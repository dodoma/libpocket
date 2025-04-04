#include <reef.h>

#include "global.h"
#include "mnet.h"
#include "mfile.h"
#include "callback.h"
#include "packet.h"
#include "client.h"
#include "binary.h"
#include "server.h"
#include "omusic.h"

#define HEARTBEAT_PERIOD 5
#define HEARTBEAT_TIMEOUT 20
#define RECONNECT_TIMEOUT 5

#define HEARTBEAT_PERIOD_BIN 120
#define HEARTBEAT_TIMEOUT_BIN 600

time_t g_ctime, g_starton, g_elapsed;

pthread_t m_timer;
pthread_t m_worker;
MsourceNode *m_sources = NULL;

bool g_dumpsend = false;
bool g_dumprecv = false;

static char *m_appdir = NULL;
static uint8_t *m_recvbuf = NULL;
static bool m_remotedone = false;
static bool m_remoteok = false;

/*
 * 争取做到 moc server 与 音源 一套心跳维护逻辑
 */
static bool _keep_heartbeat(void *data)
{
    MsourceNode *item = (MsourceNode*)data;

    //TINY_LOG("on keep heartbeat timeout");

    /* 离线的 data 可能在切换页面时被释放掉，此处仅做粗略的判断，已防止 coredump */
    bool memsafe = false;
    MsourceNode *node = m_sources;
    while (node) {
        if (node == item) {
            memsafe = true;
            break;
        }

        node = node->next;
    }

    if (!memsafe) return false;

    uint8_t sendbuf[256] = {0};
    size_t sendlen = packetPINGFill(sendbuf, sizeof(sendbuf));

    CtlNode *contrl = &item->contrl;
    BinNode *binary = &item->binary;

    if (!contrl->base.online) {
        /* 如果已经断连，仅尝试重连 */
        if (g_ctime > contrl->base.pong && g_ctime - contrl->base.pong > RECONNECT_TIMEOUT) {
            TINY_LOG("reconnect to %s", item->id);

            contrl->base.pong = g_ctime;
            if (!binary->base.online) {
                binary->base.pong = g_ctime;
                if (!serverConnect((NetNode*)&item->binary)) {
                    TINY_LOG("binary lost, and connect failure");
                    return true;
                }
            }
            if (serverConnect((NetNode*)&item->contrl)) {
                item->pos = MNET_ONLINE_LAN;
                uint8_t connbuf[LEN_IDIOT] = {0};
                size_t connlen = packetIdiotFill(connbuf, IDIOT_CONNECT);
                send(item->contrl.base.fd, connbuf, connlen, MSG_NOSIGNAL);
                MSG_LOG(g_dumpsend, "SEND: ", connbuf, connlen);

                callbackServerConnectted(item->id, contrl->base.ctype);
            } else TINY_LOG("lost");
        }
    } else {
        /* 链接正常，进行心跳保持和通畅判断 */
        if (g_ctime > contrl->base.pong && g_ctime - contrl->base.pong > HEARTBEAT_TIMEOUT) {
            TINY_LOG("connection lost on timeout %d", contrl->base.fd);
            /* 当然，遗失的心跳，也可能网络畅通，但服务器没回 PONG 包 */

            contrl->base.online = false;
            contrl->base.pong = g_ctime;

            callbackOn((NetNode*)&item->contrl, SEQ_CONNECTION_LOST,
                       0, false, strdup(item->id), NULL);
        } else {
            send(contrl->base.fd, sendbuf, sendlen, MSG_NOSIGNAL);
            //MSG_LOG("SEND: ", sendbuf, sendlen);
        }
    }

    if (!binary->base.online) {
        if (g_ctime > binary->base.pong && g_ctime - binary->base.pong > RECONNECT_TIMEOUT) {
            TINY_LOG("reconnect to %s", item->id);

            binary->base.pong = g_ctime;
            if (serverConnect((NetNode*)&item->binary)) item->pos = MNET_ONLINE_LAN;
        }
    } else {
        if (g_ctime > binary->base.pong && g_ctime - binary->base.pong > HEARTBEAT_TIMEOUT_BIN) {
            TINY_LOG("connection lost on timeout %d", binary->base.fd);

            binary->base.online = false;
            binary->base.pong = g_ctime;

            callbackOn((NetNode*)&item->binary, SEQ_CONNECTION_LOST,
                       1, false, strdup(item->id), NULL);
        } else {
            if (g_ctime > binary->base.pong && g_ctime - binary->base.pong > HEARTBEAT_PERIOD_BIN) {
                send(binary->base.fd, sendbuf, sendlen, MSG_NOSIGNAL);
                //MSG_LOG("SEND: ", sendbuf, sendlen);
            }
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

static void* el_timer(void *arg)
{
    int timerfd = *(int*)arg;

    fd_set readset;
    int maxfd, rv;

    TINY_LOG("start timer routine with timerfd %d", timerfd);

    while (true) {
        FD_ZERO(&readset);

        if (timerfd > 0) {
            FD_SET(timerfd, &readset);
            maxfd = timerfd;
        }

        struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
        rv = select(maxfd + 1, &readset, NULL, NULL, &tv);
        //TINY_LOG("select return %d", rv);

        if (rv == -1 && errno != EINTR) {
            TINY_LOG("select error %s", strerror(errno));
            break;
        } else if (rv == 0) continue;

        if (FD_ISSET(timerfd, &readset)) _timer_handler(timerfd);
    }

    TINY_LOG("timer done");

    return NULL;
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
            if (item->contrl.base.fd > maxfd) maxfd = item->contrl.base.fd;
            if (item->binary.base.fd > maxfd) maxfd = item->binary.base.fd;
            if (item->contrl.base.online && item->contrl.base.fd > 0) {
                //TINY_LOG("add contrl fd %d", item->contrl.base.fd);
                FD_SET(item->contrl.base.fd, &readset);
            }
            if (item->binary.base.online && item->binary.base.fd > 0) {
                //TINY_LOG("add binary fd %d", item->binary.base.fd);
                FD_SET(item->binary.base.fd, &readset);
            }

            item = item->next;
        }

        struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
        rv = select(maxfd + 1, &readset, NULL, NULL, &tv);
        //TINY_LOG("select return %d", rv);

        if (rv == -1 && errno != EINTR) {
            TINY_LOG("select error %s", strerror(errno));
            break;
        } else if (rv == 0) continue;

        item = m_sources;
        while (item) {
            if (item->pos == MNET_ONLINE_LAN) {
                if (item->contrl.base.online && FD_ISSET(item->contrl.base.fd, &readset)) {
                    clientRecv(item->contrl.base.fd, &item->contrl);
                }
                if (item->binary.base.online && FD_ISSET(item->binary.base.fd, &readset)) {
                    binaryRecv(item->binary.base.fd, &item->binary);
                }
            }

            item = item->next;
        }
    }

    TINY_LOG("el routine done");

    return NULL;
}

bool mnetStart(const char *homedir)
{
    g_ctime = time(NULL);
    g_starton = g_ctime;
    g_elapsed = 0;

    if (homedir) {
        int slen = strlen(homedir);
        if (homedir[slen-1] != '/') {
            char dirname[PATH_MAX];
            snprintf(dirname, sizeof(dirname), "%s/", homedir);
            m_appdir = strdup(dirname);
        } else m_appdir = strdup(homedir);
    } else m_appdir = "";

    if (!m_recvbuf) m_recvbuf = calloc(1, CONTRL_PACKET_MAX_LEN);

    clientInit();
    binaryInit();
    callbackStart();

    /* timer */
    static int timerfd;
    timerfd = timerfd_create(CLOCK_REALTIME, 0);
    if (timerfd == -1) {
        TINY_LOG("create timer failure");
        return false;
    }

    struct itimerspec new_value;
    new_value.it_value.tv_sec = 1;
    new_value.it_value.tv_nsec = 0;
    new_value.it_interval.tv_sec = 1;
    new_value.it_interval.tv_nsec = 0;
    if (timerfd_settime(timerfd, 0, &new_value, NULL) == -1) {
        TINY_LOG("set time failure");
        return false;
    }

    pthread_create(&m_timer, NULL, el_timer, &timerfd);

    pthread_create(&m_worker, NULL, el_routine, NULL);

    return true;
}

char* mnetAppDir()
{
    return m_appdir;
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
    mdf_init(&item->dbnode);
    item->plan = NULL;
    item->contrl.base.fd = -1;
    item->contrl.base.online = false;
    item->contrl.base.port = port_contrl;
    item->contrl.base.pong = g_ctime;
    item->contrl.base.ctype = CLIENT_CONTRL;
    item->contrl.base.upnode = item;
    //
    item->contrl.bufrecv = NULL;
    item->contrl.recvlen = 0;
    pthread_mutex_init(&item->contrl.lock, NULL);
    pthread_cond_init(&item->contrl.cond, NULL);

    item->binary.base.fd = -1;
    item->binary.base.online = false;
    item->binary.base.port = port_binary;
    item->binary.base.pong = g_ctime;
    item->binary.base.ctype = CLIENT_BINARY;
    item->binary.base.upnode = item;
    //
    item->binary.bufrecv = NULL;
    item->binary.recvlen = 0;
    item->binary.fpbin = NULL;
    item->binary.binlen = 0;
    item->binary.needToSync = 0;
    item->binary.syncDone = 0;
    item->binary.downloading = false;

    item->pos = MNET_ONLINE_LAN;

    if (!serverConnect((NetNode*)&item->contrl)) RETURN(false);

    if (!serverConnect((NetNode*)&item->binary)) RETURN(false);

    /* 给俺分配个ID，方便你关联 contrl 和 binary */
    uint8_t sendbuf[LEN_IDIOT] = {0};
    size_t sendlen = packetIdiotFill(sendbuf, IDIOT_CONNECT);
    send(item->contrl.base.fd, sendbuf, sendlen, MSG_NOSIGNAL);
    MSG_LOG(g_dumpsend, "SEND: ", sendbuf, sendlen);

    g_timers = timerAdd(g_timers, HEARTBEAT_PERIOD, true, item, _keep_heartbeat);

    TINY_LOG("%s contrl fd %d, binary fd %d", cpuid, item->contrl.base.fd, item->binary.base.fd);

    item->next = m_sources;
    m_sources = item;

    return true;
#undef RETURN
}

void msourceFree(MsourceNode *item)
{
    if (!item) return;

    TINY_LOG("release offline node %s resources", item->id);

    mos_free(item->ip);
    mdf_destroy(&item->dbnode);
    dommeStoreFree(item->plan);
    mos_free(item);
}

char* mnetDiscovery()
{
    if (m_sources != NULL) {
        /* 释放掉离线的音源，以防止自动重连 */
        MsourceNode *item = m_sources, *next = NULL, *prev = NULL;
        while (item) {
            next = item->next;

            if (!item->contrl.base.online) {
                if (prev) prev->next = next;
                else m_sources = next;

                msourceFree(item);
            } else prev = item;

            item = next;
        }

        /* 暂时跟一个音源玩玩 */
        if (m_sources) return m_sources->id;
    }

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

        MessagePacket *packet = packetMessageGot(rcvbuf, rv);
        if (packet && packet->frame_type == FRAME_CMD) {
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
    if (!id) return NULL;

    MsourceNode *node = nodes;
    while (node) {
        if (node->pos > MNET_OFFLINE && !strcmp(node->id, id)) return node;

        node = node->next;
    }

    return NULL;
}

/*
 * ============ business ============
 */
bool mnetWifiSet(char *id, char *ap, char *passwd, char *name, CONTRL_CALLBACK callback)
{
    if (!id) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    if (item->pos != MNET_ONLINE_LAN) {
        TINY_LOG("set wifi only works on network LAN");
        return false;
    }

    CtlNode *node = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "ap", ap);
    mdf_set_value(datanode, "passwd", passwd);
    mdf_set_value(datanode, "name", name);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_WIFI_SET, datanode);
    if (sendlen == 0) {
        TINY_LOG("message too loooong");
        mdf_destroy(&datanode);
        return false;
    }
    packetCRCFill(packet);
    if (callback) callbackRegist(packet->seqnum, packet->command, callback);

    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    return true;
}

