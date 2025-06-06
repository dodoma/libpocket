#include <reef.h>

#include "global.h"
#include "timer.h"
#include "pocket.h"
#include "mnet.h"
#include "omusic.h"

static OmusicNode *m_sources = NULL;

static int _store_compare(const void *anode, void *key)
{
    return strcmp(mdf_get_value((MDF*)anode, "name", ""), (char*)key);
}

static OmusicNode* _source_find(OmusicNode *nodes, char *id)
{
    OmusicNode *node = nodes;
    while (node) {
        if (!strcmp(node->id, id)) return node;

        node = node->next;
    }

    return NULL;
}

static bool _file_exist(char *filename)
{
    if (access(filename, F_OK) == 0) return true;
    else return false;
}

static bool _file_existf(const char *fmt, ...)
{
    if (!fmt) return false;

    char filename[PATH_MAX];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(filename, sizeof(filename), fmt, ap);
    va_end(ap);

    return _file_exist(filename);
}

static bool _disk_cached(OmusicNode *item, DommeAlbum *disk)
{
    if (!item || !item->plan || !disk) return false;

    DommeFile *dfile;
    MLIST_ITERATE(disk->tracks, dfile) {
        if (!_file_existf("%s%s/%s%s%s", mnetAppDir(), item->id,
                          item->plan->basedir, dfile->dir, dfile->name)) return false;
    }

    return true;
}

static int _album_cached_count(OmusicNode *item, DommeAlbum *disk)
{
    if (!disk || mlist_length(disk->tracks) == 0) return 0;

    int ccount = 0;

    DommeFile *dfile;
    MLIST_ITERATE(disk->tracks, dfile) {
        if (_file_existf("%s%s/%s%s%s", mnetAppDir(), item->id,
                         item->plan->basedir, dfile->dir, dfile->name))
            ccount++;
    }

    return ccount;
}

static int _artist_cached_count(OmusicNode *item, DommeArtist *artist)
{
    if (!artist || mlist_length(artist->albums) == 0) return 0;

    int ccount = 0;

    DommeAlbum *disk;
    MLIST_ITERATE(artist->albums, disk) {
        ccount += _album_cached_count(item, disk);
    }

    return ccount;
}

OmusicNode* omusicStoreClear(char *id)
{
    if (!id) return NULL;

    OmusicNode *item = _source_find(m_sources, id);
    if (item) {
        if (item->plan) dommeStoreFree(item->plan);
        mdf_destroy(&item->dbnode);

        item->dbnode = NULL;
        item->plan = NULL;

        return item;
    }

    return NULL;
}

void omusicStoreSelect(char *id, char *storename)
{
    if (!id || !storename) return;

    OmusicNode *item = omusicStoreClear(id);
    if (item) {
        mos_free(item->storename);
        item->storename = strdup(storename);
    } else {
        item = calloc(1, sizeof(OmusicNode));
        strncpy(item->id, id, LEN_CPUID);
        snprintf(item->libroot, sizeof(item->libroot), "%s%s/", mnetAppDir(), id);
        item->storename = strdup(storename);
        item->dbnode = NULL;
        item->plan = NULL;

        item->next = m_sources;
        m_sources = item;
    }
}

static OmusicNode* _makesure_load(char *id)
{
    char filename[PATH_MAX];
    MERR *err;

    if (!id || *id == '\0') return NULL;

    OmusicNode *item = _source_find(m_sources, id);
    if (item && item->storename) {
        if (!item->dbnode) {
            /* 先加载音源配置 */
            mdf_init(&item->dbnode);

            snprintf(filename, sizeof(filename), "%s%s/config.json", mnetAppDir(), id);
            while ((err = mdf_json_import_file(item->dbnode, filename)) != MERR_OK) {
                TINY_LOG("load %s failure %s, retry", filename, strerror(errno));
                merr_destroy(&err);
                sleep(1);
            }
        }

        if (!item->plan) {
            /* 再加载媒体数据库 */
            MDF *snode = mdf_search(item->dbnode, item->storename, _store_compare);
            if (!snode) {
                TINY_LOG("find store %s failure", item->storename);
                return NULL;
            }

            char *name = mdf_get_value(snode, "name", NULL);
            char *path = mdf_get_value(snode, "path", NULL);
            if (!name || !path) {
                TINY_LOG("config illgel");
                return NULL;
            }

            item->plan = dommeStoreCreate();
            item->plan->name = strdup(name);
            item->plan->basedir = strdup(path);

            snprintf(filename, sizeof(filename), "%s%smusic.db", item->libroot, path);
            while ((err = dommeLoadFromFile(filename, item->plan)) != MERR_OK) {
                TINY_LOG("load %s failure %s, retry", filename, strerror(errno));
                merr_destroy(&err);
                sleep(1);
            }
        }

        return item;
    } else {
        TINY_LOG("find %s failure, call omusicStoreSelect() first", id);
        return NULL;
    }
}

