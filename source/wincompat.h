// wincompat.h - Windows compatibility shims for POSIX APIs used by STAR
// This header is only included on Windows (_WIN32) builds.

#ifndef STAR_WINCOMPAT_H
#define STAR_WINCOMPAT_H

#ifdef _WIN32

// Prevent winsock conflicts
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------- Type definitions ----------

typedef int key_t;
typedef int mode_t;
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;

#ifndef _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#define _SSIZE_T_DEFINED
#endif

// ---------- GCC built-in replacements ----------
#include <intrin.h>

static inline int __builtin_ctz(unsigned int x) {
    unsigned long index;
    _BitScanForward(&index, x);
    return (int)index;
}

static inline int __builtin_ctzll(unsigned long long x) {
    unsigned long index;
    _BitScanForward64(&index, x);
    return (int)index;
}

static inline int __builtin_clz(unsigned int x) {
    unsigned long index;
    _BitScanReverse(&index, x);
    return 31 - (int)index;
}

static inline int __builtin_popcount(unsigned int x) {
    return (int)__popcnt(x);
}

// ---------- 128-bit integer (MSVC has no __uint128_t) ----------
// Used only in SuffixArrayFuns.cpp for 16-byte loads. Use a struct.
#ifndef uint128
struct _uint128_t {
    unsigned long long lo;
    unsigned long long hi;
};
#define uint128 _uint128_t
#define STAR_UINT128_STRUCT 1
#endif

// ---------- File/directory operations ----------

// mkdir: POSIX takes 2 args, MSVC _mkdir takes 1
#ifndef _STAR_MKDIR_DEFINED
#define _STAR_MKDIR_DEFINED
static inline int star_mkdir(const char *path, mode_t /*mode*/) {
    return _mkdir(path);
}
#define mkdir(path, mode) star_mkdir(path, mode)
#endif

// rmdir
#define rmdir _rmdir

// getcwd
#define getcwd _getcwd

// chmod
#define chmod _chmod

// access
#define access _access
#define F_OK 0
#define R_OK 4
#define W_OK 2

// open / lseek (low-level I/O) - avoid close/read/write macros as they conflict with C++ stream methods
#define lseek _lseek

// popen / pclose
#define popen _popen
#define pclose _pclose

// ftruncate
static inline int ftruncate(int fd, long long length) {
    return _chsize_s(fd, length);
}

// ---------- stat compatibility ----------
// MSVC's struct stat works, but st_ino is always 0 on NTFS.
// For shmKey derivation, we use a hash of the path instead.

// ---------- statvfs stub ----------
// Used in streamFuns.cpp for disk space reporting.
// Provide a basic implementation using GetDiskFreeSpaceEx.
struct statvfs {
    unsigned long long f_bsize;
    unsigned long long f_bavail;
};