bool mnetPlayInfo(char *id, CONTRL_CALLBACK callback)
{
    if (!id || !callback) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetNODataFill(packet, FRAME_AUDIO, CMD_PLAY_INFO);
    packet->seqnum = SEQ_PLAY_INFO;
    packetCRCFill(packet);

    callbackRegist(packet->seqnum, packet->command, callback);

    SSEND(node->base.fd, node->bufsend, sendlen);

    return true;
}

bool mnetOnStep(char *id, CONTRL_CALLBACK callback)
{
    if (!id || !callback) return false;

    callbackRegist(SEQ_PLAY_STEP, 0, callback);

    return true;
}

bool mnetOnServerConnectted(void (*callback)(char *id, CLIENT_TYPE type))
{
    if (!callback) return false;

    callbackSetServerConnectted(callback);

    return true;
}

bool mnetOnServerClosed(void (*callback)(char *id, CLIENT_TYPE type))
{
    if (!callback) return false;

    callbackSetServerClosed(callback);

    return true;
}

bool mnetOnConnectionLost(void (*callback)(char *id, CLIENT_TYPE type))
{
    if (!callback) return false;

    callbackSetConnectionLost(callback);

    return true;
}

bool mnetOnReceiving(void (*callback)(char *id, char *name))
{
    if (!callback) return false;

    callbackSetOnReceiving(callback);

    return true;
}

