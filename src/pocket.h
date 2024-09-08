#ifndef __POCKET_H__
#define __POCKET_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>

#if defined(ANDROID)
#include <android/log.h>
void TINY_LOG(const char* fmt, ...)
{
    va_list arg;
    va_start(arg, fmt);
    __android_log_vprint(ANDROID_LOG_DEBUG, "flutter", fmt, arg);
    va_end(arg);
}
#else
#define TINY_LOG(fmt, ...)                                              \
    do {                                                                \
        char _timestr[25] = {0};                                        \
        time_t _tvs = time(NULL);                                       \
        struct tm *_tm = localtime(&_tvs);                              \
        strftime(_timestr, 25, "%Y-%m-%d %H:%M:%S", _tm);               \
        fprintf(stdout, "[%s][%s:%d %s] ", _timestr, __FILE__, __LINE__, __func__); \
        fprintf(stdout, fmt, ##__VA_ARGS__);                            \
        fprintf(stdout, "\n");                                          \
    } while(0)
#endif  /* ANDROID */

#endif  /* __POCKET_H__ */
