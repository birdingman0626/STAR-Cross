#include <string>
#include <time.h>

// Thread-safe wrapper: fills tmBuf via localtime_r (POSIX) or localtime_s (MSVC).
static void localtime_safe(const time_t *t, struct tm *tmBuf) {
#ifdef _WIN32
    localtime_s(tmBuf, t);
#else
    localtime_r(t, tmBuf);
#endif
}

std::string timeMonthDayTime() {
    time_t rawTime;
    char timeChar[100];
    struct tm tmBuf;
    time(&rawTime);
    localtime_safe(&rawTime, &tmBuf);
    strftime(timeChar,80,"%b %d %H:%M:%SS",&tmBuf);
    std::string timeString=timeChar;
    timeString.erase(timeString.end()-1,timeString.end());
    return timeString;
};

std::string timeMonthDayTime(time_t &rawTime) {
    char timeChar[100];
    struct tm tmBuf;
    localtime_safe(&rawTime, &tmBuf);
    strftime(timeChar,80,"%b %d %H:%M:%SS",&tmBuf);
    std::string timeString=timeChar;
    timeString.erase(timeString.end()-1,timeString.end());
    return timeString;
};
