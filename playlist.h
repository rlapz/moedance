#ifndef __PLAYLIST_H__
#define __PLAYLIST_H__


#include <stdint.h>


#define PLAYLIST_ITEM_TITLE_SIZE  (64)
#define PLAYLIST_ITEM_ARTIST_SIZE (64)
#define PLAYLIST_ITEM_ALBUM_SIZE  (64)
#define PLAYLIST_ITEM_GENRE_SIZE  (64)


typedef struct playlist_item {
	const char *name;
	char        title[PLAYLIST_ITEM_TITLE_SIZE];
	char        artist[PLAYLIST_ITEM_ARTIST_SIZE];
	char        album[PLAYLIST_ITEM_ALBUM_SIZE];
	char        genre[PLAYLIST_ITEM_GENRE_SIZE];
	int64_t     duration;
	char        file_path[];
} PlaylistItem;

typedef struct playlist {
	int            items_len;
	PlaylistItem **items;
} Playlist;


int  playlist_init(Playlist *p, const char root_dir[]);
int  playlist_load(Playlist *p, const PlaylistItem **items[]);
void playlist_deinit(Playlist *p);


#endif

