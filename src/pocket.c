#include "pocket.h"

#if defined(ANDROID)
void TINY_LOG(const char* fmt, ...)
{
    va_list arg;
    va_start(arg, fmt);
    __android_log_vprint(ANDROID_LOG_DEBUG, "flutter", fmt, arg);
    va_end(arg);
}
#endif