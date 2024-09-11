#include "global.h"
#include "mnet.h"
#include "packet.h"
#include "client.h"

#define HEARTBEAT_PERIOD 5
#define HEARTBEAT_TIMEOUT 15

time_t g_ctime, g_starton, g_elapsed;

pthread_t m_worker;
msourceNode *m_sources = NULL;

/*
 * 争取做到 moc server 与 音源 一套心跳维护逻辑
 */
static bool _keep_heartbeat(void *data)
{
    msourceNode *item = (msourceNode*)data;

    TINY_LOG("keep heartbeat");

    uint8_t sendbuf[256] = {0};
    size_t sendlen = packetPINGFill(sendbuf, sizeof(sendbuf));

    if (item->contrl.dropped) {
        TINY_LOG("connection lost on recv");
    }
    if (item->binary.dropped) {
        TINY_LOG("connection lost on recv");
    }

    if (g_ctime > item->contrl.pong && g_ctime - item->contrl.pong > HEARTBEAT_TIMEOUT) {
        TINY_LOG("connection lost on timeout %d", item->contrl.fd);
        /* TODO callback */
    } else {
        send(item->contrl.fd, sendbuf, sendlen, MSG_NOSIGNAL);
    }

    if (g_ctime > item->binary.pong && g_ctime - item->binary.pong > HEARTBEAT_TIMEOUT) {
        TINY_LOG("connection lost on timeout %d", item->binary.fd);
        /* TODO callback */
    } else {
        send(item->binary.fd, sendbuf, sendlen, MSG_NOSIGNAL);
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

        if (g_elapsed % t->timeout == 0 && !t->pause) {
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
    fd_set readset;
    int maxfd, rv;

    TINY_LOG("start event loop routine");

    while (true) {
        maxfd = 0;
        FD_ZERO(&readset);

        msourceNode *item = m_sources;
        while (item) {
            if (item->contrl.fd > maxfd) maxfd = item->contrl.fd;
            if (item->binary.fd > maxfd) maxfd = item->binary.fd;
            if (!item->contrl.dropped && item->contrl.fd > 0) FD_SET(item->contrl.fd, &readset);
            if (!item->binary.dropped && item->binary.fd > 0) FD_SET(item->binary.fd, &readset);

            item = item->next;
        }

        struct timeval tv = {.tv_sec = 0, .tv_usec = 1000000};
        rv = select(maxfd + 1, &readset, NULL, NULL, &tv);
        //TINY_LOG("select return %d", rv);

        item = m_sources;
        while (item) {
            if (item->pos == MNET_ONLINE_TIMER) {
                if (FD_ISSET(item->contrl.fd, &readset)) _timer_handler(item->contrl.fd);
            } else if (item->pos == MNET_ONLINE_LAN) {
                if (!item->contrl.dropped && FD_ISSET(item->contrl.fd, &readset)) {
                    clientRecv(item->contrl.fd, &item->contrl);
                }
                if (!item->binary.dropped && FD_ISSET(item->binary.fd, &readset)) {
                    //clientRecv(item->bianry.fd, &item->binary);
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
    msourceNode *item = calloc(1, sizeof(msourceNode));
    memset(item, 0x0, sizeof(msourceNode));
    item->ip = NULL;
    item->contrl.fd = -1;
    item->binary.fd = -1;
    item->pos = MNET_ONLINE_TIMER;

    item->contrl.fd = timerfd_create(CLOCK_REALTIME, 0);
    if (item->contrl.fd == -1) {
        TINY_LOG("create timer failure");
        RETURN(false);
    }

    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) == -1) {
        TINY_LOG("get time failure");
        RETURN(false);
    }
    struct itimerspec new_value;
    new_value.it_value.tv_sec = now.tv_sec + 1;
    new_value.it_value.tv_nsec = now.tv_nsec;
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_nsec = 100000000ul;
    if (timerfd_settime(item->contrl.fd, TFD_TIMER_ABSTIME, &new_value, NULL) == -1) {
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
        if (item->contrl.fd > 0) close(item->contrl.fd);    \
        if (item->binary.fd > 0) close(item->binary.fd);    \
        free(item);                                         \
        return (ret);                                       \
    } while (0)

    msourceNode *item = calloc(1, sizeof(msourceNode));
    memset(item, 0x0, sizeof(msourceNode));

    memcpy(item->id, cpuid, LEN_CPUID);
    item->ip = strdup(ip);
    item->contrl.fd = -1;
    item->contrl.port = port_contrl;
    item->contrl.pong = g_ctime;
    item->contrl.buf = NULL;
    item->contrl.dropped = false;
    item->contrl.complete = false;

    item->binary.fd = -1;
    item->binary.port = port_binary;
    item->binary.pong = g_ctime;
    item->binary.buf = NULL;
    item->binary.dropped = false;
    item->binary.complete = false;

    item->pos = MNET_ONLINE_LAN;

    uint8_t sendbuf[256] = {0};
    size_t sendlen = packetPINGFill(sendbuf, sizeof(sendbuf));

    /*
     * contrl
     */
    item->contrl.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (item->contrl.fd < 0) {
        TINY_LOG("create socket failre");
        RETURN(false);
    }

    int enable = 1;
    setsockopt(item->contrl.fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    struct sockaddr_in othersa;
    struct in_addr ia;
    int rv = inet_pton(AF_INET, ip, &ia);
    if (rv <= 0) {
        TINY_LOG("pton failure %s", ip);
        RETURN(false);
    }
    othersa.sin_family = AF_INET;
    othersa.sin_addr.s_addr = ia.s_addr;
    othersa.sin_port = htons(port_contrl);
    rv = connect(item->contrl.fd, (struct sockaddr*)&othersa, sizeof(othersa));
    if (rv < 0) {
        TINY_LOG("connect failure %s %d", ip, port_contrl);
        RETURN(false);
    }

    fcntl(item->contrl.fd, F_SETFL, fcntl(item->contrl.fd, F_GETFL) | O_NONBLOCK);

    send(item->contrl.fd, sendbuf, sendlen, MSG_NOSIGNAL);

    /*
     * binary
     */
    item->binary.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (item->binary.fd < 0) {
        TINY_LOG("create socket failre");
        RETURN(false);
    }

    setsockopt(item->binary.fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    othersa.sin_family = AF_INET;
    othersa.sin_addr.s_addr = ia.s_addr;
    othersa.sin_port = htons(port_binary);
    rv = connect(item->binary.fd, (struct sockaddr*)&othersa, sizeof(othersa));
    if (rv < 0) {
        TINY_LOG("connect failure %s %d", ip, port_binary);
        RETURN(false);
    }

    fcntl(item->binary.fd, F_SETFL, fcntl(item->binary.fd, F_GETFL) | O_NONBLOCK);

    send(item->binary.fd, sendbuf, sendlen, MSG_NOSIGNAL);

    g_timers = timerAdd(g_timers, HEARTBEAT_PERIOD, item, _keep_heartbeat);

#undef RETURN

    item->next = m_sources;
    m_sources = item;

    return true;
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
        if (packet->frame_type == FRAME_TYPE_MSG && packet->cmd_set == CMDSET_GENERAL) {
            if (packet->cmd_id == CMD_BROADCAST) {
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

/*
 * ============ business ============
 */
bool mnetWifiSet(char *id)
{
    if (!id) return false;

    msourceNode *item = NULL;
}


char* mnetDiscover2()
{
    sleep(3);
    //return "imdiscover";
    return NULL;
}


#ifdef EXECUTEABLE
int main(int argc, char *argv[])
{
    clientInit();
    mnetStart();

    char *id = mnetDiscovery();

    TINY_LOG("%s", id);

    sleep(100);

    return 0;
}
#endif
