#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <sys/stat.h>

#include "player.h"
#include "util.h"


int
player_init(Player *p)
{
	p->state = PLAYER_STATE_STOPPED;
	p->playlist_item_duration = 0;
	p->playlist_item = NULL;
	return 0;
}


void
player_deinit(Player *p)
{
}


int
player_play(Player *p, const PlaylistItem *item)
{
	p->state = PLAYER_STATE_PLAYING;
	return 0;
}


void
player_pause(Player *p)
{
	p->state = PLAYER_STATE_PAUSED;
}


void
player_stop(Player *p)
{
	p->state = PLAYER_STATE_STOPPED;
}