char* omusicStoreList(char *id)
{
    if (!id) return NULL;

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return NULL;
    }

    char *output = mdf_json_export_string(item->dbnode);

    return output;
}

char* omusicHome(char *id)
{
    if (!id) return NULL;

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return NULL;
    }

    MDF *outnode;
    mdf_init(&outnode);

    mdf_set_value(outnode, "deviceID", id);
    mdf_set_int_value(outnode, "countArtist", mlist_length(item->plan->artists));
    mdf_set_int_value(outnode, "countAlbum", item->plan->count_album);
    mdf_set_int_value(outnode, "countTrack", item->plan->count_track);
    mdf_set_bool_value(outnode, "localPlay", false); /* TODO xxx */

    MDF *anode = mdf_get_or_create_node(outnode, "artists");

    DommeArtist *artist;
    MLIST_ITERATE(item->plan->artists, artist) {
        MDF *cnode = mdf_insert_node(anode, NULL, -1);

        double indisk = 0.0;
        if (artist->count_track > 0)
            indisk = (float)_artist_cached_count(item, artist) / artist->count_track;

        mdf_set_value(cnode, "name", artist->name);
        mdf_set_valuef(cnode, "avt=%sassets/cover/%s", item->libroot, artist->name);
        mdf_set_double_value(cnode, "cachePercent", indisk);
    }

    mdf_object_2_array(anode, NULL);

    char *output = mdf_json_export_string(outnode);
    mdf_destroy(&outnode);

    return output;
}

char* omusicSearch(char *id, char *query)
{
    if (!id || !query) return NULL;

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return NULL;
    }

    MRE *reo = mre_init();
    MERR *err = mre_compile(reo, query);
    if (err != MERR_OK) {
        TINY_LOG("compile %s failure", query);
        merr_destroy(&err);
        return NULL;
    }

    MDF *outnode, *cnode = NULL;
    mdf_init(&outnode);

    DommeArtist *artist;
    MLIST_ITERATE(item->plan->artists, artist) {
        if (mre_match(reo, artist->name, true)) {
            /* 匹配艺术家 */
            cnode = mdf_insert_node(outnode, NULL, -1);
            mdf_set_int_value(cnode, "type", 0);
            mdf_set_value(cnode, "artist", artist->name);
            mdf_set_valuef(cnode, "cover=%sassets/cover/%s", item->libroot, artist->name);
        }

        DommeAlbum *disk;
        MLIST_ITERATEB(artist->albums, disk) {
            if (mre_match(reo, disk->title, true)) {
                /* 匹配专辑 */
                cnode = mdf_insert_node(outnode, NULL, -1);
                mdf_set_int_value(cnode, "type", 1);
                mdf_set_value(cnode, "artist", artist->name);
                mdf_set_value(cnode, "title", disk->title);
                mdf_set_valuef(cnode, "cover=%s/assets/cover/%s_%s",
                               item->libroot, artist->name, disk->title);
            }

            DommeFile *mfile;
            MLIST_ITERATEC(disk->tracks, mfile) {
                if (mre_match(reo, mfile->title, true)) {
                    /* 匹配标题 */
                    cnode = mdf_insert_node(outnode, NULL, -1);
                    mdf_set_int_value(cnode, "type", 2);
                    mdf_set_value(cnode, "trackid", mfile->id);
                    mdf_set_value(cnode, "title", mfile->title);
                    mdf_set_valuef(cnode, "cover=%sassets/cover/%s", item->libroot, mfile->id);
                } else if (mre_match(reo, mfile->name, true)) {
                    /* 匹配文件名 */
                    cnode = mdf_insert_node(outnode, NULL, -1);
                    mdf_set_int_value(cnode, "type", 3);
                    mdf_set_value(cnode, "trackid", mfile->id);
                    mdf_set_value(cnode, "title", mfile->name);
                    mdf_set_valuef(cnode, "cover=%sassets/cover/%s", item->libroot, mfile->id);
                }
            }
        }
    }

    mdf_object_2_array(outnode, NULL);

    char *output = mdf_json_export_string(outnode);
    mdf_destroy(&outnode);

    mre_destroy(&reo);

    return output;
}