bool mnetOnFileReceived(void (callback)(char *id, char *name))
{
    if (!callback) return false;

    callbackSetOnFileReceived(callback);

    return true;
}

bool mnetOnReceiveDone(void (*callback)(char *id, int filecount))
{
    if (!callback) return false;

    callbackSetOnReceiveDone(callback);

    return true;
}

bool mnetOnUdiskMount(void (*callback)(char *id))
{
    if (!callback) return false;

    callbackSetUdiskMounted(callback);

    return true;
}

bool mnetOnFree(void (*callback)(char *id))
{
    if (!callback) return false;

    callbackSetFree(callback);

    return true;
}

bool mnetOnBusyIndexing(void (*callback)(char *id))
{
    if (!callback) return false;

    callbackSetBusyIndexing(callback);

    return true;
}


bool mnetSetShuffle(char *id, bool shuffle)
{
    if (!id) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_bool_value(datanode, "shuffle", shuffle);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_AUDIO, CMD_SET_SHUFFLE, datanode);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    return true;
}

bool mnetSetVolume(char *id, double volume)
{
    if (!id || volume > 1.0) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_double_value(datanode, "volume", volume);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_AUDIO, CMD_SET_VOLUME, datanode);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    return true;
}

bool mnetStoreSwitch(char *id, char *name)
{
    if (!id || !name) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "name", name);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_AUDIO, CMD_STORE_SWITCH, datanode);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    return true;
}

bool mnetPlay(char *id)
{
    if (!id) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetNODataFill(packet, FRAME_AUDIO, CMD_PLAY);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    return true;
}

bool mnetPlayID(char *id, char *trackid)
{
    if (!id || !trackid) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "id", trackid);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_AUDIO, CMD_PLAY, datanode);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    return true;
}

bool mnetPlayAlbum(char *id, char *name, char *title)
{
    if (!id || !name || !title) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "name", name);
    mdf_set_value(datanode, "title", title);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_AUDIO, CMD_PLAY, datanode);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    return true;
}

