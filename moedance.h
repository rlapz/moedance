#ifndef __MOEDANCE_H__
#define __MOEDANCE_H__


#include <stdint.h>
#include <poll.h>
#include <pthread.h>

#include "tui.h"
#include "player.h"
#include "playlist.h"


typedef struct {
	int      flags;
	Tui      tui;
	Player   player;
	Playlist playlist;
	char     root_dir[4096];

	pthread_mutex_t mutex;
} MoeDance;


void moedance_init(MoeDance *m, const char root_dir[]);
void moedance_deinit(MoeDance *m);
int  moedance_run(MoeDance *m);


#endif