char* omusicArtist(char *id, char *name)
{
    if (!id || !name) return NULL;

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return NULL;
    }

    DommeArtist *artist = artistFind(item->plan->artists, name);
    if (!artist) return NULL;

    MDF *outnode;
    mdf_init(&outnode);

    mdf_set_value(outnode, "artist", name);
    mdf_set_int_value(outnode, "countAlbum", mlist_length(artist->albums));
    mdf_set_int_value(outnode, "countTrack", artist->count_track);
    mdf_set_int_value(outnode, "indisk", _artist_cached_count(item, artist));
    mdf_set_valuef(outnode, "avt=%sassets/cover/%s", item->libroot, artist->name);

    MDF *anode = mdf_get_or_create_node(outnode, "albums");

    DommeAlbum *disk;
    MLIST_ITERATE(artist->albums, disk) {
        MDF *cnode = mdf_insert_node(anode, NULL, -1);

        mdf_set_value(cnode, "name", disk->title);
        mdf_set_valuef(cnode, "cover=%s/assets/cover/%s_%s", item->libroot, name, disk->title);
        mdf_set_int_value(cnode, "countTrack", mlist_length(disk->tracks));
        mdf_set_value(cnode, "PD", disk->year);
        if (_disk_cached(item, disk)) mdf_set_bool_value(cnode, "cached", true);
        else mdf_set_bool_value(cnode, "cached", false);
    }
    mdf_object_2_array(anode, NULL);

    char *output = mdf_json_export_string(outnode);
    mdf_destroy(&outnode);

    return output;
}

char* omusicAlbum(char *id, char *name, char*title)
{
    if (!id || !name || !title) return NULL;

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return NULL;
    }

    DommeArtist *artist = artistFind(item->plan->artists, name);
    if (!artist) return NULL;

    DommeAlbum *disk = albumFind(artist->albums, title);
    if (!disk) return NULL;

    MDF *outnode;
    mdf_init(&outnode);

    mdf_set_value(outnode, "title", title);
    mdf_set_value(outnode, "artist", name);
    mdf_set_value(outnode, "PD", disk->year);
    mdf_set_valuef(outnode, "cover=%s/assets/cover/%s_%s", item->libroot, name, disk->title);
    mdf_set_int_value(outnode, "countTrack", mlist_length(disk->tracks));

    MDF *anode = mdf_get_or_create_node(outnode, "tracks");

    DommeFile *dfile;
    char duration[8];
    MLIST_ITERATE(disk->tracks, dfile) {
        MDF *cnode = mdf_insert_node(anode, NULL, -1);

        sprintf(duration, "%02d:%02d", dfile->length / 60, dfile->length % 60);

        mdf_set_value(cnode, "id", dfile->id);
        mdf_set_value(cnode, "title", dfile->title);
        mdf_set_value(cnode, "duration", duration);
    }
    mdf_object_2_array(anode, NULL);

    char *output = mdf_json_export_string(outnode);
    mdf_destroy(&outnode);

    return output;
}

char* omusicLocation(char *id, char *trackid)
{
    if (!id || !trackid) return NULL;

    static char filename[PATH_MAX];

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return NULL;
    }

    DommeFile *dfile = dommeGetFile(item->plan, trackid);
    if (dfile) {
        snprintf(filename, sizeof(filename), "%s%s/%s%s%s",
                 mnetAppDir(), id, item->plan->basedir, dfile->dir, dfile->name);

        if (_file_exist(filename)) return filename;
        else {
            TINY_LOG("%s not exist", filename);
            return "";
        }
    } else {
        TINY_LOG("%s not exist", trackid);
        return NULL;
    }
}

