#include "global.h"
#include "client.h"
#include "packet.h"
#include "mnet.h"

static uint8_t *m_recvbuf = NULL;

static bool _parse_recv(struct net_node *client, uint8_t *rcvbuf, size_t rcvlen)
{
    if (rcvlen < LEN_IDIOT) {
        client->complete = false;
        return true;
    }

    IdiotPacket *ipacket = packetIdiotGot(rcvbuf, rcvlen);
    if (ipacket) {
        switch (ipacket->idiot) {
        case IDIOT_PING:
            break;
        case IDIOT_PONG:
            TINY_LOG("pong received");
            client->pong = g_ctime;
            break;
        }
    } else {
        CommandPacket *packet = packetCommandGot(rcvbuf, rcvlen);
        if (packet) {
            ;
        } else {
            ;
            /* ERROR */
        }
    }

    return true;
}

void clientInit()
{
    if (!m_recvbuf) m_recvbuf = calloc(1, CONTRL_PACKET_MAX_LEN);
}

void clientRecv(int sfd, struct net_node *client)
{
    int rv;

    if (client->buf == NULL) {
        memset(m_recvbuf, 0x0, CONTRL_PACKET_MAX_LEN);

        rv = recv(sfd, m_recvbuf, CONTRL_PACKET_MAX_LEN, 0);

        MSG_DUMP("RECV: ", m_recvbuf, rv);

        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        } else if (rv <= 0) {
            clientDrop(client);
            return;
        }

        if (!_parse_recv(client, m_recvbuf, rv)) {
            /* TODO drop client */
            TINY_LOG("packet error, return");
            return;
        }
    } else {
        ;
    }
}

void clientDrop(struct net_node *client)
{
    if (!client) return;

    TINY_LOG("drop client %d", client->fd);

    client->dropped = true;

    shutdown(client->fd, SHUT_RDWR);
    close(client->fd);
    client->fd = -1;
}
