#include <assert.h>
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


static void *_run_thrd(void *udata);
static void  _run(Player *p);


/*
 * public
 */
int
player_init(Player *p, PlayerCallback on_player_begin, PlayerCallback on_player_end,
	    PlayerCallback on_player_duration, void *callback_udata)
{
	if (pthread_mutex_init(&p->mutex, NULL) != 0)
		return -1;

	if (pthread_cond_init(&p->condv, NULL) != 0) {
		pthread_mutex_destroy(&p->mutex);
		return -1;
	}

	p->is_alive = 0;
	p->state = PLAYER_STATE_STOPPED;
	p->playlist_item_duration = 0;
	p->playlist_item = NULL;
	p->on_player_begin = on_player_begin;
	p->on_player_end = on_player_end;
	p->on_player_duration = on_player_duration;
	p->callback_udata = callback_udata;
	return 0;
}


void
player_deinit(Player *p)
{
	pthread_cond_wait(&p->condv, &p->mutex);

	pthread_mutex_destroy(&p->mutex);
	pthread_cond_destroy(&p->condv);
}


int
player_run(Player *p)
{
	pthread_t thrd;
	if (pthread_create(&thrd, NULL, _run_thrd, p) != 0)
		return -1;

	pthread_detach(thrd);
	return 0;
}


void
player_stop(Player *p)
{
	pthread_mutex_lock(&p->mutex); /* LOCK */
	p->is_alive = 0;
	pthread_mutex_unlock(&p->mutex); /* UNLOCK */
}


int
player_item_play(Player *p, const PlaylistItem *item)
{
	p->state = PLAYER_STATE_PLAYING;
	p->playlist_item = item;
	p->playlist_item_duration = 0;
	return 0;
}


int
player_item_pause(Player *p)
{
	p->state = PLAYER_STATE_PAUSED;
	return 0;
}


int
player_item_stop(Player *p)
{
	p->state = PLAYER_STATE_STOPPED;
	return 0;
}


/*
 * private
 */
static void *
_run_thrd(void *udata)
{
	_run((Player *)udata);
	return NULL;
}


static void
_run(Player *p)
{
	pthread_mutex_lock(&p->mutex); /* LOCK */
	p->is_alive = 1;
	while (p->is_alive) {
		pthread_mutex_unlock(&p->mutex); /* UNLOCK */
		sleep(1);
		p->on_player_duration(p->callback_udata);
		pthread_mutex_lock(&p->mutex); /* LOCK */
	}

	pthread_mutex_unlock(&p->mutex); /* UNLOCK */
	pthread_cond_signal(&p->condv);
}

