#ifndef __OMUSIC_H__
#define __OMUSIC_H__

#include "pocket.h"
#include "domme.h"

typedef struct _omusic_node {
    char id[LEN_CPUID];

    char libroot[PATH_MAX-128];
    char *storename;

    MDF *dbnode;
    DommeStore *plan;

    struct _omusic_node *next;
} OmusicNode;

/* 首页初次加载或切换媒体库时调用，立即返回 */
void omusicStoreSelect(char *id, char *storename);
/* 数据库更新后调用 */
OmusicNode* omusicStoreClear(char *id);

/* 调用 omusicStoreSelect() 后调用，首次会花费稍长时间加载数据库 */
char* omusicStoreList(char *id);
char* omusicHome(char *id);
char* omusicArtist(char *id, char *name);
char* omusicAlbum(char *id, char *name, char*title);

char* omusicLocation(char *id, char *trackid);
char* omusicAlbumIDS(char *id, char *name, char *title);

/* 同步缓存专辑 */
bool omusicSyncAlbum(char *id, char *name, char *title);

#endif  /* __OMUSIC_H__ */
