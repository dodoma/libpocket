#ifndef __GLOBAL_H__
#define __GLOBAL_H__

#include <time.h>
#include "timer.h"

extern TimerEntry *g_timers;

extern time_t g_ctime;
extern time_t g_starton;
extern time_t g_elapsed;

extern bool g_dumpsend;
extern bool g_dumprecv;

#endif  /* __GLOBAL_H__ */
