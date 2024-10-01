#include <reef.h>

#include "global.h"
#include "callback.h"
#include "client.h"
#include "packet.h"
#include "mnet.h"

static uint8_t *m_recvbuf = NULL;

static bool _parse_packet(NetNode *client, CommandPacket *packet)
{
    uint8_t *buf = packet->data;
    char *errmsg = NULL;

    switch (packet->frame_type) {
    case FRAME_ACK:
    {
        bool ok = *buf; buf++;

        if (packet->length > LEN_HEADER + 1 + 4) {
            int msglen = strlen((char*)buf);
            if (packet->length != LEN_HEADER + 1 + msglen + 1 + 4) {
                TINY_LOG("ack msg error %d %d", packet->length, msglen);
                errmsg = strdup((char*)buf);
            }
        }

        callbackOn(packet->seqnum, packet->command, ok, errmsg, NULL);

        if (errmsg) free(errmsg);

        break;
    }
    case FRAME_RESPONSE:
    {
        int msglen = 0;
        bool ok = *buf; buf++;

        if (*buf != 0) {
            msglen = strlen((char*)buf);
            errmsg = strdup((char*)buf);
            buf += msglen;
            buf++;
        }

        if (packet->length > LEN_HEADER + 1 + msglen + 1 + 4) {
            MDF *datanode;
            mdf_init(&datanode);
            if (mdf_mpack_deserialize(datanode, buf,
                                      packet->length - (LEN_HEADER + 1 + msglen + 1 + 4)) > 0) {
                char *response = mdf_json_export_string(datanode);

                callbackOn(packet->seqnum, packet->command, ok, errmsg, response);

                if (response) free(response);
            } else TINY_LOG("message pack deserialize failure");

            mdf_destroy(&datanode);
        } else {
            TINY_LOG("response msg error %d", packet->length);
        }

        if (errmsg) free(errmsg);

        break;
    }
    default:
        TINY_LOG("unsupport frame type %d", packet->frame_type);
        return false;
    }

    return true;
}

static bool _parse_recv(NetNode *client, uint8_t *recvbuf, size_t recvlen)
{
#define PARTLY_PACKET                                               \
    do {                                                            \
        if (!client->bufrecv) {                                     \
            client->bufrecv = mos_calloc(1, CONTRL_PACKET_MAX_LEN); \
            memcpy(client->bufrecv, recvbuf, recvlen);              \
        }                                                           \
        client->recvlen = recvlen;                                  \
        return true;                                                \
    } while (0)

    if (recvlen < LEN_IDIOT) PARTLY_PACKET;

    IdiotPacket *ipacket = packetIdiotGot(recvbuf, recvlen);
    if (ipacket) {
        switch (ipacket->idiot) {
        case IDIOT_PING:
            break;
        case IDIOT_PONG:
            //TINY_LOG("pong received");
            client->pong = g_ctime;
            break;
        default:
            TINY_LOG("unsupport idot packet %d", ipacket->idiot);
            break;
        }

        if (recvlen > LEN_IDIOT) {
            memmove(recvbuf, recvbuf + LEN_IDIOT, recvlen - LEN_IDIOT);
            return _parse_recv(client, recvbuf, recvlen - LEN_IDIOT);
        }
    } else {
        if (recvlen < LEN_HEADER + 1 + 4) PARTLY_PACKET;

        CommandPacket *packet = packetCommandGot(recvbuf, recvlen);
        if (packet->sof == PACKET_SOF && packet->idiot == 1) {
            if (recvlen < packet->length) {
                if (packet->length > CONTRL_PACKET_MAX_LEN) {
                    return false;
                }

                PARTLY_PACKET;
            } else {
                _parse_packet(client, packet);

                if (recvlen > packet->length) {
                    memmove(recvbuf, recvbuf + packet->length, recvlen - packet->length);
                    return _parse_recv(client, recvbuf, recvlen - packet->length);
                }
            }
        } else {
            TINY_LOG("packet error");
            return false;
        }
    }

    return true;

#undef PARTLY_PACKET
}

void clientInit()
{
    if (!m_recvbuf) m_recvbuf = calloc(1, CONTRL_PACKET_MAX_LEN);
}

void clientRecv(int sfd, NetNode *client)
{
    int rv;

    if (client->bufrecv == NULL) {
        memset(m_recvbuf, 0x0, CONTRL_PACKET_MAX_LEN);

        rv = recv(sfd, m_recvbuf, CONTRL_PACKET_MAX_LEN, 0);
        MSG_LOG("RECV: ", m_recvbuf, rv);

        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        } else if (rv <= 0) {
            serverClosed(client);
            return;
        }

        if (!_parse_recv(client, m_recvbuf, rv)) {
            TINY_LOG("packet error");
            return;
        }
    } else {
        rv = recv(sfd, client->bufrecv + client->recvlen, CONTRL_PACKET_MAX_LEN - client->recvlen, 0);
        MSG_LOG("CRECV: ", client->bufrecv + client->recvlen, rv);

        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        } else if (rv <= 0) {
            serverClosed(client);
            return;
        }

        if (!_parse_recv(client, client->bufrecv, client->recvlen)) {
            TINY_LOG("packet error");
            return;
        }
    }
}

void serverClosed(NetNode *client)
{
    if (!client) return;

    TINY_LOG("server closed on %d", client->fd);

    client->online = false;

    shutdown(client->fd, SHUT_RDWR);
    close(client->fd);
    client->fd = -1;

    callbackOn(SEQ_SERVER_CLOSED, 0, false, client->upnode->id, NULL);
}
