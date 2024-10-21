#ifndef __BINARY_H__
#define __BINARY_H__

#include "mnet.h"

void binaryInit();
void binaryRecv(int sfd, BinNode *client);
void serverBinClosed(BinNode *client);

#endif  /* __BINARY_H__ */
