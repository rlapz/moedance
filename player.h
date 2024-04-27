#ifndef __PLAYER_H__
#define __PLAYER_H__


#include <stdint.h>

#include "util.h"


enum {
	PLAYER_STATE_STOPPED = 0,
	PLAYER_STATE_PLAYING,
	PLAYER_STATE_PAUSED,
	PLAYER_STATE_UNKNOWN,
};

typedef struct {
	const char *name;
	int64_t     duration; /* max duration */
	const char *file_ext;
	char        file_path[1];
} PlayerItem;

typedef struct {
	int sound_init;
	int state;
} Player;

int  player_init(Player *p);
void player_deinit(Player *p);
int  player_play(Player *p, const char file_path[], int64_t offt);
void player_stop(Player *p);


#endif

