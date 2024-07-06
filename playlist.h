#ifndef __PLAYLIST_H__
#define __PLAYLIST_H__


#include <stdint.h>


typedef struct playlist_item {
	const char *name;
	int64_t     duration;
	char        file_path[];
} PlaylistItem;

typedef struct playlist {
	int            items_len;
	PlaylistItem **items;
} Playlist;


void                 playlist_init(Playlist *p);
const PlaylistItem **playlist_load(Playlist *p, const char root_dir[], int *len);
void                 playlist_deinit(Playlist *p);


#endif

