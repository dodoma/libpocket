#ifndef __CLIENT_H__
#define __CLIENT_H__

#include "mnet.h"

void clientInit();
void clientRecv(int sfd, NetNode *client);
void serverClosed(NetNode *client);

#endif  /* __CLIENT_H__ */
