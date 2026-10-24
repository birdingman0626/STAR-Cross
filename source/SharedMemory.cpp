// SharedMemory.cpp
// Gery Vessere - gvessere@illumina.com, gery@vessere.com
// An abstraction over both SysV and POSIX shared memory APIs

#include "SharedMemory.h"
#include <sstream>
#include <limits>
#ifdef _WIN32
    #include "wincompat.h"
#else
    #include <sys/mman.h>
    #include <sys/shm.h>
    #include <sys/stat.h>        /* For mode constants */
    #include <fcntl.h>           /* For O_* constants */
    #include <semaphore.h>
    #include <sys/sem.h>
    #include <errno.h>
    #include <sys/types.h>
#endif

#if defined(COMPILE_FOR_MAC) || defined(__APPLE__) || defined(__FreeBSD__)
  // macOS/FreeBSD: SHM_NORESERVE is Linux-specific; define as 0 (no-op)
  #ifndef SHM_NORESERVE
    #define SHM_NORESERVE 0
  #endif
  #ifndef MAP_NORESERVE
    #define MAP_NORESERVE 0
  #endif
#endif

using namespace std;

// Cross-process admission protocol (SysV semaphores; not on Windows, where the
// shared-memory genome is unavailable).
//
// One semaphore set per genome key (semaphore keys are a separate namespace
// from shared-memory keys) holds:
//   0  admission lock. 0 = unlocked. Lock = one atomic semop {wait for 0, +1},
//      unlock = {-1}, both with SEM_UNDO, so a holder that dies releases it and
//      a freshly created set (all zeros on Linux and macOS) is already unlocked.
//   1  loader alive: +1 with SEM_UNDO when a process creates the segment, -1
//      after it publishes the loaded genome. A crashed loader drops it to 0.
//   2  loader recorded: +1 (no undo) when a process creates the segment, so a
//      waiter can tell a dead loader from a segment created by an older build.
// The lock serializes counter attach + segment open (constructor), segment
// creation (Allocate), and count + detach + remove (destructor, Clean), so a
// job that arrives while the last user removes the genome is either counted
// before the removal or starts after it.
namespace {
    constexpr int kSemLock = 0;
    constexpr int kSemLoaderAlive = 1;
    constexpr int kSemLoaderRecorded = 2;
    constexpr int kSemCount = 3;
#ifndef _WIN32
    bool semaphoreGone(int err)
    {
        return err == EIDRM || err == EINVAL; // macOS reports a removed set as EINVAL
    }
#endif
}

class SharedMemory::AdmissionGuard
{
public:
    explicit AdmissionGuard(SharedMemory & shm): _shm(shm) { _shm.LockAdmission(); }
    ~AdmissionGuard() { _shm.UnlockAdmission(); }
    AdmissionGuard(const AdmissionGuard&) = delete;
    AdmissionGuard& operator=(const AdmissionGuard&) = delete;
private:
    SharedMemory & _shm;
};

void SharedMemory::LockAdmission()
{
#ifndef _WIN32
    // A set removed by Clean() while we wait is replaced by a new one; the
    // bound only stops a foreign set at this key from looping forever.
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        if (_lockSemID < 0)
        {
            _lockSemID = semget(_key, kSemCount, IPC_CREAT | 0666);
            if (_lockSemID < 0)
                ThrowError(ELOCK, errno);
        }
        struct sembuf ops[2];
        ops[0].sem_num = kSemLock; ops[0].sem_op = 0; ops[0].sem_flg = 0;
        ops[1].sem_num = kSemLock; ops[1].sem_op = 1; ops[1].sem_flg = SEM_UNDO;
        if (semop(_lockSemID, ops, 2) == 0)
        {
            _lockHeld = true;
            return;
        }
        const int err = errno;
        if (err == EINTR)
            continue;
        if (semaphoreGone(err))
        {
            _lockSemID = -1;
            continue;
        }
        ThrowError(ELOCK, err);
    }
    ThrowError(ELOCK, EAGAIN);
#endif
}

void SharedMemory::UnlockAdmission()
{
#ifndef _WIN32
    if (!_lockHeld)
        return;
    _lockHeld = false;
    struct sembuf op;
    op.sem_num = kSemLock; op.sem_op = -1; op.sem_flg = SEM_UNDO;
    while (semop(_lockSemID, &op, 1) == -1 && errno == EINTR)
    {}
    // Any other failure means the set is gone, which also releases the lock.
#endif
}

