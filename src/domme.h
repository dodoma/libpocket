#ifndef __DOMME_H__
#define __DOMME_H__

#define LEN_DOMMEID 11

typedef struct {
    char *title;
    char *year;
    MLIST *tracks;
    uint32_t pos;               /* 当前播放曲目 */
} DommeAlbum;

typedef struct {
    char *name;
    MLIST *albums;
    uint32_t count_track;
    uint32_t pos;               /* 当前播放专辑 */
} DommeArtist;

typedef struct {
    char id[LEN_DOMMEID];

    char *dir;                  /* directory part of filename */
    char *name;                 /* name part of filename */

    char *title;

    uint8_t  sn;
    int index;
    int length;

    DommeAlbum  *disk;
    DommeArtist *artist;

    bool touched;
} DommeFile;

typedef struct {
    char *name;
    char *basedir;
    bool moren;                 /* 默认媒体库 */

    MLIST *dirs;
    MHASH *mfiles;
    MLIST *artists;

    uint32_t count_album;
    uint32_t count_track;
    uint32_t count_touched;
    uint32_t pos;               /* 当前播放曲目 */
} DommeStore;

DommeStore* dommeStoreCreate();
void dommeStoreClear(DommeStore *plan);
void dommeStoreFree(void *p);
MERR* dommeLoadFromFile(char *filename, DommeStore *plan);
DommeFile* dommeGetFile(DommeStore *plan, char *id);

DommeArtist* artistFind(MLIST *artists, char *name);
DommeAlbum* albumFind(MLIST *albums, char *title);

uint32_t albumFreeTrack(DommeAlbum *disk);
uint32_t artistFreeTrack(DommeArtist *artist);

#endif  /* __DOMME_H__ */