bool mnetPlayArtist(char *id, char *name)
{
    if (!id || !name) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "name", name);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_AUDIO, CMD_PLAY, datanode);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    return true;
}

bool mnetPause(char *id)
{
    if (!id) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetNODataFill(packet, FRAME_AUDIO, CMD_PAUSE);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    return true;
}

bool mnetResume(char *id)
{
    if (!id) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetNODataFill(packet, FRAME_AUDIO, CMD_RESUME);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    return true;
}

bool mnetNext(char *id)
{
    if (!id) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetNODataFill(packet, FRAME_AUDIO, CMD_NEXT);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    return true;
}

bool mnetPrevious(char *id)
{
    if (!id) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetNODataFill(packet, FRAME_AUDIO, CMD_PREVIOUS);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    return true;
}

bool mnetDragTO(char *id, double percent)
{
    if (!id || percent > 1.0) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_double_value(datanode, "percent", percent);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_AUDIO, CMD_DRAGTO, datanode);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    return true;
}

static void _on_store_list(NetNode *client, bool success, char *errmsg, char *response)
{
    TINY_LOG("store %s %s", success ? "OK" : errmsg, response);

    MsourceNode *item = client->upnode;

    mdf_clear(item->dbnode);
    mdf_json_import_string(item->dbnode, response);

    mdf_json_export_filef(item->dbnode, "%s%s/config.json", m_appdir, item->id);

    /* 确保新建的媒体库下有数据库文件，不会因为传输延时造成APP死循环(_makesure_load()) */
    char filename[PATH_MAX];
    MDF *cnode = mdf_node_child(item->dbnode);
    while (cnode) {
        char *path = mdf_get_value(cnode, "path", NULL);
        if (path) {
            snprintf(filename, sizeof(filename), "%s%s/%smusic.db", m_appdir, item->id, path);
            if (access(filename, F_OK) != 0) {
                TINY_LOG("create %s", filename);
                mos_mkdirf(0755, "%s%s/%s", m_appdir, item->id, path);
                int fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0644);
                if (fd != -1) close(fd);
            }
        }

        cnode = mdf_node_next(cnode);
    }
}

bool mnetStoreList(char *id)
{
    if (!id) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    mos_mkdirf(0755, "%s%s", m_appdir, item->id);

    CtlNode *node = &item->contrl;

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetNODataFill(packet, FRAME_CMD, CMD_STORE_LIST);
    packetCRCFill(packet);

    callbackRegist(packet->seqnum, packet->command, _on_store_list);

    SSEND(node->base.fd, node->bufsend, sendlen);

    return true;
}

static bool _sync_database(void *arg);
static void _on_database_check(NetNode *client, bool success, char *errmsg, char *response)
{
    CtlNode *contrl = (CtlNode*)client;
    MsourceNode *item = contrl->base.upnode;
    DommeStore *plan = item->plan;

    if (!item || !plan) return;

    if (success) {
        /* db 没有变化，直接同步数据 */
        TINY_LOG("db ok, build sync list...");

        omusicStoreClear(item->id);     /* 更新客户端UI */
        dommeStoreClear(plan);

        char filename[PATH_MAX];
        snprintf(filename, sizeof(filename), "%s%s/%smusic.db", m_appdir, item->id, plan->basedir);
        MERR *err = dommeLoadFromFile(filename, plan);
        if (err != MERR_OK) {
            TINY_LOG("load db %s failure %s", filename, strerror(errno));
            merr_destroy(&err);
            return;
        }

        item->binary.needToSync = item->binary.syncDone = 0;

        MLIST *synclist = mfileBuildSynclist(contrl->base.upnode);

        TINY_LOG("SYNC LIST length %d", mlist_length(synclist));

        MDF *datanode;
        mdf_init(&datanode);

        SyncFile *file;
        MLIST_ITERATE(synclist, file) {
            item->binary.needToSync++;

            mdf_clear(datanode);
            mdf_set_int_value(datanode, "type", file->type);
            if (file->name) mdf_set_value(datanode, "name", file->name);
            if (file->id) mdf_set_value(datanode, "id", file->id);
            if (file->artist) mdf_set_value(datanode, "artist", file->artist);
            if (file->album) mdf_set_value(datanode, "album", file->album);
            /* 只有完成传送的文件才会正常命名，故此处不用再做size和checksum比较 */

            MessagePacket *packet = packetMessageInit(contrl->bufsend, LEN_PACKET_NORMAL);
            size_t sendlen = packetDataFill(packet, FRAME_STORAGE, CMD_SYNC_PULL, datanode);
            packetCRCFill(packet);

            //TINY_LOG("pull %s %s %s %s", file->name, file->id, file->artist, file->album);
            SSEND(contrl->base.fd, contrl->bufsend, sendlen);
        }

        mdf_destroy(&datanode);
        mlist_destroy(&synclist);
    } else {
        /* 等待 music.db 接收完成后同步数据 */
        TINY_LOG("db nok %s", errmsg);

        g_timers = timerAdd(g_timers, 13, false, contrl->base.upnode, _sync_database);
    }
}

static int _store_compare(const void *anode, void *key)
{
    return strcmp(mdf_get_value((MDF*)anode, "name", ""), (char*)key);
}