void SharedMemory::RemoveLockSemaphore()
{
#ifndef _WIN32
    if (_lockSemID < 0)
        _lockSemID = semget(_key, 0, 0);
    if (_lockSemID >= 0 && semctl(_lockSemID, 0, IPC_RMID) == -1 && !semaphoreGone(errno))
        ThrowError(ELOCK, errno);
    _lockSemID = -1;
    _lockHeld = false; // removing the set releases the lock for every waiter
    _loaderRegistered = false;
#endif
}

SharedMemory::LoaderState SharedMemory::GetLoaderState()
{
#ifndef _WIN32
    if (_lockSemID < 0)
        _lockSemID = semget(_key, 0, 0);
    if (_lockSemID < 0)
        return LoaderState::Unknown;
    // Read "recorded" first: the creator raises both in one semop.
    const int recorded = semctl(_lockSemID, kSemLoaderRecorded, GETVAL);
    if (recorded <= 0)
        return LoaderState::Unknown;
    const int alive = semctl(_lockSemID, kSemLoaderAlive, GETVAL);
    if (alive < 0)
        return LoaderState::Unknown; // set removed meanwhile (explicit Remove)
    return alive > 0 ? LoaderState::Loading : LoaderState::Exited;
#else
    return LoaderState::Unknown;
#endif
}

void SharedMemory::LoaderFinished()
{
#ifndef _WIN32
    if (!_loaderRegistered)
        return;
    _loaderRegistered = false;
    struct sembuf op;
    op.sem_num = kSemLoaderAlive; op.sem_op = -1; op.sem_flg = SEM_UNDO;
    while (semop(_lockSemID, &op, 1) == -1 && errno == EINTR)
    {}
#endif
}

SharedMemory::SharedMemory(key_t key, bool unloadLast): _key(key), _counterKey(key+1), _unloadLast(unloadLast), _err(&cerr)
{
    _shmID = -1;
    _sharedCounterID = -1;
    _counterMem = 0;
    _mapped=NULL;
    _isAllocator = false;
    _needsAllocation = true;

    try
    {
        AdmissionGuard guard(*this);
        EnsureCounter();
        OpenIfExists();
    }
    catch (...)
    {
        try { Close(); } catch (...) {}
        throw;
    }
}

SharedMemory::~SharedMemory()
{
    // Explicit Clean() already detached this instance. Do not reattach a counter
    // that was removed, or recreate it while destroying a finished owner.
    if (_counterMem == NULL)
        return;
    try
    {
        AdmissionGuard guard(*this);
        LoaderFinished(); // an unfinished load is abandoned; waiters detect it
        int inUse = SharedObjectsUseCount()-1;
        const bool noSegment = _needsAllocation;
        Close();

        if (noSegment)
        {
            // Never found or created a segment (e.g. Remove of an unloaded
            // genome): drop the counter and semaphores if nobody else uses them.
            if (inUse == 0)
                CleanLocked();
        }
        else if (_unloadLast)
        {
            if (inUse > 0)
            {
                (*_err) << inUse << " other job(s) are attached to the shared memory segment, will not remove it." <<endl;
            }
            else
            {
                (*_err) << "No other jobs are attached to the shared memory segment, removing it."<<endl;
                CleanLocked();
            }
        }
    }
    catch (...)
    {
        // A failed usage-count query is not permission to remove another job's
        // mapping. Detach only; explicit Remove remains the removal authority.
        try
        {
           Close();
        }
        catch (...)
        {}
    }
}

