#include <reef.h>

#include "global.h"
#include "callback.h"
#include "mnet.h"
#include "packet.h"
#include "client.h"
#include "server.h"

#define HEARTBEAT_PERIOD 5
#define HEARTBEAT_TIMEOUT 15
#define RECONNECT_TIMEOUT 30

time_t g_ctime, g_starton, g_elapsed;

pthread_t m_worker;
MsourceNode *m_sources = NULL;

/*
 * 争取做到 moc server 与 音源 一套心跳维护逻辑
 */
static bool _keep_heartbeat(void *data)
{
    MsourceNode *item = (MsourceNode*)data;

    TINY_LOG("on keep heartbeat timeout");

    uint8_t sendbuf[256] = {0};
    size_t sendlen = packetPINGFill(sendbuf, sizeof(sendbuf));

    if (!item->contrl.online) {
        /* 如果已经断连，仅尝试重连 */
        if (g_ctime > item->contrl.pong && g_ctime - item->contrl.pong > RECONNECT_TIMEOUT) {
            TINY_LOG("reconnect to %s", item->id);

            item->contrl.pong = g_ctime;
            serverConnect(&item->contrl);
        }
    } else {
        /* 链接正常，进行心跳保持和通畅判断 */
        if (g_ctime > item->contrl.pong && g_ctime - item->contrl.pong > HEARTBEAT_TIMEOUT) {
            TINY_LOG("connection lost on timeout %d", item->contrl.fd);
            /* 当然，遗失的心跳，也可能网络畅通，但服务器没回 PONG 包 */

            item->contrl.online = false;
            item->contrl.pong = g_ctime;

            callbackOn(SEQ_CONNECTION_LOST, 0, false, item->id, NULL);
        } else {
            send(item->contrl.fd, sendbuf, sendlen, MSG_NOSIGNAL);
            //MSG_LOG("SEND: ", sendbuf, sendlen);
        }
    }

    if (!item->binary.online) {
        if (g_ctime > item->binary.pong && g_ctime - item->binary.pong > RECONNECT_TIMEOUT) {
            //TINY_LOG("reconnect to %s", item->id);

            item->binary.pong = g_ctime;
            //serverConnect(&item->binary);
        }
    } else {
        if (g_ctime > item->binary.pong && g_ctime - item->binary.pong > HEARTBEAT_TIMEOUT) {
            TINY_LOG("connection lost on timeout %d", item->binary.fd);

            item->binary.online = false;
            item->binary.pong = g_ctime;

            callbackOn(SEQ_CONNECTION_LOST, 1, false, item->id, NULL);
        } else {
            send(item->binary.fd, sendbuf, sendlen, MSG_NOSIGNAL);
            //MSG_LOG("SEND: ", sendbuf, sendlen);
        }
    }

    return true;
}

static void _timer_handler(int fd)
{
    uint64_t value;
    read(fd, &value, sizeof(uint64_t));

    time_t now = time(NULL);
    if (now <= g_ctime) return;

    //TINY_LOG("tick tock");

    g_ctime = now;
    g_elapsed = now - g_starton;

    TimerEntry *t = g_timers, *p, *n;
    p = NULL;
    while (t && t->timeout > 0) {
        n = t->next;

        if (t->right_now || (g_elapsed % t->timeout == 0 && !t->pause)) {
            t->right_now = false;
            if (!t->callback(t->data)) {
                if (p) p->next = n;
                if (t == g_timers) g_timers = n;

                free(t);
            } else p = t;
        } else p = t;

        t = n;
    }
}

/*
 * 网络监控主程序
 */
