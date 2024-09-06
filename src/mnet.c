#include "mnet.h"

char* mnet_discover()
{
    //memset(ip, 0x0, sizeof(ip));

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        TINY_LOG("create socket failure");
        return "NULL1";
    }

    int enable = 1;
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) != 0) {
        TINY_LOG("set reuseable error %s", strerror(errno));
        return "NULL2";
    }

    struct timeval tvsend = {0, 0}, tvrecv = {7, 0};
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&tvsend, sizeof(tvsend)) != 0) {
        TINY_LOG("set send timeout error %s", strerror(errno));
        return "NULL3";
    }

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&tvrecv, sizeof(tvrecv)) != 0) {
        TINY_LOG("set receive timeout error %s", strerror(errno));
        return "NULL4";
    }

    struct sockaddr_in ownsa, othersa;
    int otherlen = sizeof(othersa);
    ownsa.sin_family = AF_INET;
    ownsa.sin_addr.s_addr = INADDR_ANY;
    ownsa.sin_port = htons(3102);

    int rv = bind(fd, (const struct sockaddr*)&ownsa, sizeof(ownsa));
    if (rv != 0) {
        close(fd);
        TINY_LOG("bind failure %s", strerror(errno));
        return "NULL5";
    }

    uint8_t rcvbuf[256];
    rv = recvfrom(fd, rcvbuf, sizeof(rcvbuf), 0, (struct sockaddr*)&othersa, &otherlen);
    close(fd);
    if (rv > 0) {
        //char *ip = inet_ntop(othersa.sin_family, othersa->sin_addr, e)
        //printf("received %d bytes data\n", rv);
        TINY_LOG("received %d bytes data", rv);
        return "I am Fine";
    } else {
        TINY_LOG("receive error %d %s", rv, strerror(errno));
        return "Receive Error";
    }

    close(fd);

    return "rcvbuf";
}

char* mnet_discover2()
{
    sleep(3);

    return "HAHAHAHA";
}

#ifdef EXECUTEABLE
int main(int argc, char *argv[])
{
    char *id = mnet_discover();

    TINY_LOG("%s", id);
    return 0;
}
#endif
