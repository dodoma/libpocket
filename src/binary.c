#include <reef.h>

#include "global.h"
#include "mnet.h"
#include "callback.h"
#include "client.h"
#include "binary.h"
#include "packet.h"

static uint8_t *m_recvbuf = NULL;

static bool _parse_packet(BinNode *client, MessagePacket *packet)
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
         * n   filename    n
         * |      \0       |
         * 8   filesize    8
         * \---------------/
         */
        if (packet->command == CMD_SYNC) {
            /* 文件需要下载 */
            memset(client->tempname, 0x0, sizeof(client->tempname));
            memset(client->filename, 0x0, sizeof(client->filename));

            uint8_t *buf = packet->data;
            int slen = strlen((char*)buf);
            if (slen >= PATH_MAX) {
                TINY_LOG("filename too loooong");
                break;
            }

            snprintf(client->filename, sizeof(client->filename),
                     "%s%s/%s", mnetAppDir(), source->id, (char*)buf);
            buf += slen;
            buf++;              /* '\0' */

            client->binlen = *(uint64_t*)buf;
            buf += 8;

            TINY_LOG("SYNC %s with %lu bytes...", client->filename, client->binlen);

            remove(client->filename);

            snprintf(client->tempname, sizeof(client->tempname),
                     "%s%s/tmp/pocket.XXXXXX", mnetAppDir(), source->id);
            int fd = mkstemp(client->tempname);
            if (fd < 0) {
                TINY_LOG("unable to create file %s", client->tempname);
                break;
            }

            /* TODO record tempname <==> filename to 断网续传 */

            client->fpbin = fdopen(fd, "wb");
            if (!client->fpbin) {
                TINY_LOG("create file %s failure %s", client->tempname, strerror(errno));
                break;
            }
        }
        break;
    }
    default:
        TINY_LOG("unsupport frame type %d", packet->frame_type);
        return false;
    }

    return true;
}

static void _makesure_directory(char *filename)
{
    if (!filename) return;

    char *dirc = strdup(filename);
    char *dname = dirname(dirc);

    mos_mkdir(dname, 0755);

    free(dirc);
}

static bool _parse_recv(BinNode *client, uint8_t *recvbuf, size_t recvlen)
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

    /* 二进制文件内容 */
    while (client->fpbin && client->binlen > 0) {
        size_t writelen = 0;

        if (recvlen <= client->binlen) {
            /* 收到的内容仅够此次吃喝 */
            writelen = fwrite(recvbuf, 1, recvlen, client->fpbin);
            //TINY_LOG("%ju bytes write", writelen);
            if (writelen < recvlen) {
                TINY_LOG("write error %s", strerror(errno));
                fclose(client->fpbin);
                client->fpbin = NULL;
                client->binlen = 0;
                break;
            }
            client->binlen -= writelen;
            if (client->binlen == 0) {
                /* 文件传送完成 */
                TINY_LOG("SYNC done.");
                fclose(client->fpbin);
                client->fpbin = NULL;
                _makesure_directory(client->filename);
                if (rename(client->tempname, client->filename) != 0)
                    TINY_LOG("rename %s failure %s", client->filename, strerror(errno));
                //unlink(client->tempname);
            }
            return true;
        } else {
            /* 收到的内容比需要保存的文件内容多 */
            writelen = fwrite(recvbuf, 1, client->binlen, client->fpbin);
            TINY_LOG("%ju bytes write", writelen);
            if (writelen < client->binlen) {
                TINY_LOG("write error %s", strerror(errno));
                fclose(client->fpbin);
                client->fpbin = NULL;
                client->binlen = 0;
                break;
            }
            TINY_LOG("SYNC done.");
            fclose(client->fpbin);
            client->fpbin = NULL;
            _makesure_directory(client->filename);
            if (rename(client->tempname, client->filename) != 0)
                TINY_LOG("rename %s failure %s", client->filename, strerror(errno));
            //unlink(client->tempname);

            size_t exceed = recvlen - client->binlen;
            memmove(recvbuf, recvbuf + client->binlen, exceed);
            client->binlen = 0;
            return _parse_recv(client, recvbuf, exceed);
        }
    }

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

void binaryInit()
{
    if (!m_recvbuf) m_recvbuf = calloc(1, CONTRL_PACKET_MAX_LEN);
}

void binaryRecv(int sfd, BinNode *client)
{
    int rv;

    if (client->bufrecv == NULL) {
        memset(m_recvbuf, 0x0, CONTRL_PACKET_MAX_LEN);

        rv = recv(sfd, m_recvbuf, CONTRL_PACKET_MAX_LEN, 0);
        MSG_LOG(g_dumprecv, "RECV: ", m_recvbuf, rv);

        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        } else if (rv <= 0) {
            serverBinClosed(client);
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
            serverBinClosed(client);
            return;
        }

        if (!_parse_recv(client, client->bufrecv, client->recvlen)) {
            TINY_LOG("packet error");
            return;
        }
    }
}

void serverBinClosed(BinNode *client)
{
    if (!client) return;

    TINY_LOG("server closed on %d", client->base.fd);

    client->base.online = false;

    shutdown(client->base.fd, SHUT_RDWR);
    close(client->base.fd);
    client->base.fd = -1;

    callbackOn((NetNode*)client, SEQ_SERVER_CLOSED, 0, false, strdup(client->base.upnode->id), NULL);
}