static void* el_routine(void *arg)
{
    /*
     * 由于 select 会持续返回套接字可写，用时加入writeset又显得过于复杂，
     * 故，为避免不必要的资源开销，写操作不由 select 管理。
     */
    fd_set readset;
    int maxfd, rv;

    TINY_LOG("start event loop routine");

    while (true) {
        maxfd = 0;
        FD_ZERO(&readset);

        MsourceNode *item = m_sources;
        while (item) {
            if (item->contrl.fd > maxfd) maxfd = item->contrl.fd;
            if (item->binary.fd > maxfd) maxfd = item->binary.fd;
            if (item->contrl.online && item->contrl.fd > 0) {
                //TINY_LOG("add contrl fd %d", item->contrl.fd);
                FD_SET(item->contrl.fd, &readset);
            }
            if (item->binary.online && item->binary.fd > 0) {
                //TINY_LOG("add binary fd %d", item->binary.fd);
                FD_SET(item->binary.fd, &readset);
            }

            item = item->next;
        }

        struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
        rv = select(maxfd + 1, &readset, NULL, NULL, &tv);
        //TINY_LOG("select return %d", rv);

        if (rv == -1) {
            TINY_LOG("select error %s", strerror(errno));
            break;
        } else if (rv == 0) continue;

        item = m_sources;
        while (item) {
            if (item->pos == MNET_ONLINE_TIMER) {
                if (FD_ISSET(item->contrl.fd, &readset)) _timer_handler(item->contrl.fd);
            } else if (item->pos == MNET_ONLINE_LAN) {
                if (FD_ISSET(item->contrl.fd, &readset)) {
                    clientRecv(item->contrl.fd, &item->contrl);
                }
                if (FD_ISSET(item->binary.fd, &readset)) {
                    //clientRecv(item->binary.fd, &item->binary);
                }
            }

            item = item->next;
        }
    }

    return NULL;
}

bool mnetStart()
{
    g_ctime = time(NULL);
    g_starton = g_ctime;
    g_elapsed = 0;

#define RETURN(ret)                             \
    do {                                        \
        free(item);                             \
        return (ret);                           \
    } while (0)

    /* timer source */
    MsourceNode *item = calloc(1, sizeof(MsourceNode));
    memset(item, 0x0, sizeof(MsourceNode));
    item->ip = NULL;
    item->contrl.fd = -1;
    item->binary.fd = -1;
    item->contrl.online = true;
    item->pos = MNET_ONLINE_TIMER;

    item->contrl.fd = timerfd_create(CLOCK_REALTIME, 0);
    if (item->contrl.fd == -1) {
        TINY_LOG("create timer failure");
        RETURN(false);
    } else TINY_LOG("timer fd %d", item->contrl.fd);

    struct itimerspec new_value;
    new_value.it_value.tv_sec = 1;
    new_value.it_value.tv_nsec = 0;
    new_value.it_interval.tv_sec = 1;
    new_value.it_interval.tv_nsec = 0;
    if (timerfd_settime(item->contrl.fd, 0, &new_value, NULL) == -1) {
        TINY_LOG("set time failure");
        RETURN(false);
    }

    item->next = m_sources;
    m_sources = item;

    pthread_create(&m_worker, NULL, el_routine, NULL);

#undef RETURN

    return true;
}

/*
 * 收到了音源广播包，尝试与其建立链接，并将链接后的网络信息加入监听范围
 */
static bool _onbroadcast(char cpuid[LEN_CPUID], char ip[INET_ADDRSTRLEN],
                         uint16_t port_contrl, uint16_t port_binary)
{
    TINY_LOG("got broadcast from %s with ip %s, port %d %d", cpuid, ip, port_contrl, port_binary);

#define RETURN(ret)                                         \
    do {                                                    \
        free(item);                                         \
        return (ret);                                       \
    } while (0)

    MsourceNode *item = calloc(1, sizeof(MsourceNode));
    memset(item, 0x0, sizeof(MsourceNode));

    memcpy(item->id, cpuid, LEN_CPUID);
    item->ip = strdup(ip);
    item->contrl.fd = -1;
    item->contrl.online = false;
    item->contrl.port = port_contrl;
    item->contrl.pong = g_ctime;
    item->contrl.bufrecv = NULL;
    item->contrl.recvlen = 0;
    item->contrl.upnode = item;

    item->binary.fd = -1;
    item->binary.online = false;
    item->binary.port = port_binary;
    item->binary.pong = g_ctime;
    item->binary.bufrecv = NULL;
    item->binary.recvlen = 0;
    item->binary.upnode = item;

    item->pos = MNET_ONLINE_LAN;

    if (!serverConnect(&item->contrl)) RETURN(false);

    if (!serverConnect(&item->binary)) RETURN(false);

    g_timers = timerAdd(g_timers, HEARTBEAT_PERIOD, true, item, _keep_heartbeat);

    TINY_LOG("%s contrl fd %d, binary fd %d", cpuid, item->contrl.fd, item->binary.fd);

    item->next = m_sources;
    m_sources = item;

    return true;
#undef RETURN
}