void SharedMemory::Allocate(size_t shmSize)
{
    _exception.ClearError();

    if (!_needsAllocation)
        ThrowError(EALREADYALLOCATED);

    if (shmSize > std::numeric_limits<size_t>::max() - sizeof(size_t))
        ThrowError(EOPENFAILED, EOVERFLOW);
    AdmissionGuard guard(*this);
    CreateAndInitSharedObject(shmSize);

    if (_exception.HasError() && _exception.GetErrorCode() != EEXISTS)
        throw _exception;

    const bool created = !_exception.HasError();
    _exception.ClearError(); // someone else came in first so retry open

    try {
        OpenIfExists();
    } catch (...) {
        // Roll back only the object created by this allocation attempt. Never
        // unlink a pre-existing segment when attaching/mapping it fails.
        if (created) {
            _needsAllocation = false;
            try { Unlink(); } catch (...) {} // preserve the original failure
        }
        throw;
    }

#ifndef _WIN32
    if (created)
    {
        // Record the loader in the lock section that created the segment, so
        // no waiter can see the segment without also seeing its loader.
        struct sembuf ops[2];
        ops[0].sem_num = kSemLoaderAlive; ops[0].sem_op = 1; ops[0].sem_flg = SEM_UNDO;
        ops[1].sem_num = kSemLoaderRecorded; ops[1].sem_op = 1; ops[1].sem_flg = 0;
        int ret;
        while ((ret = semop(_lockSemID, ops, 2)) == -1 && errno == EINTR)
        {}
        if (ret == -1)
        {
            const int detail = errno;
            try { Close(); Unlink(); } catch (...) {}
            ThrowError(ELOCK, detail);
        }
        _loaderRegistered = true;
    }
#endif
    _isAllocator = created;
}

string SharedMemory::GetPosixObjectKey()
{
    ostringstream key;
    key << "/" << _key;
    return key.str();
}


void SharedMemory::CreateAndInitSharedObject(size_t shmSize)
{
    size_t toReserve = shmSize + sizeof(size_t);

#ifdef POSIX_SHARED_MEM
    _shmID=shm_open(GetPosixObjectKey().c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
#else
    _shmID=shmget(_key, toReserve, IPC_CREAT | IPC_EXCL | SHM_NORESERVE | 0666); //        _shmID = shmget(shmKey, shmSize, IPC_CREAT | SHM_NORESERVE | SHM_HUGETLB | 0666);
#endif

    if (_shmID == -1)
    {
        switch (errno)
        {
            case EEXIST:
                _exception.SetError(EEXISTS, 0);
                break;
            default:
                ThrowError(EOPENFAILED, errno);
        }
        return;
    }

#ifdef POSIX_SHARED_MEM
    int err = ftruncate(_shmID, toReserve);
    if (err == -1)
    {
        const int detail = errno;
        _needsAllocation = false; // this attempt owns the newly created name
        try { Unlink(); } catch (...) {}
        ThrowError(EFTRUNCATE, detail);
    }
#endif
}

void SharedMemory::OpenIfExists()
{
    errno=0;
    if (_shmID < 0){
#ifdef POSIX_SHARED_MEM
        _shmID=shm_open(GetPosixObjectKey().c_str(), O_RDWR, 0);
#else
        _shmID=shmget(_key,0,0);
#endif
}
    bool exists=_shmID>=0;
    if (! (exists || errno == ENOENT))
        ThrowError(EOPENFAILED, errno); // it's there but we couldn't get a handle

    if (exists)
    {
        MapSharedObjectToMemory();

        _needsAllocation = false;
    }
}

#ifdef POSIX_SHARED_MEM
struct stat SharedMemory::GetSharedObjectInfo()
{
    struct stat buf;
    int err = fstat(_shmID, &buf);
    if (err == -1)
        ThrowError(EOPENFAILED, errno);

    return buf;
}
#endif

void SharedMemory::MapSharedObjectToMemory()
{
#ifdef POSIX_SHARED_MEM
    size_t size=0;
    struct stat buf = SharedMemory::GetSharedObjectInfo();
    size = (size_t) buf.st_size;
    if (size < sizeof(size_t))
        ThrowError(EMAPFAILED, EINVAL);
    void * mapped = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_NORESERVE, _shmID, (off_t) 0);

    if (mapped==((void *) -1))
        ThrowError(EMAPFAILED, errno);

    _mapped = mapped;
    _mappedSize = size;
#else
#ifdef _WIN32
    ThrowError(EMAPFAILED, ENOSYS); // Windows shim deliberately does not implement IPC.
#else
    struct shmid_ds info;
    if (shmctl(_shmID, IPC_STAT, &info) == -1)
        ThrowError(EMAPFAILED, errno);
    if (info.shm_segsz == 1)
    {
        // Another genome's usage counter (always 1 byte) lives at this key:
        // that genome's key is ours minus 1. Never use or remove it.
        _shmID = -1;
        ThrowError(EKEYCOLLISION, EEXIST);
    }
    if (info.shm_segsz < sizeof(size_t))
        ThrowError(EMAPFAILED, EINVAL);
    void * mapped = shmat(_shmID, NULL, 0);

    if (mapped==((void *) -1))
        ThrowError(EMAPFAILED, errno);

    _mapped = mapped;
    _mappedSize = info.shm_segsz;
#endif
#endif
}

