#ifndef __PLAYER_H__
#define __PLAYER_H__


#include <stdint.h>

#include "playlist.h"
#include "util.h"


enum {
	PLAYER_STATE_STOPPED = 0,
	PLAYER_STATE_PLAYING,
	PLAYER_STATE_PAUSED,
	PLAYER_STATE_UNKNOWN,
};

typedef struct {
	int                  state;
	int64_t              playlist_item_duration;
	int                  playlist_item_curr;
	int                  playlist_items_len;
	const PlaylistItem **playlist_items;
} Player;


int  player_init(Player *p);
void player_deinit(Player *p);
void player_set_playlist(Player *p, const PlaylistItem *items[], int len);
int  player_play(Player *p, int idx);
void player_stop(Player *p);


#endif

