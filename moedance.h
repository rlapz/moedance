#ifndef __MOEDANCE_H__
#define __MOEDANCE_H__


#include <stdint.h>
#include <poll.h>
#include <threads.h>

#include "tui.h"
#include "player.h"
#include "playlist.h"


typedef struct moedance {
	volatile int  flags;
	Tui           tui;
	Player        player;
	Playlist      playlist;
	const char   *root_dir;
	atomic_int    is_started;
	mtx_t         mutex;
} Moedance;


int  moedance_init(Moedance *m, const char root_dir[]);
void moedance_deinit(Moedance *m);
int  moedance_run(Moedance *m);


#endif

