// win32_compat.h - Windows compatibility for htslib C code
// Included via force-include in CMakeLists.txt on Windows builds

#ifndef HTSLIB_WIN32_COMPAT_H
#define HTSLIB_WIN32_COMPAT_H

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <time.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Type definitions
typedef int mode_t;
typedef int pid_t;
typedef int uid_t;
typedef int gid_t;

#ifndef _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#define _SSIZE_T_DEFINED
#endif

// PATH_MAX
#ifndef PATH_MAX
#define PATH_MAX 260
#endif

// SSIZE_MAX
#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t)(((size_t)(~0)) >> 1))
#endif

// Signals
#include <signal.h>
#ifndef SIGTERM
#define SIGTERM 15
#endif
#ifndef SIGPIPE
#define SIGPIPE 13
#endif

// Standard file descriptors
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

// Provide stubs for headers that don't exist on Windows
// unistd.h equivalents
#define access _access
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define getcwd _getcwd
#define lseek _lseek
#define open _open
#define close _close
#define read _read
#define write _write
#define popen _popen
#define pclose _pclose

static __inline int ftruncate(int fd, long long length) {
    return _chsize_s(fd, length);
}

#define mkdir(path, mode) _mkdir(path)

// sleep
static __inline unsigned int sleep(unsigned int seconds) {
    Sleep(seconds * 1000);
    return 0;
}

// File permission bits
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IXUSR _S_IEXEC
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IXGRP 0
#define S_IROTH 0
#define S_IWOTH 0
#define S_IXOTH 0
#define S_IRWXU (_S_IREAD | _S_IWRITE | _S_IEXEC)
#define S_IRWXG 0
#define S_IRWXO 0

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

// Stub out sysconf
#ifndef sysconf
#define sysconf(x) 512
#endif

// pthread compatibility - minimal for htslib thread pool
typedef HANDLE hts_pthread_t;
typedef CRITICAL_SECTION hts_pthread_mutex_t;
typedef CONDITION_VARIABLE hts_pthread_cond_t;

// Map pthread types to hts_ prefixed ones to avoid conflicts
#define pthread_t hts_pthread_t
#define pthread_mutex_t hts_pthread_mutex_t
#define pthread_cond_t hts_pthread_cond_t
#define pthread_attr_t int

#define PTHREAD_MUTEX_INITIALIZER {0}
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_MUTEX_RECURSIVE 1
typedef int pthread_condattr_t;
typedef int pthread_mutexattr_t;

static __inline int pthread_mutexattr_init(pthread_mutexattr_t *attr) { *attr = 0; return 0; }
static __inline int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) { *attr = type; return 0; }
static __inline int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) { (void)attr; return 0; }

static __inline int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize) {
    (void)attr; *stacksize = 16 * 1024 * 1024; return 0;
}
static __inline int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize) {
    (void)attr; (void)stacksize; return 0;
}

static __inline int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr) {
    (void)attr;
    InitializeCriticalSection(mutex);
    return 0;
}

static __inline int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    DeleteCriticalSection(mutex);
    return 0;
}

static __inline int pthread_mutex_lock(pthread_mutex_t *mutex) {
    EnterCriticalSection(mutex);
    return 0;
}

static __inline int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    LeaveCriticalSection(mutex);
    return 0;
}

static __inline int pthread_cond_init(pthread_cond_t *cond, const void *attr) {
    (void)attr;
    InitializeConditionVariable(cond);
    return 0;
}

static __inline int pthread_cond_destroy(pthread_cond_t *cond) {
    (void)cond;
    return 0; // Windows condition variables don't need explicit destroy
}

static __inline int pthread_cond_signal(pthread_cond_t *cond) {
    WakeConditionVariable(cond);
    return 0;
}

static __inline int pthread_cond_broadcast(pthread_cond_t *cond) {
    WakeAllConditionVariable(cond);
    return 0;
}

static __inline int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    SleepConditionVariableCS(cond, mutex, INFINITE);
    return 0;
}

struct _hts_pthread_start_info {
    void *(*start_routine)(void*);
    void *arg;
};

static __inline DWORD WINAPI _hts_pthread_start_wrapper(LPVOID param) {
    struct _hts_pthread_start_info *info = (struct _hts_pthread_start_info*)param;
    void *(*start_routine)(void*) = info->start_routine;
    void *arg = info->arg;
    free(info);
    start_routine(arg);
    return 0;
}

