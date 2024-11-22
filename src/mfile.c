#include <reef.h>

#include "global.h"
#include "timer.h"
#include "pocket.h"
#include "mfile.h"

char* mfile_test(char *dirname)
{
    static char line[256] = {0};
    static int count = 101;
    char filename[PATH_MAX];

    if (!dirname) return "unknown path";

    snprintf(filename, sizeof(filename), "%s/avmISAVM.txt", dirname);

    TINY_LOG("xxxx %s", filename);

#if 1
    FILE *fp = fopen(filename, "w");
    if (fp) {
        fprintf(fp, "xxxyyyzzz %d\n", count++);
        fclose(fp);
    } else TINY_LOG("open file for write failure %s %s", filename, strerror(errno));
#endif

    fp = fopen(filename, "r");
    if (fp) {
        if (!fgets(line, sizeof(line), fp)) TINY_LOG("read failure %s", strerror(errno));
    } else TINY_LOG("open file for read failure %s %s", filename, strerror(errno));

    if (rename(filename, "/data/user/0/com.example.libmoc_example/app_flutter/avmISAVM232.txt") != 0)
        TINY_LOG("link %s failure %s", filename, strerror(errno));
    else TINY_LOG("link ok");

    return line;
}

static SyncFile* _syncNew(char *name, char *id, char *artist, char *album, SYNC_TYPE type)
{
    SyncFile *sfile = mos_calloc(1, sizeof(SyncFile));
    memset(sfile, 0x0, sizeof(SyncFile));

    if (name)   sfile->name   = strdup(name);
    if (id)     sfile->id     = strdup(id);
    if (artist) sfile->artist = strdup(artist);
    if (album)  sfile->album  = strdup(album);

    sfile->type = type;

    return sfile;
}

static void _syncFree(void *p)
{
    if (!p) return;

    SyncFile *sfile = (SyncFile*)p;
    mos_free(sfile->name);
    mos_free(sfile->id);
    mos_free(sfile->artist);
    mos_free(sfile->album);
    mos_free(sfile);
}

MLIST* mfileBuildSynclistTest(MsourceNode *item)
{
    MLIST *synclist;
    mlist_init(&synclist, _syncFree);

    /*
     * 3f7ad268dd 图片比文件本身大
     * 8193c2922f 图片显示不出(名称后缀问题)
     * 0546d4d57e 正常显示
     */
    mlist_append(synclist, _syncNew(NULL, "8193c2922f", NULL, NULL, SYNC_TRACK_COVER));

    return synclist;
}

MLIST* mfileBuildSynclist(MsourceNode *item)
{
    DommeStore *plan = item->plan;
    char filename[PATH_MAX];
    struct stat fs;

    if (!item || !item->plan) return NULL;

    MLIST *synclist;
    mlist_init(&synclist, _syncFree);

    /*
     * 1. 搞定封面
     */
    char *key;
    DommeFile *dfile;
    MHASH_ITERATE(plan->mfiles, key, dfile) {
        snprintf(filename, sizeof(filename), "%s%s/assets/cover/%s",
                 mnetAppDir(), item->id, dfile->id);
        if (stat(filename, &fs) != 0)
            mlist_append(synclist, _syncNew(NULL, dfile->id, NULL, NULL, SYNC_TRACK_COVER));
    }

    DommeArtist *artist;
    MLIST_ITERATE(plan->artists, artist) {
        snprintf(filename, sizeof(filename), "%s%s/assets/cover/%s",
                 mnetAppDir(), item->id, artist->name);
        if (stat(filename, &fs) != 0)
            mlist_append(synclist, _syncNew(NULL, NULL, artist->name, NULL, SYNC_ARTIST_COVER));

        DommeAlbum *disk;
        MLIST_ITERATEB(artist->albums, disk) {
            snprintf(filename, sizeof(filename), "%s%s/assets/cover/%s_%s",
                     mnetAppDir(), item->id, artist->name, disk->title);
            if (stat(filename, &fs) != 0)
                mlist_append(synclist,
                             _syncNew(NULL, NULL, artist->name, disk->title, SYNC_ALBUM_COVER));
        }
    }

    /*
     * 2. 搞定媒体文件
     */
    snprintf(filename, sizeof(filename), "%s%s/setting/needToSync", mnetAppDir(), item->id);
    MLIST *needToSync = mlist_build_from_textfile(filename, 128);
    char *id;
    MLIST_ITERATE(needToSync, id) {
        /* 拉取已选媒体库下媒体文件，此处忽略 STOREMARK 指令 */
        if (!memcmp(id, "STOREMARK", 9)) continue;

        dfile = dommeGetFile(plan, id);
        if (dfile) {
            char tok[512];
            snprintf(tok, sizeof(tok), "%s%s", dfile->dir, dfile->name);
            snprintf(filename, sizeof(filename), "%s%s/%s%s%s",
                     mnetAppDir(), item->id, plan->basedir, dfile->dir, dfile->name);
            if (stat(filename, &fs) != 0)
                mlist_append(synclist, _syncNew(tok, NULL, NULL, NULL, SYNC_STORE_FILE));
        }
    }
    mlist_destroy(&needToSync);

    /*
     * 3. 定时善后
     */
    g_timers = timerAdd(g_timers, 60, false, item, mnetNTSCheck);

    return synclist;
}
