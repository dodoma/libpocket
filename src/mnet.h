#ifndef __MNET_H__
#define __MNET_H__

#include "pocket.h"
#include "domme.h"

/*
 * mnet, 网络相关
 */

#define LEN_CPUID 14
#define LEN_CLIENTID 11
#define LEN_PACKET_NORMAL 1024
#define CONTRL_PACKET_MAX_LEN 10485760
#define PORT_BROADCAST_DST 4102

typedef enum {
    MNET_OFFLINE = 0,
    MNET_ONLINE_TIMER,          /* 内部定时器使用 */
    MNET_ONLINE_LAN,
    MNET_ONLINE_WAN,
    MNET_ONLINE_MOC,
} MSOURCE_POSITION;

typedef enum {
    CLIENT_CONTRL = 0,
    CLIENT_BINARY,
} CLIENT_TYPE;

typedef struct {
    int fd;
    uint16_t port;
    time_t pong;
    CLIENT_TYPE ctype;
    bool online;

    struct _msource_node *upnode;
} NetNode;

typedef struct {
    NetNode base;

    uint8_t *bufrecv;
    uint8_t bufsend[LEN_PACKET_NORMAL];
    size_t recvlen;
} CtlNode;

typedef struct {
    NetNode base;

    uint8_t *bufrecv;
    size_t recvlen;

    char tempname[PATH_MAX];
    char filename[PATH_MAX];

    FILE *fpbin;
    uint64_t binlen;
    uint32_t needToSync;
    uint32_t syncDone;
    bool downloading;
} BinNode;

typedef struct _msource_node {
    char id[LEN_CPUID];
    char *ip;

    char myid[LEN_CLIENTID];    /* client id produced by node */

    MDF *dbnode;

    DommeStore *plan;

    CtlNode contrl;
    BinNode binary;

    MSOURCE_POSITION pos;

    struct _msource_node *next;
} MsourceNode;

typedef void (*CONTRL_CALLBACK)(NetNode *client, bool success, char *errmsg, char *response);

/*
 * 启动网络监控线程
 * 调且仅能调用一次
 */
bool mnetStart(const char *appdir);

char* mnetAppDir();

/* 查找本地设备
 * 若无，持续等待
 * 若有，返回局域网内音源设备SN编号，并尝试与其建立链接
 * 若出错，返回NULL
 */
char* mnetDiscovery();

/*
 * 随机播放歌曲
 * 仅本地链接时可调用
 */
bool mnetPlayRandom(char *id);

bool mnetWifiSet(char *id, char *ap, char *passwd, char *name, CONTRL_CALLBACK callback);

bool mnetPlayInfo(char *id, CONTRL_CALLBACK callback);
bool mnetOnStep(char *id, CONTRL_CALLBACK callback);

bool mnetOnServerConnectted(void (*callback)(char *id, CLIENT_TYPE type));
bool mnetOnServerClosed(void (*callback)(char *id, CLIENT_TYPE type));
bool mnetOnConnectionLost(void (*callback)(char *id, CLIENT_TYPE type));
bool mnetOnReceiving(void (*callback)(char *id, char *name));
bool mnetOnFileReceived(void (callback)(char *id, char *name));
bool mnetOnReceiveDone(void (*callback)(char *id, int filecount));

bool mnetSetShuffle(char *id, bool shuffle);
bool mnetSetVolume(char *id, double volume);
bool mnetStoreSwitch(char *id, char *name);
bool mnetPlay(char *id);
bool mnetPlayID(char *id, char *trackid);
bool mnetPlayAlbum(char *id, char *name, char *title);
bool mnetPlayArtist(char *id, char *name);
bool mnetPause(char *id);
bool mnetResume(char *id);
bool mnetNext(char *id);
bool mnetPrevious(char *id);
bool mnetDragTO(char *id, double percent);

bool mnetStoreList(char *id);
bool mnetStoreSync(char *id, char *storename);
bool mnetSyncTracks(char *id);
bool mnetDeleteTrack(char *id, char *trackid);

bool mnetNTSCheck(void *arg);

char* mnetDiscover2();

#endif  /* __MNET_H__ */
