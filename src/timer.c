#include "timer.h"
#include "pocket.h"

TimerEntry *g_timers = NULL;

TimerEntry* timerAdd(TimerEntry *entry, int timeout, bool rightnow,
                     void *data, bool (*callback)(void *data))
{
    TimerEntry *e = calloc(1, sizeof(TimerEntry));
    e->timeout = timeout;
    e->right_now = rightnow;
    e->pause = false;
    e->data = data;
    e->callback = callback;
    e->next = entry;

    return e;
}
