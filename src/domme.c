#include <reef.h>

#include "pocket.h"
#include "domme.h"

static int _track_compare(const void *a, const void *b)
{
    DommeFile *pa, *pb;

    pa = *(DommeFile**)a;
    pb = *(DommeFile**)b;

    return pa->sn - pb->sn;
}

static void dommeFileFree(void *key, void *val)
{
    DommeFile *mfile = (DommeFile*)val;

    /* dir free in StoreFree */
    mos_free(mfile->name);
    mos_free(mfile->title);
    mos_free(mfile);
}

static DommeAlbum* albumCreate(char *title)
{
    if (!title) return NULL;

    DommeAlbum *disk = mos_calloc(1, sizeof(DommeAlbum));
    disk->title = strdup(title);
    disk->year = NULL;
    disk->pos = 0;
    mlist_init(&disk->tracks, NULL);

    return disk;
}

static void albumFree(void *p)
{
    DommeAlbum *disk = (DommeAlbum*)p;

    mos_free(disk->title);
    mos_free(disk->year);
    mlist_destroy(&disk->tracks);
    mos_free(disk);
}

static DommeArtist* artistCreate(char *name)
{
    if (!name) return NULL;

    DommeArtist *artist = mos_calloc(1, sizeof(DommeArtist));
    artist->name = strdup(name);
    artist->pos = 0;
    artist->count_track = 0;
    mlist_init(&artist->albums, albumFree);

    return artist;
}

static void artistFree(void *p)
{
    DommeArtist *artist = (DommeArtist*)p;

    mos_free(artist->name);
    mlist_destroy(&artist->albums);
    mos_free(artist);
}

DommeStore* dommeStoreCreate()
{
    DommeStore *plan = mos_calloc(1, sizeof(DommeStore));
    plan->name = NULL;
    plan->basedir = NULL;
    plan->moren = false;
    plan->count_album = 0;
    plan->count_track = 0;
    plan->count_touched = 0;

    mlist_init(&plan->dirs, free);
    mhash_init(&plan->mfiles, mhash_str_hash, mhash_str_comp, dommeFileFree);
    mlist_init(&plan->artists, artistFree);

    return plan;
}

void dommeStoreClear(DommeStore *plan)
{
    if (!plan) return;

    mlist_clear(plan->dirs);
    mhash_destroy(&plan->mfiles);
    mhash_init(&plan->mfiles, mhash_str_hash, mhash_str_comp, dommeFileFree);
    mlist_clear(plan->artists);

    plan->count_album = 0;
    plan->count_track = 0;
    plan->count_touched = 0;

    plan->pos = 0;
}

void dommeStoreFree(void *p)
{
    DommeStore *plan = (DommeStore*)p;

    mos_free(plan->name);
    mos_free(plan->basedir);

    mlist_destroy(&plan->dirs);
    mhash_destroy(&plan->mfiles);
    mlist_destroy(&plan->artists);

    mos_free(plan);
}

MERR* dommeLoadFromFile(char *filename, DommeStore *plan)
{
    DommeFile *mfile;
    DommeAlbum *disk;
    DommeArtist *artist;
    MERR *err;

    MERR_NOT_NULLB(filename, plan);

    MDF *dbnode;
    mdf_init(&dbnode);

    err = mdf_mpack_import_file(dbnode, filename);
    if (err) {
        mdf_destroy(&dbnode);
        return merr_pass(err);
    }
    //MDF_TRACE_MT(dbnode);

    MDF *cnode = mdf_node_child(dbnode);
    while (cnode) {
        char *dir = mdf_get_value_copy(cnode, "dir", NULL);
        if (!dir) goto nextdir;

        char **tmps = mlist_search(plan->dirs, &dir, mlist_strcompare);
        if (tmps) {
            mos_free(dir);
            dir = *tmps;
        } else mlist_append(plan->dirs, dir);

        MDF *artnode = mdf_get_child(cnode, "art");
        while (artnode) {
            char *sartist = mdf_get_value(artnode, "a", NULL);
            char *salbum  = mdf_get_value(artnode, "b", NULL);
            char *syear   = mdf_get_value(artnode, "c", "");

            if (!sartist || !salbum) goto nextdisk;

            artist = artistFind(plan->artists, sartist);
            if (!artist) {
                artist = artistCreate(sartist);
                mlist_append(plan->artists, artist);
            }
            disk = albumFind(artist->albums, salbum);
            if (!disk) {
                disk = albumCreate(salbum);
                disk->year = strdup(syear);

                mlist_append(artist->albums, disk);
                plan->count_album++;
            }

            MDF *song = mdf_get_child(artnode, "d");
            while (song) {
                if (mdf_child_count(song, NULL) != 4) continue;

                char *id = mdf_get_value(song, "[0]", NULL);
                char *name = mdf_get_value(song, "[1]", NULL);
                char *title = mdf_get_value(song, "[2]", NULL);

                mtc_mt_noise("restore music %s%s with id %s", dir, name, id);

                mfile = mos_calloc(1, sizeof(DommeFile));
                memcpy(mfile->id, id, strlen(id) > LEN_DOMMEID ? LEN_DOMMEID : strlen(id));
                mfile->id[LEN_DOMMEID-1] = 0;
                mfile->dir = dir;
                mfile->name = strdup(name);
                mfile->title = strdup(title);

                mfile->sn = mdf_get_int_value(song, "[3]", 0);
                mfile->touched = false;

                mfile->artist = artist;
                mfile->disk = disk;

                mhash_insert(plan->mfiles, mfile->id, mfile);
                plan->count_track++;

                mlist_append(disk->tracks, mfile);
                artist->count_track++;

                song = mdf_node_next(song);
            }

        nextdisk:
            artnode = mdf_node_next(artnode);
        }

    nextdir:
        cnode = mdf_node_next(cnode);
    }

    mdf_destroy(&dbnode);

    /*
     * post dbload scan
     */
    MLIST_ITERATE(plan->artists, artist) {
        MLIST_ITERATEB(artist->albums, disk) {
            mlist_sort(disk->tracks, _track_compare);
        }
    }

    return MERR_OK;
}

DommeFile* dommeGetFile(DommeStore *plan, char *id)
{
    if (!plan || !id) return NULL;

    return mhash_lookup(plan->mfiles, (void*)id);
}

DommeArtist* artistFind(MLIST *artists, char *name)
{
    DommeArtist *artist;
    MLIST_ITERATE(artists, artist) {
        /* TODO 艺术家可能有包含关系，srv, srv & double trouble... */
        if (!strcmp(artist->name, name)) return artist;
    }

    return NULL;
}

DommeAlbum* albumFind(MLIST *albums, char *title)
{
    DommeAlbum *disk;
    MLIST_ITERATE(albums, disk) {
        if (!strcmp(disk->title, title)) return disk;
    }

    return NULL;
}
