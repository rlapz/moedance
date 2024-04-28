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
	p->playlist_item_curr = -1;
	p->playlist_item_duration = 0;
	p->playlist_items_len = 0;
	p->playlist_items = NULL;
	return 0;
}


void
player_deinit(Player *p)
{
}


void
player_set_playlist(Player *p, const PlaylistItem *items[], int len)
{
	if (len <= 0)
		return;

	p->playlist_items = items;
	p->playlist_items_len = len;
	p->playlist_item_curr = 0;
	p->playlist_item_duration = 0;
	p->state = PLAYER_STATE_STOPPED;
}


int
player_play(Player *p, int idx)
{
	p->state = PLAYER_STATE_PLAYING;
	return 0;
}


void
player_stop(Player *p)
{
}

