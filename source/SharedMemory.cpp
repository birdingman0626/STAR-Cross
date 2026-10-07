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

SharedMemory::SharedMemory(key_t key, bool unloadLast): _key(key), _counterKey(key+1), _unloadLast(unloadLast), _err(&cerr)
{
    _shmID = -1;
    _sharedCounterID = -1;
    _counterMem = 0;
    _mapped=NULL;
    _sem=NULL;
    _isAllocator = false;
    _needsAllocation = true;

    try
    {
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
        int inUse = SharedObjectsUseCount()-1;
        Close();

        if (_unloadLast)
        {
            if (inUse > 0)
            {
                (*_err) << inUse << " other job(s) are attached to the shared memory segment, will not remove it." <<endl;
            }
            else
            {
                (*_err) << "No other jobs are attached to the shared memory segment, removing it."<<endl;
                Clean();
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
    CreateAndInitSharedObject(shmSize);

    if (_exception.HasError() && _exception.GetErrorCode() != EEXISTS)
        throw _exception;

    const bool created = !_exception.HasError();
    _exception.ClearError(); // someone else came in first so retry open

    OpenIfExists();

    _isAllocator = created;
}

string SharedMemory::GetPosixObjectKey()
{
    ostringstream key;
    key << "/" << _key;
    return key.str();
}

string SharedMemory::CounterName()
{
    ostringstream counterName;
    counterName << "/shared_use_counter" << _key;
    return counterName.str();
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
        ThrowError(EFTRUNCATE);
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
    Close();
    Unlink();
    RemoveSharedCounter();
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
