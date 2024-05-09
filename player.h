#ifndef __PLAYER_H__
#define __PLAYER_H__


#include <stdint.h>
#include <pthread.h>

#include "playlist.h"
#include "util.h"


enum {
	PLAYER_STATE_STOPPED = 0,
	PLAYER_STATE_PLAYING,
	PLAYER_STATE_PAUSED,
	PLAYER_STATE_UNKNOWN,
};

typedef void (*PlayerCallback)(void *udata);

typedef struct {
	int                 is_alive;
	int                 state;
	const PlaylistItem *playlist_item;
	int64_t             playlist_item_duration;

	PlayerCallback      on_player_begin;
	PlayerCallback      on_player_end;
	PlayerCallback      on_player_duration;
	void               *callback_udata;

	pthread_mutex_t    mutex;
	pthread_cond_t     condv;
} Player;


int  player_init(Player *p, PlayerCallback on_player_begin, PlayerCallback on_player_end,
		 PlayerCallback on_player_duration, void *callback_udata);
void player_deinit(Player *p);
int  player_run(Player *p);
void player_stop(Player *p);

int  player_item_play(Player *p, const PlaylistItem *item);
int  player_item_pause(Player *p);
int  player_item_stop(Player *p);


#endif