static bool _sync_database(void *arg)
{
    MsourceNode *item = (MsourceNode*)arg;
    CtlNode *node = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);

    mdf_set_value(datanode, "name", item->plan->name);

    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s%s/%smusic.db", m_appdir, item->id, item->plan->basedir);
    char sumstr[33] = {0};
    ssize_t filesize = mhash_md5_file_s(filename, sumstr);
    if (filesize >= 0) {
        mdf_set_int64_value(datanode, "size", filesize);
        mdf_set_value(datanode, "checksum", sumstr);
    }

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_STORAGE, CMD_DB_MD5, datanode);
    packetCRCFill(packet);

    callbackRegist(packet->seqnum, packet->command, _on_database_check);

    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    return false;
}

bool mnetStoreSync(char *id, char *storename)
{
    char filename[PATH_MAX];

    if (!id || !storename) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    MDF *snode = mdf_search(item->dbnode, storename, _store_compare);
    if (!snode) {
        TINY_LOG("find store %s failure", storename);
        //MDF_TRACE(item->dbnode);
        return false;
    }

    char *name = mdf_get_value(snode, "name", NULL);
    char *path = mdf_get_value(snode, "path", NULL);
    if (!name || !path) return false;

    /*
     * plan
     */
    if (item->plan) dommeStoreFree(item->plan);
    item->plan = dommeStoreCreate();
    item->plan->name = strdup(name);
    item->plan->basedir = strdup(path);

    /*
     * avm
     * `-- a4204428f3063
     *     |-- assets
     *     |   `-- cover
     *     |       `-- fdf1ff08ed.jpg
     *     |-- default
     *     |   `-- music.db
     *     |-- setting
     *     `-- tmp
     *
     * 6 directories, 2 files
     */
    mos_mkdirf(0755, "%s%s/tmp", m_appdir, item->id);
    mos_mkdirf(0755, "%s%s/setting", m_appdir, item->id);
    mos_mkdirf(0755, "%s%s/assets/cover", m_appdir, item->id);
    mos_mkdirf(0755, "%s%s/%s", m_appdir, item->id, path);

    _sync_database(item);

    return true;
}

bool mnetStoreSyncMEDIA_ANYWAY(char *id, char *storename)
{
    if (!id || !storename) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_bool_value(datanode, "name", storename);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_STORAGE, CMD_SYNC_STORE, datanode);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    return true;
}

/*
 * 用户通过专辑或者艺术及勾选需要同步的音频文件后，id列表保存至了 setting/needToSync 中
 * 无论是切换媒体库时同步，还是设置后触发的同步，就只管同步
 * 该函数用于检查同步是否完成，完成则清空 needToSync，否则删掉文件中已完成同步的id
 */
bool mnetNTSCheck(void *arg)
{
    MsourceNode *item = (MsourceNode*)arg;
    DommeStore *plan = item->plan;
    char filename[PATH_MAX];
    struct stat fs;

    if (!plan) return false;

    snprintf(filename, sizeof(filename), "%s%s/setting/needToSync", m_appdir, item->id);
    MLIST *synclist = mlist_build_from_textfile(filename, 128);
    if (!synclist) return false;

    int filecount = mlist_length(synclist);

    char *id;
    MLIST_ITERATE(synclist, id) {
        /* 按理说 dommeGetFile 获取不到此id文件，加上以下判断只为逻辑更加严谨 */
        if (!memcmp(id, "STOREMARK", 9)) {
            mlist_delete(synclist, _moon_i);
            _moon_i--;
            continue;
        }

        DommeFile *mfile = dommeGetFile(plan, id);
        if (mfile) {
            snprintf(filename, sizeof(filename), "%s%s/%s%s%s",
                     m_appdir, item->id, plan->basedir, mfile->dir, mfile->name);
            if (stat(filename, &fs) == 0) {
                mlist_delete(synclist, _moon_i);
                _moon_i--;
            }
        }
    }

    if (mlist_length(synclist) == 0) {
        snprintf(filename, sizeof(filename), "%s%s/setting/needToSync", m_appdir, item->id);
        TINY_LOG("All media file DONE. remove %s", filename);

        callbackOnReceiveDone(item->id, filecount);

        mlist_destroy(&synclist);

        remove(filename);
    } else {
        TINY_LOG("%d ids remain", mlist_length(synclist));

        //if (!item->binary.downloading) mnetSyncTracks(item->id);

        mlist_destroy(&synclist);
    }

    return false;
}