static __inline int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                    void *(*start_routine)(void*), void *arg) {
    struct _hts_pthread_start_info *info;
    (void)attr;
    info = (struct _hts_pthread_start_info*)malloc(sizeof(struct _hts_pthread_start_info));
    if (!info) return ENOMEM;
    info->start_routine = start_routine;
    info->arg = arg;
    *thread = CreateThread(NULL, 0, _hts_pthread_start_wrapper, info, 0, NULL);
    if (*thread == NULL) {
        free(info);
        return EAGAIN;
    }
    return 0;
}

static __inline int pthread_join(pthread_t thread, void **retval) {
    (void)retval;
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}

static __inline void pthread_exit(void *retval) {
    (void)retval;
    ExitThread(0);
}

static __inline int pthread_detach(pthread_t thread) {
    CloseHandle(thread);
    return 0;
}

// Networking stubs - STAR doesn't use remote file access from htslib
// but the code references socket headers. Stub them out.
#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H
// Prevent inclusion of sys/socket.h
#endif

#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H
// Prevent inclusion of sys/select.h
#endif

// GCC atomic built-ins
#define __sync_fetch_and_add(ptr, val) InterlockedExchangeAdd((volatile LONG*)(ptr), (val))

// String comparison
#define strcasecmp _stricmp
#define strncasecmp _strnicmp

// fsync
#define fsync(fd) _commit(fd)

// alloca
#include <malloc.h>
#define alloca _alloca

// pthread_attr functions (stubs)
static __inline int pthread_attr_init(pthread_attr_t *attr) { (void)attr; return 0; }
static __inline int pthread_attr_setdetachstate(pthread_attr_t *attr, int state) { (void)attr; (void)state; return 0; }
static __inline int pthread_attr_destroy(pthread_attr_t *attr) { (void)attr; return 0; }

// pthread_cond_timedwait
static __inline int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime) {
    DWORD ms = INFINITE;
    if (abstime) {
        FILETIME ft;
        ULARGE_INTEGER uli;
        GetSystemTimeAsFileTime(&ft);
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        long long now_ms = (long long)(uli.QuadPart / 10000ULL - 11644473600000ULL);
        long long target_ms = (long long)abstime->tv_sec * 1000LL + abstime->tv_nsec / 1000000LL;
        long long diff = target_ms - now_ms;
        ms = (diff > 0) ? (DWORD)diff : 0;
    }
    return SleepConditionVariableCS(cond, mutex, ms) ? 0 : ETIMEDOUT;
}

// pthread_kill (stub - used only for thread cancellation in htslib thread pool)
static __inline int pthread_kill(pthread_t thread, int sig) {
    (void)thread; (void)sig;
    return 0; // stub - not really needed for STAR's use of htslib
}

// off_t for file operations
#ifndef off_t
#define off_t __int64
#endif

#ifndef fseeko
#define fseeko _fseeki64
#endif
#ifndef ftello
#define ftello _ftelli64
#endif

// drand48 / srand48 - used in htslib
static __inline double drand48(void) {
    return (double)rand() / RAND_MAX;
}

static __inline void srand48(long seed) {
    srand((unsigned int)seed);
}

static __inline double erand48(unsigned short xsubi[3]) {
    (void)xsubi;
    return (double)rand() / RAND_MAX;
}

static __inline long lrand48(void) {
    return rand();
}

static __inline int usleep(unsigned int usec) {
    Sleep(usec / 1000 > 0 ? usec / 1000 : 1);
    return 0;
}


// pthread_once / TLS (thread-local storage) support
typedef INIT_ONCE pthread_once_t;
#define PTHREAD_ONCE_INIT INIT_ONCE_STATIC_INIT
typedef DWORD pthread_key_t;

static __inline BOOL CALLBACK _pthread_once_cb(PINIT_ONCE io, PVOID param, PVOID *ctx) {
    (void)io; (void)ctx;
    void (*fn)(void) = (void (*)(void))param;
    fn();
    return TRUE;
}
static __inline int pthread_once(pthread_once_t *once, void (*fn)(void)) {
    InitOnceExecuteOnce(once, _pthread_once_cb, (PVOID)fn, NULL);
    return 0;
}
static __inline int pthread_key_create(pthread_key_t *key, void (*dtor)(void*)) {
    (void)dtor; *key = TlsAlloc(); return (*key == TLS_OUT_OF_INDEXES) ? -1 : 0;
}
static __inline int pthread_key_delete(pthread_key_t key) { return TlsFree(key) ? 0 : -1; }
static __inline void *pthread_getspecific(pthread_key_t key) { return TlsGetValue(key); }
static __inline int pthread_setspecific(pthread_key_t key, const void *val) { return TlsSetValue(key, (LPVOID)val) ? 0 : -1; }

#endif // _WIN32
#endif // HTSLIB_WIN32_COMPAT_H