static inline int statvfs(const char *path, struct statvfs *buf) {
    ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;
    // Extract drive root from path
    char root[4] = {0};
    if (path && path[0] && path[1] == ':') {
        root[0] = path[0];
        root[1] = ':';
        root[2] = '\\';
        root[3] = '\0';
    } else {
        root[0] = '\0'; // current drive
    }
    if (GetDiskFreeSpaceExA(root[0] ? root : NULL, &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
        buf->f_bsize = 1; // report in bytes directly
        buf->f_bavail = freeBytesAvailable.QuadPart;
        return 0;
    }
    buf->f_bsize = 1;
    buf->f_bavail = 0;
    return -1;
}

// ---------- sleep ----------
// POSIX sleep() takes seconds; Windows Sleep() takes milliseconds
static inline unsigned int sleep(unsigned int seconds) {
    Sleep(seconds * 1000);
    return 0;
}

// ---------- Signal handling stubs ----------
#ifndef SIGKILL
#define SIGKILL 9
#endif

static inline int kill(pid_t pid, int /*sig*/) {
    if (pid <= 0) return -1;
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (hProcess == NULL) return -1;
    BOOL result = TerminateProcess(hProcess, 1);
    CloseHandle(hProcess);
    return result ? 0 : -1;
}

// ---------- FIFO stub ----------
// mkfifo is not available on Windows. The callers will use an alternative path.
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IXUSR _S_IEXEC
#define S_IRWXU (_S_IREAD | _S_IWRITE | _S_IEXEC)
#define S_IRWXG 0
#define S_IRWXO 0

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

// ---------- symlink stub ----------
// symlink is not reliably available on Windows; use copy as fallback
static inline int symlink(const char *target, const char *linkpath) {
    return CopyFileA(target, linkpath, FALSE) ? 0 : -1;
}

// ---------- Shared memory stubs ----------
// STAR on Windows only supports --genomeLoad NoSharedMemory.
// These stubs allow compilation; runtime use will throw an error.

#define IPC_CREAT  0001000
#define IPC_EXCL   0002000
#define IPC_RMID   0
#define IPC_STAT   2
#define SHM_NORESERVE 0

struct shmid_ds {
    size_t shm_segsz;
    int shm_nattch;
};

static inline int shmget(key_t /*key*/, size_t /*size*/, int /*shmflg*/) {
    errno = ENOSYS;
    return -1;
}

static inline void *shmat(int /*shmid*/, const void * /*shmaddr*/, int /*shmflg*/) {
    errno = ENOSYS;
    return (void*)-1;
}

static inline int shmdt(const void * /*shmaddr*/) {
    errno = ENOSYS;
    return -1;
}

static inline int shmctl(int /*shmid*/, int /*cmd*/, struct shmid_ds * /*buf*/) {
    errno = ENOSYS;
    return -1;
}

// ---------- mmap stubs ----------
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_SHARED  0x01
#define MAP_NORESERVE 0x4000
#define MAP_FAILED  ((void*)-1)

static inline void *mmap(void * /*addr*/, size_t /*length*/, int /*prot*/, int /*flags*/, int /*fd*/, long long /*offset*/) {
    errno = ENOSYS;
    return MAP_FAILED;
}

static inline int munmap(void * /*addr*/, size_t /*length*/) {
    errno = ENOSYS;
    return -1;
}

// ---------- Semaphore stubs ----------
typedef HANDLE sem_t;

static inline sem_t *sem_open(const char *name, int /*oflag*/, ...) {
    sem_t *sem = (sem_t*)malloc(sizeof(sem_t));
    if (!sem) return NULL;
    *sem = CreateSemaphoreA(NULL, 1, 1, name);
    return *sem ? sem : NULL;
}

static inline int sem_close(sem_t *sem) {
    if (sem && *sem) { CloseHandle(*sem); free(sem); }
    return 0;
}

static inline int sem_unlink(const char * /*name*/) {
    return 0; // Windows semaphores auto-cleanup
}

// ---------- shm_open / shm_unlink stubs (for POSIX_SHARED_MEM) ----------
static inline int shm_open(const char * /*name*/, int /*oflag*/, mode_t /*mode*/) {
    errno = ENOSYS;
    return -1;
}

static inline int shm_unlink(const char * /*name*/) {
    errno = ENOSYS;
    return -1;
}

// ---------- vfork / execlp stubs ----------
// On Windows, the FIFO code path uses _popen/system() instead.
// These stubs exist only to allow compilation of code that is
// guarded by #ifndef _WIN32 at runtime.

// ---------- nftw replacement ----------
// Used in sysRemoveDir.cpp. Provide a Windows implementation there instead.
#define FTW_F   0
#define FTW_D   1
#define FTW_DP  6
#define FTW_DEPTH 0x08

struct FTW {
    int base;
    int level;
};

// ---------- pthread compatibility layer ----------
// Minimal pthread implementation using Windows threads

#include <stdint.h>

typedef HANDLE pthread_t;
typedef CRITICAL_SECTION pthread_mutex_t;

// Simplified attributes (ignored)
typedef int pthread_attr_t;

#define PTHREAD_MUTEX_INITIALIZER {0}

static inline int pthread_mutex_init(pthread_mutex_t *mutex, const void * /*attr*/) {
    InitializeCriticalSection(mutex);
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    DeleteCriticalSection(mutex);
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *mutex) {
    EnterCriticalSection(mutex);
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    LeaveCriticalSection(mutex);
    return 0;
}

struct _pthread_start_info {
    void *(*start_routine)(void*);
    void *arg;
};

static inline DWORD WINAPI _pthread_start_wrapper(LPVOID param) {
    struct _pthread_start_info *info = (struct _pthread_start_info*)param;
    void *(*start_routine)(void*) = info->start_routine;
    void *arg = info->arg;
    free(info);
    start_routine(arg);
    return 0;
}

static inline int pthread_create(pthread_t *thread, const pthread_attr_t * /*attr*/,
                                  void *(*start_routine)(void*), void *arg) {
    struct _pthread_start_info *info = (struct _pthread_start_info*)malloc(sizeof(struct _pthread_start_info));
    if (!info) return ENOMEM;
    info->start_routine = start_routine;
    info->arg = arg;
    // STAR uses large stack arrays in alignment code; use 64MB per thread
    // (Linux default is 8MB, STAR needs more for deep recursion + stack arrays)
    *thread = CreateThread(NULL, 64 * 1024 * 1024, _pthread_start_wrapper, info, 0, NULL);
    if (*thread == NULL) {
        free(info);
        return EAGAIN;
    }
    return 0;
}

static inline int pthread_join(pthread_t thread, void ** /*retval*/) {
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}

static inline void pthread_exit(void * /*retval*/) {
    ExitThread(0);
}

#endif // _WIN32
#endif // STAR_WINCOMPAT_H
