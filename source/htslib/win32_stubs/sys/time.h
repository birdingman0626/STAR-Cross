// Stub sys/time.h for Windows builds
#ifndef _SYS_TIME_H_WIN32_STUB
#define _SYS_TIME_H_WIN32_STUB

#ifdef _WIN32
#include <time.h>
#include <winsock2.h> // for struct timeval

// gettimeofday
static __inline int gettimeofday(struct timeval *tv, void *tz) {
    FILETIME ft;
    unsigned long long tmpres = 0;
    (void)tz;
    if (tv) {
        GetSystemTimeAsFileTime(&ft);
        tmpres |= ft.dwHighDateTime;
        tmpres <<= 32;
        tmpres |= ft.dwLowDateTime;
        // Convert to microseconds since Unix epoch
        tmpres /= 10;
        tmpres -= 11644473600000000ULL;
        tv->tv_sec = (long)(tmpres / 1000000UL);
        tv->tv_usec = (long)(tmpres % 1000000UL);
    }
    return 0;
}
#endif

#endif
