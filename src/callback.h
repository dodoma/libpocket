#ifndef __CALLBACK_H__
#define __CALLBACK_H__

/*
 * callback, 处理用户的网络事件回调
 */
typedef struct queue_entry {
    NetNode *client;
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

void callbackSetServerConnectted(void (*callback)(char *id, CLIENT_TYPE type));
void callbackSetServerClosed(void (*callback)(char *id, CLIENT_TYPE type));
void callbackSetConnectionLost(void (*callback)(char *id, CLIENT_TYPE type));
void callbackSetOnReceiving(void (*callback)(char *id, char *fname));
void callbackSetOnFileReceived(void (*callback)(char *id, char *fname));
void callbackSetOnReceiveDone(void (*callback)(char *id, int filecount));
void callbackSetUdiskMounted(void (*callback)(char *id));
void callbackSetFree(void (*callback)(char *id));
void callbackSetBusyIndexing(void (*callback)(char *id));

void callbackServerConnectted(char *id, CLIENT_TYPE type);
void callbackOnReceiving(char *id, char *fname);
void callbackOnFileReceived(MsourceNode *item, char *fname);
void callbackOnReceiveDone(char *id, int filecount);
void callbackUdiskMounted(char *id);
void callbackFree(char *id);
void callbackBusyIndexing(char *id);

/*
 * 如果 errmsg, response 不为空，请确保为跨线程安全内存，回调完毕后会自动释放
 */
void callbackOn(NetNode *client, uint16_t seqnum, uint16_t command,
                bool success, char *errmsg, char *response);
void callbackEntryFree(void *p);

void queueEntryFree(void *p);
QueueEntry* queueEntryGet(QueueManager *queue);
void queueEntryPush(QueueManager *queue, QueueEntry *entry);

QueueManager* queueCreate();
void queueFree(QueueManager *queue);

#endif  /* __CALLBACK_H__ */
