#ifndef __CALLBACK_H__
#define __CALLBACK_H__

/*
 * callback, 处理用户的网络事件回调
 */
typedef void (*CONTRL_CALLBACK)(bool success, char *errmsg, char *response);

typedef struct queue_entry {
    uint16_t seqnum;
    uint16_t command;
    bool success;
    char *errmsg;
    char *response;

    struct queue_entry *next;
} QueueEntry;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;

    ssize_t size;
    QueueEntry *top;
    QueueEntry *bottom;
} QueueManager;

typedef struct {
    uint16_t seqnum;
    uint16_t command;
    CONTRL_CALLBACK callback;
} CallbackEntry;

typedef struct {
    bool running;
    QueueManager *queue;
    pthread_t worker;

    MDLIST *callbacks;
} CallbackManager;

void callbackStart();
void callbackStop();

void callbackRegist(uint16_t seqnum, uint16_t command, CONTRL_CALLBACK callback);
void callbackOn(uint16_t seqnum, uint16_t command, bool success, char *errmsg, char *response);
void callbackEntryFree(void *p);

void queueEntryFree(void *p);
QueueEntry* queueEntryGet(QueueManager *queue);
void queueEntryPush(QueueManager *queue, QueueEntry *entry);

QueueManager* queueCreate();
void queueFree(QueueManager *queue);

#endif  /* __CALLBACK_H__ */
