#ifndef __MFILE_H__
#define __MFILE_H__

#include "pocket.h"
#include "domme.h"
#include "mnet.h"

typedef enum {
    SYNC_RAWFILE = 0,
    SYNC_TRACK_COVER,
    SYNC_ARTIST_COVER,
    SYNC_ALBUM_COVER,
} SYNC_TYPE;

typedef struct {
    SYNC_TYPE type;
    char *name;
    char *id;
    char *artist;
    char *album;
} SyncFile;

char* mfile_test(char *dirname);

/*
 * 根据 item->storepath 下的 music.db 文件，扫描本地已存在文件，建立待同步文件列表
 */
MLIST* mfileBuildSynclist(MsourceNode *item);
MLIST* mfileBuildSynclistTest(MsourceNode *item);

#endif  /* __MFILE_H__ */
