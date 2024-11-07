#include "timer.h"
#include "pocket.h"

TimerEntry *g_timers = NULL;

TimerEntry* timerAdd(TimerEntry *entry, int timeout, bool rightnow,
                     void *data, bool (*callback)(void *data))
{
    TimerEntry *node = entry;
    while (node) {
        if (node->callback == callback) {
            node->timeout = timeout;
            node->right_now = rightnow;
            node->pause = false;
            node->data = data;

            return entry;
        }

        node = node->next;
    }

    TimerEntry *e = calloc(1, sizeof(TimerEntry));
    e->timeout = timeout;
    e->right_now = rightnow;
    e->pause = false;
    e->data = data;
    e->callback = callback;
    e->next = entry;

    return e;
}
