#ifndef __MOEDANCE_H__
#define __MOEDANCE_H__


#include <stdint.h>
#include <poll.h>

#include "tui.h"
#include "player.h"
#include "playlist.h"


enum {
	MOEDANCE_FD_KBD = 0,
	MOEDANCE_FD_SIGNAL,

	__MOEDANCE_FD_SIZE,
};

typedef struct {
	int            flags;
	const char    *root_dir;
	Tui            tui;
	Player         player;
	Playlist       playlist;
	struct pollfd  poll_fds[__MOEDANCE_FD_SIZE];
} MoeDance;


void moedance_init(MoeDance *m, const char root_dir[]);
void moedance_deinit(MoeDance *m);
int  moedance_run(MoeDance *m);


#endif