char* mnetDiscovery()
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        TINY_LOG("create socket failure");
        return NULL;
    }

    int enable = 1;
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) != 0) {
        TINY_LOG("set reuseable error %s", strerror(errno));
        return NULL;
    }

    struct sockaddr_in ownsa, othersa;
    int otherlen = sizeof(othersa);
    ownsa.sin_family = AF_INET;
    ownsa.sin_addr.s_addr = INADDR_ANY;
    ownsa.sin_port = htons(PORT_BROADCAST_DST);

    int rv = bind(fd, (const struct sockaddr*)&ownsa, sizeof(ownsa));
    if (rv != 0) {
        close(fd);
        TINY_LOG("bind failure %s", strerror(errno));
        return NULL;
    }

    TINY_LOG("discovery music source...");

    uint8_t rcvbuf[256];
#ifdef ANDROID
    rv = TEMP_FAILURE_RETRY(recvfrom(fd, rcvbuf, sizeof(rcvbuf), 0,
                                     (struct sockaddr*)&othersa, (socklen_t*)&otherlen));
#else
    rv = recvfrom(fd, rcvbuf, sizeof(rcvbuf), 0, (struct sockaddr*)&othersa, (socklen_t*)&otherlen);
#endif
    if (rv > 0) {
        static char cpuid[LEN_CPUID] = "unkownID";
        char ip[INET_ADDRSTRLEN];
        inet_ntop(othersa.sin_family, (void*)&othersa.sin_addr, ip, INET6_ADDRSTRLEN);
        int port = ntohs(othersa.sin_port);
        TINY_LOG("received %d bytes data from %s %d", rv, ip, port);

        CommandPacket *packet = packetCommandGot(rcvbuf, rv);
        if (packet && packet->frame_type == FRAME_MSG) {
            if (packet->command == CMD_BROADCAST) {
                uint8_t *buf = packet->data;

                int idlen = strlen((char*)buf);
                strncpy(cpuid, (char*)buf, idlen > LEN_CPUID ? LEN_CPUID : idlen);
                buf += idlen;
                buf++;          /* '\0' */

                uint16_t port_contrl = *(uint16_t*)buf; buf += 2;
                uint16_t port_binary = *(uint16_t*)buf; buf += 2;

                _onbroadcast(cpuid, ip, port_contrl, port_binary);
            }
        }

        close(fd);
        return cpuid;
    } else {
        TINY_LOG("receive error %d %s", rv, strerror(errno));

        close(fd);
        return NULL;
    }
}

MsourceNode* _source_find(MsourceNode *nodes, char *id)
{
    MsourceNode *node = nodes;
    while (node) {
        if (node->pos > MNET_ONLINE_TIMER && !strcmp(node->id, id)) return node;

        node = node->next;
    }

    return NULL;
}

/*
 * ============ business ============
 */
bool mnetWifiSet(char *id, const char *ap, const char *passwd, const char *name,
                 CONTRL_CALLBACK callback)
{
    if (!id) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    if (item->pos != MNET_ONLINE_LAN) {
        TINY_LOG("set wifi only works on network LAN");
        return false;
    }

    NetNode *node = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "ap", ap);
    mdf_set_value(datanode, "passwd", passwd);
    mdf_set_value(datanode, "name", name);

    CommandPacket *packet = packetCommandFill(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_WIFI_SET, datanode);
    if (sendlen == 0) {
        TINY_LOG("message too loooong");
        mdf_destroy(&datanode);
        return false;
    }
    packetCRCFill(packet);
    if (callback) callbackRegist(packet->seqnum, packet->command, callback);

    SSEND(node->fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    return true;
}


char* mnetDiscover2()
{
    sleep(3);
    //return "imdiscover";
    return NULL;
}


#ifdef EXECUTEABLE

static void _on_wifi_setted(bool success, char *errmsg, MDF *nodein)
{
    TINY_LOG("wifi setted %s", success ? "OK" : errmsg);
}

int main(int argc, char *argv[])
{
    clientInit();
    callbackStart();
    mnetStart();

    char *id = mnetDiscovery();

    TINY_LOG("%s", id);

    sleep(20);

    mnetWifiSet("a4204428f3063", "TPLINK_2323", "123123", "No.419", _on_wifi_setted);

    sleep(100);

    callbackStop();

    return 0;
}
#endif
