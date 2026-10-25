// SharedMemory.cpp
// Gery Vessere - gvessere@illumina.com, gery@vessere.com
// An abstraction over both SysV and POSIX shared memory APIs

#ifndef SHAREDMEMORY_H
#define SHAREDMEMORY_H


#include <string>
#ifdef _WIN32
    #include "wincompat.h"
#else
    #include <semaphore.h>
    #include <unistd.h>
    #include <sys/ipc.h>
#endif
#include <exception>
#include <iostream>


enum ErrorState {
ENONE,
ENOTALLOCATED,
ETRYAGAIN,
EALREADYALLOCATED,
EOPENFAILED,
EEXISTS,
EFTRUNCATE,
EMAPFAILED,
ECLOSE,
EUNLINK,
ECOUNTERCREATE,
ECOUNTERREMOVE,
ECOUNTERUSE,
ELOCK,
EKEYCOLLISION
};


class SharedMemoryException: public std::exception
{
private:
    bool _hasError;
    ErrorState _error;
    int _errorDetail;

public:

    SharedMemoryException()
    {
        _hasError = false;
        _error = ENONE;
        _errorDetail = 0;
    };

    SharedMemoryException(ErrorState error): _hasError(error != ENONE), _error(error), _errorDetail(0)
    {};

    ErrorState GetErrorCode() const
    {
        return _error;
    };

    int GetErrorDetail() const
    {
        return _errorDetail;
    }

    bool HasError() const
    {
        return _hasError;
    };

    void SetError(ErrorState error, int detail)
    {
        if (!_hasError)
        {
            _hasError = true;
            _error = error;
            _errorDetail = detail;
        }
    }

    void ClearError()
    {
        _hasError = false;
        _error = ENONE;
        _errorDetail = 0;
    };
};

class SharedMemory
{
public:
        void * GetMapped()
        {
            return _mapped ? (void *) ((char*) _mapped + sizeof(size_t)) : nullptr;
        };

        size_t GetSize()
        {
            // Usable mapped capacity, which may exceed the allocation request
            // when the OS rounds a POSIX shared-memory object to page size.
            if (!_needsAllocation)
                return _mappedSize - sizeof(size_t);

            _exception.SetError(ENOTALLOCATED, 0);
            return static_cast<size_t>(-1);
        };

        bool NeedsAllocation() const
        {
            return _needsAllocation;
        };

        // the owner is the first one that created the named shared memory segment
        bool IsAllocator() const
        {
            return _isAllocator;
        };

        int GetId() const
        {
            return _shmID;
        };

        bool HasError() const
        {
            return _exception.HasError();
        }

        void ThrowError(ErrorState error, int detail)
        {
            if (!_exception.HasError())
            {
                _exception.SetError(error, detail);
            }
            throw _exception;
        };

        void ThrowError(ErrorState error)
        {
            ThrowError(error, 0);
        };

        void SetErrorStream(std::ostream * err)
        {
            _err = err;
        };

        // State of the process that created the segment, as recorded by the
        // admission semaphore set. Unknown: not recorded (for example a
        // segment created by an older STAR build, or Windows).
        enum class LoaderState { Unknown, Loading, Exited };
        LoaderState GetLoaderState();
        // The allocator calls this after publishing the loaded genome.
        void LoaderFinished();

        SharedMemory(key_t key, bool unloadLast);
        SharedMemory(const SharedMemory&) = delete;
        SharedMemory& operator=(const SharedMemory&) = delete;
        SharedMemory(SharedMemory&&) = delete;
        SharedMemory& operator=(SharedMemory&&) = delete;
        ~SharedMemory();
        void Allocate(size_t shmSize);
        void Clean();

private:
        SharedMemoryException _exception;

        int _shmID;
        int _sharedCounterID;
        void * _counterMem;

        void * _mapped;
        size_t _mappedSize = 0;
        bool _isAllocator;
        bool _needsAllocation;

        key_t _key;
        key_t _counterKey;
        // SysV semaphore set keyed by _key (semaphore keys do not share the
        // shared-memory key namespace): 0 = admission lock, 1 = loader alive,
        // 2 = loader recorded. See SharedMemory.cpp.
        int _lockSemID = -1;
        bool _lockHeld = false;
        bool _loaderRegistered = false;
        bool _unloadLast;
        std::ostream * _err;

        int SharedObjectsUseCount();
        void OpenIfExists();
        void CreateAndInitSharedObject(size_t shmSize);
        void MapSharedObjectToMemory();
        std::string GetPosixObjectKey();
        struct stat GetSharedObjectInfo();
        void Close();
        void Unlink();
        void CleanLocked();

        void EnsureCounter();
        void RemoveSharedCounter();

        void LockAdmission();
        void UnlockAdmission();
        void RemoveLockSemaphore();
        class AdmissionGuard;
};

#endif
