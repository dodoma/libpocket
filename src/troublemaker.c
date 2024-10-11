#include <reef.h>

#include "global.h"
#include "callback.h"
#include "mnet.h"
#include "packet.h"
#include "client.h"
#include "server.h"

uint8_t bufsend[LEN_PACKET_NORMAL];
size_t sendlen;

time_t g_ctime, g_starton, g_elapsed;

void test_1(int fd, uint8_t *randbuf, size_t randlen)
{
    int rv = send(fd, randbuf, randlen, 0);
    MSG_LOG("SEND: ", randbuf, rv);
}

void test_2(int fd, uint8_t *randbuf, size_t randlen)
{
    char buf[1] = {0xAA};

    int rv = send(fd, buf, 1, 0);
    MSG_LOG("SEND: ", buf, 1);

    sleep(1);
    buf[0] = 0x65;
    rv = send(fd, buf, 1, 0);
    MSG_LOG("SEND: ", buf, 1);

    sleep(1);
    buf[0] = 0xAA;
    rv = send(fd, buf, 1, 0);
    MSG_LOG("SEND: ", buf, 1);

    sleep(1);
    buf[0] = 0x69;
    rv = send(fd, buf, 1, 0);
    MSG_LOG("SEND: ", buf, 1);

    sleep(1);
    buf[0] = 0xAA;
    rv = send(fd, buf, 1, 0);
    MSG_LOG("SEND: ", buf, 1);

    sleep(1);
    buf[0] = 0x62;
    rv = send(fd, buf, 1, 0);
    MSG_LOG("SEND: ", buf, 1);

    sleep(1);
    rv = send(fd, randbuf, randlen, 0);
    MSG_LOG("SEND: ", randbuf, rv);
}

void test_3(int fd, uint8_t *bufrand, size_t randlen)
{
    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "ap", "ODIVIAWEIVIDKAFLLVLX89213429d8fa0sdf)A*D&F)*Q#&$");
    mdf_set_value(datanode, "passwd", "DSFPSDPVS*(234jskdfj;asdfpsidjfp983u24wjdifpaoisdufpas98df90s87aspdfu34r2D*#$Y*(_!)+@#sdfaopfi3rpfQ#IPO@IF");
    mdf_set_value(datanode, "name", "我是个测试音源");

    MessagePacket *packet = packetMessageInit(bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_WIFI_SET, datanode);
    if (sendlen == 0) {
        TINY_LOG("message too loooong");
        mdf_destroy(&datanode);
        return;
    }
    packetCRCFill(packet);

    int rv = send(fd, bufsend, sendlen, 0);
    MSG_LOG("SEND: ", bufsend, rv);

    mdf_destroy(&datanode);
}

void test_4(int fd, uint8_t *bufrand, size_t randlen)
{
    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "ap", "ODIVIAWEIVIDKAFLLVLX89213429d8fa0sdf)A*D&F)*Q#&$");
    mdf_set_value(datanode, "passwd", "DSFPSDPVS*(234jskdfj;asdfpsidjfp983u24wjdifpaoisdufpas98df90s87aspdfu34r2D*#$Y*(_!)+@#sdfaopfi3rpfQ#IPO@IF");
    mdf_set_value(datanode, "name", "我是个测试音源");

    MessagePacket *packet = packetMessageInit(bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_WIFI_SET, datanode);
    if (sendlen == 0) {
        TINY_LOG("message too loooong");
        mdf_destroy(&datanode);
        return;
    }
    packetCRCFill(packet);

    int rv = send(fd, bufsend, 100, 0);
    MSG_LOG("SEND: ", bufsend, rv);

    sleep(2);
    rv = send(fd, bufsend + 100, 50, 0);
    MSG_LOG("SEND: ", bufsend + 100, rv);

    sleep(3);
    rv = send(fd, bufsend + 150, sendlen - 150, 0);
    MSG_LOG("SEND: ", bufsend + 150, rv);

    mdf_destroy(&datanode);
}

void test_5(int fd, uint8_t *bufrand, size_t randlen)
{
    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "ap", "sdf21348wfu234S(DRFTQW#$%IJIOFWUD)F(#U@//m,d/8fa0sdf)A*D&F)*Q#&$");
    mdf_set_value(datanode, "passwd", "XZZZZZZZZZZZZZZZZZZZZZZXC*#$Y*(_FDSFSDFSDFSDFSDFSDFSDFSDFSDFSDFSD!)+@#sdfaopfi3rpfQ#IPO@IF");
    mdf_set_value(datanode, "name", "我是个测试音源2");

    MessagePacket *packet = packetMessageInit(bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_WIFI_SET, datanode);
    if (sendlen == 0) {
        TINY_LOG("message too loooong");
        mdf_destroy(&datanode);
        return;
    }
    packetCRCFill(packet);

    for (int i = 0; i < 100; i++) {
        int rv = send(fd, bufsend, sendlen, 0);
        MSG_LOG("SEND: ", bufsend, rv);
    }

    mdf_destroy(&datanode);
}

