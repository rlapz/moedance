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
	int                 state;
	int64_t             playlist_item_duration;
	const PlaylistItem *playlist_item;
} Player;


int  player_init(Player *p);
void player_deinit(Player *p);
int  player_play(Player *p, const PlaylistItem *item);
void player_pause(Player *p);
void player_stop(Player *p);


#endif