char* omusicLibraryID(char *id)
{
    if (!id) return NULL;

    char filename[PATH_MAX];
    static char fileid[LEN_DOMMEID] = {0};

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return NULL;
    }

    DommeStore *plan = item->plan;

    /* 剔除本地没有的音频文件，标记为已播放 */
    char *key = NULL;
    DommeFile *dfile;
    if (fileid[0] == 0) {
        MHASH_ITERATE(plan->mfiles, key, dfile) {
            snprintf(filename, sizeof(filename), "%s%s/%s%s%s",
                     mnetAppDir(), id, plan->basedir, dfile->dir, dfile->name);
            if (!_file_exist(filename)) {
                dfile->touched = true;
                plan->count_touched++;
            }
        }
    }

    if (plan->count_touched >= plan->count_track) return NULL;

    int trycount = 0;
    uint32_t pos = 0, freeCount = 0;
    DommeArtist *artist;

nextartist:
    pos = mos_rand(mlist_length(plan->artists));
    artist = mlist_getx(plan->artists, pos);
    freeCount = artistFreeTrack(artist);
    if (freeCount > 0) {
        pos = mos_rand(freeCount);
        DommeAlbum *disk;
        MLIST_ITERATE(artist->albums, disk) {
            MLIST_ITERATEB(disk->tracks, dfile) {
                if (!dfile->touched) {
                    snprintf(filename, sizeof(filename), "%s%s/%s%s%s",
                             mnetAppDir(), id, plan->basedir, dfile->dir, dfile->name);
                    if (_file_exist(filename)) {
                        if (pos == 0) {
                            strncpy(fileid, dfile->id, LEN_DOMMEID);
                            dfile->touched = true;
                            plan->count_touched++;
                            return fileid;
                        }
                        pos--;
                    }
                }
            }
        }
    } else if (trycount++ < 10000) goto nextartist;

    return NULL;
}

char* omusicArtistIDS(char *id, char *name)
{
    if (!id || !name) return NULL;

    char filename[PATH_MAX];

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return NULL;
    }

    DommeArtist *artist = artistFind(item->plan->artists, name);
    if (!artist) return NULL;

    MDF *outnode;
    mdf_init(&outnode);

    int idcount = 0;
    DommeAlbum *disk;
    MLIST_ITERATE(artist->albums, disk) {
        DommeFile *dfile;
        MLIST_ITERATEB(disk->tracks, dfile) {
            snprintf(filename, sizeof(filename), "%s%s/%s%s%s",
                     mnetAppDir(), id, item->plan->basedir, dfile->dir, dfile->name);

            if (_file_exist(filename)) mdf_set_valuef(outnode, "[%d]=%s", idcount++, dfile->id);
        }
    }

    mdf_object_2_array(outnode, NULL);

    char *output = mdf_json_export_string(outnode);
    mdf_destroy(&outnode);

    return output;
}

char* omusicAlbumIDS(char *id, char *name, char *title)
{
    if (!id || !name || !title) return NULL;

    char filename[PATH_MAX];

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return NULL;
    }

    DommeArtist *artist = artistFind(item->plan->artists, name);
    if (!artist) return NULL;

    DommeAlbum *disk = albumFind(artist->albums, title);
    if (!disk) return NULL;

    MDF *outnode;
    mdf_init(&outnode);

    int idcount = 0;
    DommeFile *dfile;
    MLIST_ITERATE(disk->tracks, dfile) {
        snprintf(filename, sizeof(filename), "%s%s/%s%s%s",
                 mnetAppDir(), id, item->plan->basedir, dfile->dir, dfile->name);

        if (_file_exist(filename)) mdf_set_valuef(outnode, "[%d]=%s", idcount++, dfile->id);
    }
    mdf_object_2_array(outnode, NULL);

    char *output = mdf_json_export_string(outnode);
    mdf_destroy(&outnode);

    return output;
}

