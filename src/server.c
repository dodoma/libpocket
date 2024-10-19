#include <reef.h>

#include "global.h"
#include "mnet.h"
#include "callback.h"
#include "packet.h"
#include "server.h"

bool serverConnect(NetNode *server)
{
#define RETURN(ret)                                         \
    do {                                                    \
        close(fd);                                          \
        return (ret);                                       \
    } while (0)

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        TINY_LOG("create socket failre");
        return false;
    }

    int enable = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    struct sockaddr_in othersa;
    struct in_addr ia;
    int rv = inet_pton(AF_INET, server->upnode->ip, &ia);
    if (rv <= 0) {
        TINY_LOG("pton failure %s", server->upnode->ip);
        RETURN(false);
    }
    othersa.sin_family = AF_INET;
    othersa.sin_addr.s_addr = ia.s_addr;
    othersa.sin_port = htons(server->port);
    rv = connect(fd, (struct sockaddr*)&othersa, sizeof(othersa));
    if (rv < 0) {
        TINY_LOG("connect failure %s %d", server->upnode->ip, server->port);
        RETURN(false);
    }

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

    server->fd = fd;
    server->pong = g_ctime;
    server->online = true;

    uint8_t sendbuf[256] = {0};
    size_t sendlen = packetPINGFill(sendbuf, sizeof(sendbuf));
    send(fd, sendbuf, sendlen, MSG_NOSIGNAL);
    //MSG_LOG("SEND: ", sendbuf, sendlen);

    return true;
#undef RETURN
}
