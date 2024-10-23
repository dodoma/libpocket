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

#if 0
    FILE *fp = fopen(filename, "w");
    if (fp) {
        fprintf(fp, "xxxyyyzzz %d\n", count++);
        fclose(fp);
    } else TINY_LOG("open file for write failure %s %s", filename, strerror(errno));
#endif

    FILE *fp = fopen(filename, "r");
    if (fp) {
        if (!fgets(line, sizeof(line), fp)) TINY_LOG("read failure %s", strerror(errno));
    } else TINY_LOG("open file for read failure %s %s", filename, strerror(errno));

    return line;
}

/*
 * 用户通过专辑或者艺术及勾选需要同步的音频文件后，id列表保存至了 setting/needToSync 中
 * 无论是切换媒体库时同步，还是设置后触发的同步，就只管同步
 * 该函数用于检查同步是否完成，完成则清空 needToSync，否则删掉文件中已完成同步的id
 */
static bool _check_needToSync(void *arg)
{
    MsourceNode *item = (MsourceNode*)arg;
    DommeStore *plan = item->plan;
    char filename[PATH_MAX];
    struct stat fs;
    char *id;

    if (!plan) return false;

    MLIST_ITERATE(item->needToSync, id) {
        bool exist = false;
        DommeFile *dfile = dommeGetFile(plan, id);
        if (dfile) {
            snprintf(filename, sizeof(filename), "%s%s/%s%s%s",
                     mnetAppDir(), item->id, plan->basedir, dfile->dir, dfile->name);
            if (stat(filename, &fs) == 0) exist = true;
        }

        if (exist || !dfile) {
            mlist_delete(item->needToSync, _moon_i);
            _moon_i--;
        }
    }

    snprintf(filename, sizeof(filename), "%s%s/setting/needToSync", mnetAppDir(), item->id);
    if (mlist_length(item->needToSync) == 0) {
        TINY_LOG("All media file DONE. remove %s", filename);

        remove(filename);

        return false;
    } else {
        TINY_LOG("write %s with %d ids", filename, mlist_length(item->needToSync));
        mlist_write_textfile(item->needToSync, filename);

        return true;
    }
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
    char *id;
    MLIST_ITERATE(item->needToSync, id) {
        dfile = dommeGetFile(plan, id);
        if (dfile) {
            char tok[512];
            snprintf(tok, sizeof(tok), "%s%s", dfile->dir, dfile->name);
            snprintf(filename, sizeof(filename), "%s%s/%s%s%s",
                     mnetAppDir(), item->id, plan->basedir, dfile->dir, dfile->name);
            if (stat(filename, &fs) != 0)
                mlist_append(synclist, _syncNew(tok, NULL, NULL, NULL, SYNC_RAWFILE));
        }
    }

    /*
     * 3. 定时善后
     */
    g_timers = timerAdd(g_timers, 60, false, item, _check_needToSync);

    return synclist;
}