bool mnetSyncTracks(char *id)
{
    struct stat fs;
    MERR *err;

    if (!id) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *contrl = &item->contrl;

    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s%s/setting/needToSync", m_appdir, id);
    MLIST *synclist = mlist_build_from_textfile(filename, 128);

    TINY_LOG("sync %d files", mlist_length(synclist));

    if (mlist_length(synclist) == 0) return true;

    item->binary.needToSync = item->binary.syncDone = 0;

    MDF *datanode;
    mdf_init(&datanode);

    DommeStore *plan = item->plan;
    bool createplan = false;
    char *fileid;
    MLIST_ITERATE(synclist, fileid) {
        /* 找到合适的媒体库 */
        if (!memcmp(fileid, "STOREMARK", 9)) {
            if (!strcmp(fileid, "STOREMARK")) {
                if (createplan) {
                    createplan = false;
                    dommeStoreFree(plan);
                }

                plan = item->plan;
            } else {
                char *storename = fileid + strlen("STOREMARK");
                while (isspace(*storename)) storename++;

                if (!storename || *storename == '\0') {
                    TINY_LOG("storename not found");
                    plan = NULL;
                    continue;
                }

                MDF *snode = mdf_search(item->dbnode, storename, _store_compare);
                if (!snode) {
                    TINY_LOG("find store %s failure", storename);
                    plan = NULL;
                    continue;
                }

                plan = dommeStoreCreate();
                plan->name = strdup(storename);
                plan->basedir = mdf_get_value_copy(snode, "path", "");

                snprintf(filename, sizeof(filename), "%s%s/%smusic.db",
                         m_appdir, item->id, plan->basedir);
                err = dommeLoadFromFile(filename, plan);
                if (err != MERR_OK) {
                    TINY_LOG("load db %s failure %s", filename, strerror(errno));
                    merr_destroy(&err);
                    dommeStoreFree(plan);
                    plan = NULL;
                    continue;
                }

                createplan = true;
            }

            continue;
        }

        /* 找到媒体库下的文件 */
        DommeFile *mfile = dommeGetFile(plan, fileid);
        if (mfile) {
            snprintf(filename, sizeof(filename), "%s%s/%s%s%s",
                     m_appdir, id, plan->basedir, mfile->dir, mfile->name);
            if (stat(filename, &fs) != 0) {
                item->binary.needToSync++;

                mdf_clear(datanode);
                mdf_set_int_value(datanode, "type", SYNC_RAWFILE);
                mdf_set_valuef(datanode, "name=%s%s%s", plan->basedir, mfile->dir, mfile->name);

                MessagePacket *packet = packetMessageInit(contrl->bufsend, LEN_PACKET_NORMAL);
                size_t sendlen = packetDataFill(packet, FRAME_STORAGE, CMD_SYNC_PULL, datanode);
                packetCRCFill(packet);

                SSEND(contrl->base.fd, contrl->bufsend, sendlen);
            }
        }
    }

    mlist_destroy(&synclist);
    mdf_destroy(&datanode);

    return true;
}

bool mnetDeleteTrack(char *id, char *trackid)
{
    if (!id || !trackid) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *contrl = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);

    mdf_set_value(datanode, "id", trackid);

    MessagePacket *packet = packetMessageInit(contrl->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_STORAGE, CMD_REMOVE, datanode);
    packetCRCFill(packet);

    SSEND(contrl->base.fd, contrl->bufsend, sendlen);

    mdf_destroy(&datanode);

    return true;
}

bool mnetCancelSync(char *id)
{
    char filename[PATH_MAX];

    if (!id) return false;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    snprintf(filename, sizeof(filename), "%s%s/setting/needToSync", m_appdir, item->id);

    TINY_LOG("Cancel SYNC. remove %s", filename);

    remove(filename);
    callbackOnReceiveDone(item->id, 1);

    CtlNode *node = &item->contrl;

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetNODataFill(packet, FRAME_STORAGE, CMD_SYNC_CANCEL);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    return true;
}

void mnetOnSyncREQBack(bool success, char *message)
{
    if (message) {
        int slen = strlen(message);
        memset(m_recvbuf, 0x0, CONTRL_PACKET_MAX_LEN);
        strncpy(m_recvbuf, message, slen > CONTRL_PACKET_MAX_LEN ? CONTRL_PACKET_MAX_LEN : slen);
    }

    m_remoteok = success;
    m_remotedone = true;
}

char* msourceHome(char *id)
{
    int rv = 0;

    if (!id) return NULL;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return NULL;

    CtlNode *node = &item->contrl;

    m_remotedone = false;

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetNODataFill(packet, FRAME_HARDWARE, CMD_HOME_INFO);
    packet->seqnum = SEQ_SYNC_REQ;
    packetCRCFill(packet);
    SSEND(node->base.fd, node->bufsend, sendlen);

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;

    /*
     * https://stackoverflow.com/questions/8594591/why-does-pthread-cond-wait-have-spurious-wakeups
     */
    pthread_mutex_lock(&node->lock);
    while (!m_remotedone) {
        rv = pthread_cond_timedwait(&node->cond, &node->lock, &timeout);
        if (rv == ETIMEDOUT) {
            pthread_mutex_unlock(&node->lock);

            TINY_LOG("trigger sync timeout");
            return NULL;
        }
    }
    pthread_mutex_unlock(&node->lock);

    if (rv != 0) {
        TINY_LOG("trigger sync nok %d %d %s", m_remotedone, rv, strerror(errno));
        return NULL;
    } else return m_recvbuf;
}

char* msourceDirectoryInfo(char *id, char *path)
{
    int rv = 0;

    if (!id || !path) return NULL;

    /* skip ahead '/' */
    while (*path == '/') path++;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return NULL;

    CtlNode *node = &item->contrl;

    m_remotedone = false;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "directory", path);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_UDISK_INFO, datanode);
    packet->seqnum = SEQ_SYNC_REQ;
    packetCRCFill(packet);
    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;

    pthread_mutex_lock(&node->lock);
    while (!m_remotedone) {
        rv = pthread_cond_timedwait(&node->cond, &node->lock, &timeout);
        if (rv == ETIMEDOUT) {
            pthread_mutex_unlock(&node->lock);

            TINY_LOG("trigger sync timeout");
            return NULL;
        }
    }
    pthread_mutex_unlock(&node->lock);

    if (rv != 0) {
        TINY_LOG("trigger sync nok %d %d %s", m_remotedone, rv, strerror(errno));
        return NULL;
    } else return m_recvbuf;
}

