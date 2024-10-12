#include "pocket.h"
#include "global.h"

#if defined(ANDROID)
void TINY_LOG(const char* fmt, ...)
{
    va_list arg;
    va_start(arg, fmt);
    __android_log_vprint(ANDROID_LOG_DEBUG, "flutter", fmt, arg);
    va_end(arg);
}
#endif

bool SSEND(int fd, uint8_t *buf, size_t len)
{
    ssize_t count = 0;
    int rv;

    if (fd <= 0 || !buf || len <= 0) {
        TINY_LOG("parameter error");
        return false;
    }

    while (count < len) {
        rv = send(fd, buf + count, len - count, MSG_NOSIGNAL);
        MSG_LOG(g_dumpsend, "SEND: ", buf + count, rv);

        if (rv == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            else {
                TINY_LOG("send failure %s", strerror(errno));
                return false;
            }
        } else if (rv == 0) {
            return false;
        } else {
            count += rv;
        }
    }

    return true;
}

/*
 * use < 10 judgement, or, you can use array ['0', '1', ..., 'e', 'f']
 */
void pocket_bin2hexstr(uint8_t *hexin, unsigned int inlen, char *charout)
{
    /* 48 '0' */
    /* 97 'a'  122 'z'  65 'A' */
#define HEX2STR(in, out)                        \
    do {                                        \
        if (((in) & 0xf) < 10) {                \
            (out) = ((in)&0xf) + 48;            \
        } else {                                \
            (out) = ((in)&0xf) - 10 + 65;       \
        }                                       \
    } while (0)

    if (hexin == NULL || charout == NULL)
        return;

    unsigned int i, j;
    memset(charout, 0x0, inlen*2+1);

    for (i = 0, j = 0; i < inlen; i++, j += 2) {
        HEX2STR(hexin[i]>>4, charout[j]);
        HEX2STR(hexin[i], charout[j+1]);
    }

    charout[j] = '\0';
}
