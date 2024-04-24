#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include "player.h"
#include "util.h"


int
player_init(Player *p)
{
	log_info("player item: %zu", sizeof(PlayerItem));
	p->sound_init = 0;
	p->state = PLAYER_STATE_STOPPED;
	return 0;
}


void
player_deinit(Player *p)
{
	player_stop(p);
}


int
player_play(Player *p, const char file_path[], int64_t offt)
{
	p->state = PLAYER_STATE_PLAYING;
	p->sound_init = 1;
	return 0;
}


void
player_stop(Player *p)
{
}