void SharedMemory::Close()
{
    #ifdef POSIX_SHARED_MEM
    if (_mapped != NULL)
    {
        int ret = munmap(_mapped, _mappedSize);
        if (ret == -1)
            ThrowError(EMAPFAILED, errno);
        _mapped = NULL;
    }

    if (_shmID != -1)
    {
        int err = close(_shmID);
        _shmID=-1;
        if (err == -1)
            ThrowError(ECLOSE, errno);
    }

    #else
    if (_mapped != NULL)
    {
        if (shmdt(_mapped) == -1)
            ThrowError(ECLOSE, errno);
        _mapped = NULL;
    }
    #endif
    _mappedSize = 0;
    if (_counterMem != NULL)
    {
        if (shmdt(_counterMem) == -1)
            ThrowError(ECLOSE, errno);
        _counterMem = NULL;
    }
}

void SharedMemory::Unlink()
{
    if (!_needsAllocation)
    {
        int shmStatus=-1;
    #ifdef POSIX_SHARED_MEM
        shmStatus = shm_unlink(GetPosixObjectKey().c_str());
    #else
        struct shmid_ds buf;
        shmStatus=shmctl(_shmID,IPC_RMID,&buf);
    #endif
        if (shmStatus == -1)
            ThrowError(EUNLINK, errno);

        _needsAllocation = true;
    }
}

void SharedMemory::Clean()
{
    AdmissionGuard guard(*this);
    CleanLocked();
}

void SharedMemory::CleanLocked()
{
    _loaderRegistered = false;
    Close();
    Unlink();
    RemoveSharedCounter();
    RemoveLockSemaphore(); // last: releases the lock and wakes waiting jobs
}

void SharedMemory::EnsureCounter()
{
    if (_sharedCounterID < 0)
        _sharedCounterID=shmget(_counterKey,0,0);

    bool exists=_sharedCounterID>=0;

    if (!exists)
    {
        errno=0;
        _sharedCounterID=shmget(_counterKey, 1, IPC_CREAT | IPC_EXCL | SHM_NORESERVE | 0666);

        if (_sharedCounterID < 0 && errno == EEXIST)
            _sharedCounterID=shmget(_counterKey,0,0);
        if (_sharedCounterID < 0)
            ThrowError(ECOUNTERCREATE, errno);
    }

    // The counter key (key+1) is also the data key of a genome directory whose
    // inode is key+1. Counters are always 1 byte; anything else is that other
    // genome's data, which must never be attached as a counter or removed.
    struct shmid_ds counterInfo;
    if (shmctl(_sharedCounterID, IPC_STAT, &counterInfo) == 0 && counterInfo.shm_segsz != 1)
    {
        _sharedCounterID = -1;
        ThrowError(EKEYCOLLISION, EEXIST);
    }

    if (_counterMem == 0)
    {
        void * counterMem = shmat(_sharedCounterID, NULL, 0);

        if (counterMem==((void *) -1))
            ThrowError(EMAPFAILED, errno);
        _counterMem = counterMem;
    }
}

void SharedMemory::RemoveSharedCounter()
{
    if (_sharedCounterID == -1)
        return;
    struct shmid_ds buf;
    int shmStatus=shmctl(_sharedCounterID,IPC_RMID,&buf);
    if (shmStatus == -1)
        ThrowError(ECOUNTERREMOVE, errno);
    _sharedCounterID = -1;
}

int SharedMemory::SharedObjectsUseCount()
{
    EnsureCounter();
    if (_sharedCounterID != -1)
    {
        struct shmid_ds shmStat;
        int shmStatus=shmctl(_sharedCounterID,IPC_STAT,&shmStat);
        if (shmStatus == -1)
            ThrowError(ECOUNTERUSE, errno);
#ifdef SHM_DEST
        // IPC_STAT can still succeed after IPC_RMID while we remain attached.
        // Such a counter no longer tracks newly arriving users by this key.
        if ((shmStat.shm_perm.mode & SHM_DEST) != 0)
            ThrowError(ECOUNTERUSE, EIDRM);
#endif

        return shmStat.shm_nattch;
    }
    else
        return -1;
}
