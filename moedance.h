#ifndef __MOEDANCE_H__
#define __MOEDANCE_H__


#include <stdint.h>
#include <poll.h>

#include "tui.h"
#include "player.h"


enum {
	MOEDANCE_FD_KBD = 0,
	MOEDANCE_FD_SIGNAL,

	__MOEDANCE_FD_SIZE,
};

typedef struct {
	int         is_selected;
	int         now_playing;
	int64_t     duration_min;
	int64_t     duration_max;
	const char *name;
	char        path[];
} MoeDanceItem;

typedef struct {
	int         flags;
	const char *root_dir;

	int            item_top;
	int            item_active;
	int            item_cursor;
	int            items_len;
	MoeDanceItem **items;

	Tui           tui;
	Player        player;
	struct pollfd poll_fds[__MOEDANCE_FD_SIZE];
} MoeDance;

void moedance_init(MoeDance *m, const char root_dir[]);
void moedance_deinit(MoeDance *m);
int  moedance_run(MoeDance *m);


#endif