char* msourceLibraryCreate(char *id, char *libname)
{
    int rv = 0;

    if (!id || !libname) return "参数错误";

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return "音源离线";

    CtlNode *node = &item->contrl;

    m_remotedone = false;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "name", libname);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_STORE_CREATE, datanode);
    packet->seqnum = SEQ_SYNC_REQ;
    packetCRCFill(packet);
    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;

    pthread_mutex_lock(&node->lock);
    while (!m_remotedone) {
        rv = pthread_cond_timedwait(&node->cond, &node->lock, &timeout);
        if (rv == ETIMEDOUT) {
            pthread_mutex_unlock(&node->lock);

            TINY_LOG("trigger sync timeout");
            return "音源无响应";
        }
    }
    pthread_mutex_unlock(&node->lock);

    if (rv != 0) {
        TINY_LOG("trigger sync nok %d %d %s", m_remotedone, rv, strerror(errno));
        return "内部错误";
    } else if (!m_remoteok) {
        return m_recvbuf;
    } else return NULL;
}

char* msourceLibraryRename(char *id, char *nameold, char *namenew)
{
    int rv = 0;

    if (!id || !nameold || !namenew) return "参数错误";

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return "音源离线";

    CtlNode *node = &item->contrl;

    m_remotedone = false;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "from", nameold);
    mdf_set_value(datanode, "to", namenew);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_STORE_RENAME, datanode);
    packet->seqnum = SEQ_SYNC_REQ;
    packetCRCFill(packet);
    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;

    pthread_mutex_lock(&node->lock);
    while (!m_remotedone) {
        rv = pthread_cond_timedwait(&node->cond, &node->lock, &timeout);
        if (rv == ETIMEDOUT) {
            pthread_mutex_unlock(&node->lock);

            TINY_LOG("trigger sync timeout");
            return "音源无响应";
        }
    }
    pthread_mutex_unlock(&node->lock);

    if (rv != 0) {
        TINY_LOG("trigger sync nok %d %d %s", m_remotedone, rv, strerror(errno));
        return "内部错误";
    } else if (!m_remoteok) {
        return m_recvbuf;
    } else return NULL;
}

char* msourceLibrarySetDefault(char *id, char *libname)
{
    int rv = 0;

    if (!id || !libname) return "参数错误";

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return "音源离线";

    CtlNode *node = &item->contrl;

    m_remotedone = false;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "name", libname);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_STORE_SET_DEFAULT, datanode);
    packet->seqnum = SEQ_SYNC_REQ;
    packetCRCFill(packet);
    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;

    pthread_mutex_lock(&node->lock);
    while (!m_remotedone) {
        rv = pthread_cond_timedwait(&node->cond, &node->lock, &timeout);
        if (rv == ETIMEDOUT) {
            pthread_mutex_unlock(&node->lock);

            TINY_LOG("trigger sync timeout");
            return "音源无响应";
        }
    }
    pthread_mutex_unlock(&node->lock);

    if (rv != 0) {
        TINY_LOG("trigger sync nok %d %d %s", m_remotedone, rv, strerror(errno));
        return "内部错误";
    } else if (!m_remoteok) {
        return m_recvbuf;
    } else return NULL;
}

char* msourceLibraryDelete(char *id, char *storename, bool force)
{
    int rv = 0;

    if (!id || !storename) return "参数错误";

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return "音源离线";

    CtlNode *node = &item->contrl;

    m_remotedone = false;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "name", storename);
    mdf_set_bool_value(datanode, "force", force);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_STORE_DELETE, datanode);
    packet->seqnum = SEQ_SYNC_REQ;
    packetCRCFill(packet);
    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;

    pthread_mutex_lock(&node->lock);
    while (!m_remotedone) {
        rv = pthread_cond_timedwait(&node->cond, &node->lock, &timeout);
        if (rv == ETIMEDOUT) {
            pthread_mutex_unlock(&node->lock);

            TINY_LOG("trigger sync timeout");
            return "音源无响应";
        }
    }
    pthread_mutex_unlock(&node->lock);

    if (rv != 0) {
        TINY_LOG("trigger sync nok %d %d %s", m_remotedone, rv, strerror(errno));
        return "内部错误";
    } else if (!m_remoteok) {
        return m_recvbuf;
    } else return NULL;
}


