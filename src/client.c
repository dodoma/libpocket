#include <reef.h>

#include "global.h"
#include "mnet.h"
#include "callback.h"
#include "client.h"
#include "packet.h"

static uint8_t *m_recvbuf = NULL;

static bool _parse_packet(CtlNode *client, MessagePacket *packet)
{
    uint8_t *buf = packet->data;
    char *errmsg = NULL;

    MsourceNode *source = client->base.upnode;

    switch (packet->frame_type) {
    case FRAME_CMD:
    {
        /*
         * 0 1 2 3 4 5 6 7 8
         * /---------------\
         * n   clientid    n
         * |      \0       |
         * \---------------/
         */
        if (packet->command == CMD_CONNECT) {
            /* 往 binary fd 发送 connect 命令，绑定至 contrl fd */
            uint8_t *buf = packet->data;
            int idlen = strlen((char*)buf);
            strncpy(source->myid, (char*)buf, idlen > LEN_CLIENTID ? LEN_CLIENTID: idlen);
            buf += idlen;
            buf++;              /* '\0' */

            char bufsend[LEN_PACKET_NORMAL] = {0};

            MessagePacket *outpacket = packetMessageInit(bufsend, LEN_PACKET_NORMAL);
            size_t sendlen = packetConnectFill(outpacket, source->myid);
            packetCRCFill(outpacket);

            send(source->binary.base.fd, bufsend, sendlen, MSG_NOSIGNAL);
        }
        break;
    }
    /*
     * 0 1 2 3 4 5 6 7 8
     * /---------------\
     * |   success     |
     * ...  errmsg   ...
     * |      \0       |
     * \---------------/
     */
    case FRAME_ACK:
    {
        bool ok = *buf; buf++;

        if (packet->length > LEN_HEADER + 1 + 4) {
            int msglen = strlen((char*)buf);
            if (packet->length != LEN_HEADER + 1 + msglen + 1 + 4) {
                TINY_LOG("ack msg error %d %d", packet->length, msglen);
            } else errmsg = strdup((char*)buf);
        }

        callbackOn((NetNode*)client, packet->seqnum, packet->command, ok, errmsg, NULL);
        break;
    }
    /*
     * 0 1 2 3 4 5 6 7 8
     * /---------------\
     * |   success     |
     * ...  errmsg   ...
     * |      \0       |
     * ... message pack ...
     * \---------------/
     */
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
        buf++;

        MDF *datanode;
        mdf_init(&datanode);
        if (packet->length > LEN_HEADER + 1 + msglen + 1 + 4) {
            if (mdf_mpack_deserialize(datanode, buf,
                                      packet->length - (LEN_HEADER + 1 + msglen + 1 + 4)) <= 0) {
                TINY_LOG("message pack deserialize failure");
                if (errmsg) free(errmsg);
                break;
            }
        }

        char *response = mdf_json_export_string(datanode);
        mdf_destroy(&datanode);

        callbackOn((NetNode*)client, packet->seqnum, packet->command, ok, errmsg, response);
        break;
    }
    default:
        TINY_LOG("unsupport frame type %d", packet->frame_type);
        return false;
    }

    return true;
}

static bool _parse_recv(CtlNode *client, uint8_t *recvbuf, size_t recvlen)
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
            client->base.pong = g_ctime;
            break;
        case IDIOT_PLAY_STEP:
            callbackOn((NetNode*)client, SEQ_PLAY_STEP, 0, true, NULL, NULL);
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

        MessagePacket *packet = packetMessageGot(recvbuf, recvlen);
        if (packet && packet->sof == PACKET_SOF && packet->idiot == 1) {
            if (recvlen < packet->length) {
                if (packet->length > CONTRL_PACKET_MAX_LEN) {
                    return false;
                }

                PARTLY_PACKET;
            } else {
                _parse_packet(client, packet);

                if (recvlen > packet->length) {
                    size_t exceed = recvlen - packet->length;
                    memmove(recvbuf, recvbuf + packet->length, exceed);
                    return _parse_recv(client, recvbuf, exceed);
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

void clientRecv(int sfd, CtlNode *client)
{
    int rv;

    if (client->bufrecv == NULL) {
        memset(m_recvbuf, 0x0, CONTRL_PACKET_MAX_LEN);

        rv = recv(sfd, m_recvbuf, CONTRL_PACKET_MAX_LEN, 0);
        MSG_LOG(g_dumprecv, "RECV: ", m_recvbuf, rv);

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
        MSG_LOG(g_dumprecv, "CRECV: ", client->bufrecv + client->recvlen, rv);

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

void serverClosed(CtlNode *client)
{
    if (!client) return;

    TINY_LOG("server closed on %d", client->base.fd);

    client->base.online = false;

    shutdown(client->base.fd, SHUT_RDWR);
    close(client->base.fd);
    client->base.fd = -1;

    callbackOn((NetNode*)client, SEQ_SERVER_CLOSED, 0, false, strdup(client->base.upnode->id), NULL);
}
