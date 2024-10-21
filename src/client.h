#ifndef __CLIENT_H__
#define __CLIENT_H__

#include "mnet.h"

void clientInit();
void clientRecv(int sfd, CtlNode *client);
void serverClosed(CtlNode *client);

#endif  /* __CLIENT_H__ */