bool omusicSyncStore(char *id, char *storename)
{
    char filename[PATH_MAX];
    MERR *err;
    if (!id || !storename) return false;

    MDF *libconfig;
    mdf_init(&libconfig);

    snprintf(filename, sizeof(filename), "%s%s/config.json", mnetAppDir(), id);
    err = mdf_json_import_file(libconfig, filename);
    RETURN_V_NOK(err, false);

    MDF *snode = mdf_search(libconfig, storename, _store_compare);
    if (!snode) {
        TINY_LOG("find %s failure", storename);
        return false;
    }

    char *name = mdf_get_value(snode, "name", NULL);
    char *path = mdf_get_value(snode, "path", NULL);
    if (!name || !path) {
        TINY_LOG("path not found");
        return false;
    }

    DommeStore *plan = dommeStoreCreate();
    plan->name = strdup(name);
    plan->basedir = strdup(path);
    snprintf(filename, sizeof(filename), "%s%s/%smusic.db", mnetAppDir(), id, plan->basedir);
    err = dommeLoadFromFile(filename, plan);
    if (err) {
        TINY_LOG("媒体库 %s 下的数据库还没同步，先同步整库媒体文件", storename);

        merr_destroy(&err);
        mdf_destroy(&libconfig);
        dommeStoreFree(plan);

        return mnetStoreSyncMEDIA_ANYWAY(id, storename);
    }

    mdf_destroy(&libconfig);

    int idcount = 0;
    snprintf(filename, sizeof(filename), "%s%s/setting/needToSync", mnetAppDir(), id);
    FILE *fp = fopen(filename, "a");
    if (fp) {
        /* 用户操作手速有限，哥就不加锁了 */
        fprintf(fp, "STOREMARK %s\n", storename);

        char *key;
        DommeFile *dfile;
        MHASH_ITERATE(plan->mfiles, key, dfile) {
            if (!_file_existf("%s%s/%s%s%s",
                              mnetAppDir(), id, plan->basedir, dfile->dir, dfile->name)) {
                fputs(dfile->id, fp);
                fputc('\n', fp);
                idcount++;
            }
        }

        fputs("STOREMARK\n", fp);
        fclose(fp);
        dommeStoreFree(plan);
    } else {
        TINY_LOG("open %s for write error %s", filename, strerror(errno));
        dommeStoreFree(plan);
        return false;
    }

    TINY_LOG("write %s with %d ids", filename, idcount);

    mnetSyncTracks(id);

    return true;
}

int omusicClearStore(char *id, char *storename, bool rmdir)
{
    char filename[PATH_MAX];
    MERR *err;
    if (!id || !storename) return 0;

    MDF *libconfig;
    mdf_init(&libconfig);

    snprintf(filename, sizeof(filename), "%s%s/config.json", mnetAppDir(), id);
    err = mdf_json_import_file(libconfig, filename);
    RETURN_V_NOK(err, 0);

    MDF *snode = mdf_search(libconfig, storename, _store_compare);
    if (!snode) {
        TINY_LOG("find %s failure", storename);
        return 0;
    }

    char *name = mdf_get_value(snode, "name", NULL);
    char *path = mdf_get_value(snode, "path", NULL);
    if (!name || !path) {
        TINY_LOG("path not found");
        return 0;
    }

    DommeStore *plan = dommeStoreCreate();
    plan->name = strdup(name);
    plan->basedir = strdup(path);
    snprintf(filename, sizeof(filename), "%s%s/%smusic.db", mnetAppDir(), id, plan->basedir);
    err = dommeLoadFromFile(filename, plan);
    if (err) {
        TINY_LOG("load %s db failure", storename);

        merr_destroy(&err);
        mdf_destroy(&libconfig);
        dommeStoreFree(plan);

        return 0;
    }

    mdf_destroy(&libconfig);

    int filecount = 0;
    char *key;
    DommeFile *dfile;
    MHASH_ITERATE(plan->mfiles, key, dfile) {
        snprintf(filename, sizeof(filename), "%s%s/%s%s%s", mnetAppDir(), id,
                 plan->basedir, dfile->dir, dfile->name);
        if (remove(filename) == 0) filecount++;
    }

    TINY_LOG("remove %d files", filecount);

    if (rmdir) {

        mos_rmrff("%s%s/%s", mnetAppDir(), id, plan->basedir);
    }

    dommeStoreFree(plan);

    return filecount;
}

bool omusicSyncArtist(char *id, char *name)
{
    if (!id || !name) return false;

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return false;
    }

    DommeArtist *artist = artistFind(item->plan->artists, name);
    if (!artist) return false;

    int idcount = 0;
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s%s/setting/needToSync", mnetAppDir(), id);
    FILE *fp = fopen(filename, "a");
    if (fp) {
        fprintf(fp, "STOREMARK %s\n", item->plan->name);

        DommeAlbum *disk;
        MLIST_ITERATE(artist->albums, disk) {
            DommeFile *dfile;
            MLIST_ITERATEB(disk->tracks, dfile) {
                if (!_file_existf("%s%s/%s%s%s", mnetAppDir(), item->id,
                                  item->plan->basedir, dfile->dir, dfile->name)) {
                    fputs(dfile->id, fp);
                    fputc('\n', fp);
                    idcount++;
                }
            }
        }

        fputs("STOREMARK\n", fp);
        fclose(fp);
    } else {
        TINY_LOG("open %s for write error %s", filename, strerror(errno));
        return false;
    }

    TINY_LOG("write %s with %d ids", filename, idcount);

    mnetSyncTracks(id);

    return true;
}

