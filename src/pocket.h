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
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>

#include <sys/timerfd.h>
#include <fcntl.h>

#if defined(ANDROID)
#include <android/log.h>
void TINY_LOG(const char* fmt, ...);
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

#define MSG_LOG(noise, pre, p, psize)                           \
    do {                                                        \
        if ((noise) && (ssize_t)(psize) > 0) {                  \
            char zstra[(psize)*2+1];                            \
            pocket_bin2hexstr((uint8_t*)(p), (psize), zstra);   \
            TINY_LOG("%s%zu %s", pre, (size_t)(psize), zstra);  \
        }                                                       \
    } while (0)

/*
 * 针对非阻塞套接字进行阻塞式发送（发送缓冲区满时持续阻塞）
 * 返回情况：
 * 1. true: 一次性成功发送 len 个字节
 * 2. true: 阻塞至待缓冲区可用后一/多次成功发送 len 个字节
 * 3. false: 套接字发送错误
 * 4. false: 套接字关闭
 * 4. false: 参数错误
 */
bool SSEND(int fd, uint8_t *buf, size_t len);

void pocket_bin2hexstr(uint8_t *hexin, unsigned int inlen, char *charout);

#endif  /* __POCKET_H__ */
