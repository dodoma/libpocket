#ifndef __CLIENT_H__
#define __CLIENT_H__

#include "mnet.h"

void clientInit();
void clientRecv(int sfd, struct net_node *client);
void clientDrop(struct net_node *client);

#endif  /* __CLIENT_H__ */
