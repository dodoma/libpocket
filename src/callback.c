#include <reef.h>

#include "pocket.h"
#include "packet.h"
#include "callback.h"

CallbackManager *m_callback = NULL;

static void* _do(void *arg)
{
    int rv;
    CallbackManager *worker = (CallbackManager*)arg;
    QueueManager *queue = worker->queue;

    TINY_LOG("start callback worker");

    while (worker->running) {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 1;

        pthread_mutex_lock(&queue->lock);

        while (queue->size == 0 &&
               (rv = pthread_cond_timedwait(&queue->cond, &queue->lock, &timeout)) == ETIMEDOUT) {
            timeout.tv_sec += 1;
            if (!worker->running) break;
        }

        if (!worker->running) {
            pthread_mutex_unlock(&queue->lock);
            break;
        }

        if (queue->size == 0 && rv != 0) {
            TINY_LOG("timedwait error %s", strerror(errno));
            pthread_mutex_unlock(&queue->lock);
            continue;
        }

        /* rv == 0 */
        QueueEntry *qentry = queueEntryGet(queue);
        pthread_mutex_unlock(&queue->lock);

        if (qentry) {
            MDLIST *dlist = mdlist_head(m_callback->callbacks);
            while (dlist) {
                CallbackEntry *centry = (CallbackEntry*)mdlist_data(dlist);
                if (centry->seqnum == qentry->seqnum) {
                    centry->callback(qentry->success, qentry->errmsg, qentry->response);

                    if (centry->seqnum >= SEQ_USER_START) mdlist_eject(dlist, callbackEntryFree);

                    queueEntryFree(qentry);
                    goto done;
                }

                dlist = mdlist_next(dlist);
            }

            TINY_LOG("whoops, callback %d uneixst", qentry->seqnum);
            queueEntryFree(qentry);

        done:
            ;
        }
    }

    TINY_LOG("callback worker done");

    return NULL;
}

void queueEntryFree(void *p)
{
    if (!p) return;

    QueueEntry *entry = (QueueEntry*)p;

    /* errmsg 可能固定赋值，此处不负责释放，用户自行看管 */
    if (entry->response) free(entry->response);
    free(entry);
}

QueueEntry* queueEntryGet(QueueManager *queue)
{
    if (!queue || !queue->bottom || queue->size < 1) return NULL;

    QueueEntry *entry = queue->bottom;

    queue->bottom = entry->next;

    if (queue->size == 1) {
        queue->top = NULL;      /* redundancy */
        queue->bottom = NULL;
    }

    queue->size -= 1;

    return entry;
}

void queueEntryPush(QueueManager *queue, QueueEntry *entry)
{
    if (!queue || !entry) return;

    //entry->next = queue->top; /* 方便后入先出 */
    if (queue->top) queue->top->next = entry; /* 方便先入先出 */

    queue->top = entry;

    if (queue->size == 0) queue->bottom = entry;

    queue->size += 1;

    return;
}

QueueManager* queueCreate()
{
    QueueManager *queue = mos_calloc(1, sizeof(QueueManager));
    queue->size = 0;
    queue->top = NULL;
    queue->bottom = NULL;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutex_init(&(queue->lock), &attr);
    pthread_mutexattr_destroy(&attr);
    pthread_cond_init(&(queue->cond), NULL);

    return queue;
}

void queueFree(QueueManager *queue)
{
    QueueEntry *entry = queueEntryGet(queue);
    while (entry) {
        queueEntryFree(entry);

        entry = queueEntryGet(queue);
    }

    pthread_mutex_destroy(&queue->lock);

    mos_free(queue);
}

static void _server_closed(bool success, char *errmsg, char *response)
{
    TINY_LOG("server %s closed", errmsg);
}

static void _connection_lost(bool success, char *errmsg, char *response)
{
    TINY_LOG("lost connection with %s", errmsg);
}

void callbackStart()
{
    if (!m_callback) {
        m_callback = calloc(1, sizeof(CallbackManager));
        m_callback->running = true;
        m_callback->queue = queueCreate();
        m_callback->callbacks = NULL;
        pthread_create(&m_callback->worker, NULL, _do, m_callback);

        callbackRegist(SEQ_SERVER_CLOSED, 0, _server_closed);
        callbackRegist(SEQ_CONNECTION_LOST, 0, _connection_lost);
    }
}

void callbackStop()
{
    if (!m_callback) return;

    m_callback->running = false;
    pthread_join(m_callback->worker, NULL);
    queueFree(m_callback->queue);
    free(m_callback);
    m_callback = NULL;
}

void callbackOn(uint16_t seqnum, uint16_t command, bool success, char *errmsg, char *response)
{
    QueueEntry *entry = calloc(1, sizeof(QueueEntry));
    entry->seqnum = seqnum;
    entry->command = command;
    entry->success = success;
    entry->errmsg = errmsg;
    entry->response = response;
    entry->next = NULL;

    pthread_mutex_lock(&m_callback->queue->lock);
    queueEntryPush(m_callback->queue, entry);
    pthread_cond_signal(&m_callback->queue->cond);
    pthread_mutex_unlock(&m_callback->queue->lock);
}

void callbackRegist(uint16_t seqnum, uint16_t command, CONTRL_CALLBACK callback)
{
    if (!callback) return;

    CallbackEntry *centry = calloc(1, sizeof(CallbackEntry));
    centry->seqnum = seqnum;
    centry->callback = callback;

    MDLIST *dlist = mdlist_new(centry);
    m_callback->callbacks = mdlist_concat(m_callback->callbacks, dlist);
}

void callbackEntryFree(void *p)
{
    if (p) free(p);
}
