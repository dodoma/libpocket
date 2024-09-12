#ifndef __MNET_H__
#define __MNET_H__

#include "pocket.h"

/*
 * mnet, 网络相关
 */

#define LEN_CPUID 14
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

struct net_node {
    int fd;
    uint16_t port;
    time_t pong;

    uint8_t *bufrecv;
    uint8_t  bufsend[LEN_PACKET_NORMAL];
    bool dropped;
    bool complete;
};

typedef struct _msource_node {
    char id[LEN_CPUID];
    char *ip;

    struct net_node contrl;
    struct net_node binary;

    MSOURCE_POSITION pos;

    struct _msource_node *next;
} msourceNode;

/*
 * 启动网络监控线程
 * 调且仅能调用一次
 */
bool mnetStart();

/* 查找本地设备
 * 若无，持续等待
 * 若有，返回局域网内音源设备SN编号，并尝试与其建立链接
 * 若出错，返回NULL
 */
char* mnetDiscovery();

/*
 * 与 moc server 建立长连接，并保持心跳
 * 成功链接返回 true
 * 失败返回 false
 */
bool mnetMocConnect();

/*
 * 判断音源设备 sn 远程在线状态
 * 同步请求，数秒内返回，有超时判断
 */
bool mnetOnlineCheck(char *id);

/*
 * 随机播放歌曲
 * 仅本地链接时可调用
 */
bool mnetPlayRandom(char *id);

bool mnetWifiSet(char *id, const char *ap, const char *passwd, const char *name);


char* mnetDiscover2();

#endif  /* __MNET_H__ */