char* msourceLibraryMerge(char *id, char *libsrc, char *libdst)
{
    int rv = 0;

    if (!id || !libsrc || !libdst) return "参数错误";

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return "音源离线";

    CtlNode *node = &item->contrl;

    m_remotedone = false;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "src", libsrc);
    mdf_set_value(datanode, "dst", libdst);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_STORE_MERGE, datanode);
    packet->seqnum = SEQ_SYNC_REQ;
    packetCRCFill(packet);
    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;

    pthread_mutex_lock(&node->lock);
    while (!m_remotedone) {
        rv = pthread_cond_timedwait(&node->cond, &node->lock, &timeout);
        if (rv == ETIMEDOUT) {
            pthread_mutex_unlock(&node->lock);

            TINY_LOG("trigger sync timeout");
            return "音源无响应";
        }
    }
    pthread_mutex_unlock(&node->lock);

    if (rv != 0) {
        TINY_LOG("trigger sync nok %d %d %s", m_remotedone, rv, strerror(errno));
        return "内部错误";
    } else if (!m_remoteok) {
        return m_recvbuf;
    } else return NULL;
}

char* msourceSetAutoPlay(char *id, bool autoplay)
{
    int rv = 0;

    if (!id) return "参数错误";

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return "音源离线";

    CtlNode *node = &item->contrl;

    m_remotedone = false;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_bool_value(datanode, "autoPlay", autoplay);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_SET_AUTOPLAY, datanode);
    packet->seqnum = SEQ_SYNC_REQ;
    packetCRCFill(packet);
    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;

    pthread_mutex_lock(&node->lock);
    while (!m_remotedone) {
        rv = pthread_cond_timedwait(&node->cond, &node->lock, &timeout);
        if (rv == ETIMEDOUT) {
            pthread_mutex_unlock(&node->lock);

            TINY_LOG("trigger sync timeout");
            return "音源无响应";
        }
    }
    pthread_mutex_unlock(&node->lock);

    if (rv != 0) {
        TINY_LOG("trigger sync nok %d %d %s", m_remotedone, rv, strerror(errno));
        return "内部错误";
    } else if (!m_remoteok) {
        return m_recvbuf;
    } else return NULL;
}

bool msourceMediaCopy(char *id, char *path, char *libname, bool recursive)
{
    if (!id || !path || !libname) return false;

    while (*path == '/') path++;

    MsourceNode *item = _source_find(m_sources, id);
    if (!item) return false;

    CtlNode *node = &item->contrl;

    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "path", path);
    mdf_set_value(datanode, "name", libname);
    mdf_set_bool_value(datanode, "recursive", recursive);

    MessagePacket *packet = packetMessageInit(node->bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_UDISK_COPY, datanode);
    packetCRCFill(packet);

    SSEND(node->base.fd, node->bufsend, sendlen);

    mdf_destroy(&datanode);

    return true;
}


char* mnetDiscover2()
{
    sleep(3);
    //return "imdiscover";
    return NULL;
    //return mdf_get_value();
}


#ifdef EXECUTEABLE

static void _on_wifi_setted(NetNode *client, bool success, char *errmsg, char *response)
{
    TINY_LOG("wifi setted %s", success ? "OK" : errmsg);
}


static void _on_playing(NetNode *client, bool success, char *errmsg, char *response)
{
    TINY_LOG("on RESPONSE %d %s %s", success, errmsg, response);
}

int main(int argc, char *argv[])
{
    mnetStart("./avm/");

    char *id = mnetDiscovery();

    TINY_LOG("%s", id);
    //mnetWifiSet("a4204428f3063", "TPLINK_2323", "123123", "No.419", _on_wifi_setted);
    //char *msg = msourceHome("a4204428f3063");
    //TINY_LOG("xxxxxx %s", msg);

    sleep(5);
    //char *msg = msourceLibraryCreate("a4204428f3063", "测试媒体库2");
    //char *msg = msourceDirectoryInfo("a4204428f3063", "music/0701/");
    //if (msg) TINY_LOG("DIR %s", msg);
    //else TINY_LOG("list dir failure");

    //mnetPlayInfo("a4204428f3063", _on_playing);
    //mnetPlay("a4204428f3063");

    sleep(5);
    mnetStoreList("acb331f347a71");

    sleep(5);
    omusicStoreSelect("acb331f347a71", "默认媒体库");
    //omusicSyncStore("a4204428f3063", "国语精选");
    //omusicSyncStore("acb331f347a71", "默认媒体库");

    sleep(5);
    //mnetStoreSync("acb331f347a71", "默认媒体库");
    char *msg = omusicLibraryID("acb331f347a71");
    //char *msg = "";
    //char *msg = omusicHome("a4204428f3063");
    //char *tok;
    //while ((tok = strstr(msg, "张学友,汤宝如")) != NULL) {
        //msg = tok + strlen("张韶涵");
   //     msg = tok + strlen("张学友,汤宝如");
   //     TINY_LOG("token in");
    //}
    TINY_LOG("home: %s", msg);
    //TINY_LOG("home: %s", omusicArtist("a4204428f3063", "U2"));
    //TINY_LOG("artist: %s", omusicAlbum("a4204428f3063", "U2", "Duals"));

#if 0
    int count = 0;
    while (count++ < 10) {
        sleep(25);
        mnetPause("a4204428f3063");
        //mnetPlayInfo("a4204428f3063", _on_playing);

        sleep(5);
        mnetResume("a4204428f3063");

        sleep(6);
        mnetPause("a4204428f3063");
        //mnetPlayInfo("a4204428f3063", _on_playing);

        sleep(5);
        mnetResume("a4204428f3063");

        sleep(5);
        mnetNext("a4204428f3063");
    }
#endif

    sleep(10000);

    callbackStop();

    return 0;
}
#endif