int omusicClearArtist(char *id, char *name)
{
    char filename[PATH_MAX];

    if (!id || !name) return 0;

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return 0;
    }

    DommeArtist *artist = artistFind(item->plan->artists, name);
    if (!artist) return 0;

    int filecount = 0;
    DommeAlbum *disk;
    MLIST_ITERATE(artist->albums, disk) {
        DommeFile *dfile;
        MLIST_ITERATEB(disk->tracks, dfile) {
            snprintf(filename, sizeof(filename), "%s%s/%s%s%s", mnetAppDir(), item->id,
                     item->plan->basedir, dfile->dir, dfile->name);
            if (remove(filename) == 0) filecount++;
        }
    }

    TINY_LOG("remove %d files", filecount);

    return filecount;
}

bool omusicSyncAlbum(char *id, char *name, char *title)
{
    if (!id || !name || !title) return false;

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return false;
    }

    DommeArtist *artist = artistFind(item->plan->artists, name);
    if (!artist) return false;

    DommeAlbum *disk = albumFind(artist->albums, title);
    if (!disk) return false;

    int idcount = 0;
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s%s/setting/needToSync", mnetAppDir(), id);
    FILE *fp = fopen(filename, "a");
    if (fp) {
        fprintf(fp, "STOREMARK %s\n", item->plan->name);

        DommeFile *dfile;
        MLIST_ITERATE(disk->tracks, dfile) {
            if (!_file_existf("%s%s/%s%s%s", mnetAppDir(), item->id,
                              item->plan->basedir, dfile->dir, dfile->name)) {
                fputs(dfile->id, fp);
                fputc('\n', fp);
                idcount++;
            }
        }

        fputs("STOREMARK\n", fp);
        fclose(fp);
    } else {
        TINY_LOG("open %s for write error %s", filename, strerror(errno));
        return false;
    }

    TINY_LOG("write %s with %d ids", filename, idcount);

    mnetSyncTracks(id);

    return true;
}

int omusicClearAlbum(char *id, char *name, char *title)
{
    char filename[PATH_MAX];
    if (!id || !name || !title) return 0;

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return 0;
    }

    DommeArtist *artist = artistFind(item->plan->artists, name);
    if (!artist) return 0;

    DommeAlbum *disk = albumFind(artist->albums, title);
    if (!disk) return 0;

    int filecount = 0;
    DommeFile *dfile;
    MLIST_ITERATE(disk->tracks, dfile) {
        snprintf(filename, sizeof(filename), "%s%s/%s%s%s", mnetAppDir(), item->id,
                 item->plan->basedir, dfile->dir, dfile->name);
        if (remove(filename) == 0) filecount++;
    }

    TINY_LOG("remove %d files", filecount);

    return filecount;
}

int omusicDeleteAlbum(char *id, char *name, char *title)
{
    char filename[PATH_MAX];
    if (!id || !name || !title) return 0;

    OmusicNode *item = _makesure_load(id);
    if (!item) {
        TINY_LOG("%s store/path empty", id);
        return 0;
    }

    DommeArtist *artist = artistFind(item->plan->artists, name);
    if (!artist) return 0;

    DommeAlbum *disk = albumFind(artist->albums, title);
    if (!disk) return 0;

    DommeFile *dfile;
    MLIST_ITERATE(disk->tracks, dfile) {
        snprintf(filename, sizeof(filename), "%s%s/%s%s%s", mnetAppDir(), item->id,
                 item->plan->basedir, dfile->dir, dfile->name);
        TINY_LOG("remove %s", filename);
        remove(filename);

        mnetDeleteTrack(id, dfile->id);
    }

    TINY_LOG("remove %d files", mlist_length(disk->tracks));

    return mlist_length(disk->tracks);
}