void test_6(int fd, uint8_t *bufrand, size_t randlen)
{
    MDF *datanode;
    mdf_init(&datanode);
    mdf_set_value(datanode, "ap", "sdf21348wfu234S(DRFTQW#$%IJIOFWUD)F(#U@//m,d/8fa0sdf)A*D&F)*Q#&$");
    mdf_set_value(datanode, "passwd", "XZZZZZZZZZZZZZZZZZZZZZZXC*#$Y*(_FDSFSDFSDFSDFSDFSDFSDFSDFSDFSDFSD!)+@#sdfaopfi3rpfQ#IPO@IF");
    mdf_set_value(datanode, "name", "我是个测试音源2");

    MessagePacket *packet = packetMessageInit(bufsend, LEN_PACKET_NORMAL);
    size_t sendlen = packetDataFill(packet, FRAME_HARDWARE, CMD_WIFI_SET, datanode);
    if (sendlen == 0) {
        TINY_LOG("message too loooong");
        mdf_destroy(&datanode);
        return;
    }
    packetCRCFill(packet);

    int rv = send(fd, bufsend, 100, 0);
    MSG_LOG("SEND: ", bufsend, rv);

    sleep(5);
    rv = send(fd, bufsend + 100, 50, 0);
    MSG_LOG("SEND: ", bufsend + 100, rv);

    sleep(3);

    uint8_t *mixbuf = calloc(1, CONTRL_PACKET_MAX_LEN), *bufptr = mixbuf;
    memcpy(mixbuf, bufsend + 150, sendlen - 150);
    bufptr += sendlen - 150;

    //rv = send(fd, bufsend + 150, sendlen - 150, 0);
    //MSG_LOG("SEND: ", bufsend + 150, rv);

    for (int i = 0; i < 100; i++) {
        //int rv = send(fd, bufsend, sendlen, 0);
        //MSG_LOG("SEND: ", bufsend, rv);
        memcpy(bufptr, bufsend, sendlen);
        bufptr += sendlen;
    }

    rv = send(fd, mixbuf, bufptr - mixbuf, 0);
    MSG_LOG("SEND: ", mixbuf, rv);

    free(mixbuf);

    mdf_destroy(&datanode);
}

int main(int argc, char *argv[])
{
    g_ctime = g_starton = g_elapsed = 0 ;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        TINY_LOG("create socket failre");
        return 1;
    }

    int enable = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    char *ip = "192.168.8.66";
    int port = 4001;
    struct sockaddr_in othersa;
    othersa.sin_family = AF_INET;
    othersa.sin_addr.s_addr = inet_addr(ip);
    othersa.sin_port = htons(port);
    int rv = connect(fd, (struct sockaddr*)&othersa, sizeof(othersa));
    if (rv < 0) {
        TINY_LOG("connect failure %s %d", ip, port);
        close(fd);
        return 1;
    }

    //fcntl(nitem->fd, F_SETFL, fcntl(nitem->fd, F_GETFL) & ~O_NONBLOCK);
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));

    char randstr[128];
    uint8_t randbuf[64];
    size_t randlen = sizeof(randbuf);
    mstr_rand_hexstring_fixlen(randstr, sizeof(randstr));
    mstr_hexstr2bin(randstr, sizeof(randstr), randbuf);

    memset(bufsend, 0x0, sizeof(bufsend));
    //test_1(fd, randbuf, randlen);

    memset(bufsend, 0x0, sizeof(bufsend));
    //test_2(fd, randbuf, randlen);

    memset(bufsend, 0x0, sizeof(bufsend));
    //test_3(fd, randbuf, randlen);

    memset(bufsend, 0x0, sizeof(bufsend));
    //test_4(fd, randbuf, randlen);

    memset(bufsend, 0x0, sizeof(bufsend));
    test_6(fd, randbuf, randlen);

    sleep(10);
    memset(bufsend, 0x0, sizeof(bufsend));
    test_6(fd, randbuf, randlen);

    sleep(10);

    close(fd);

    return 0;
}
