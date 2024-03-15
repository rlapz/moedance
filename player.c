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


#define _FLAGS (MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION)


int
player_init(Player *p)
{
	const ma_result res = ma_engine_init(NULL, &p->engine);
	if (res != MA_SUCCESS) {
		log_err(0, "player_init: ma_engine_init: failed to init");
		return -1;
	}

	p->sound_init = 0;
	p->state = PLAYER_STATE_STOPPED;
	return 0;
}


void
player_deinit(Player *p)
{
	player_stop(p);
	ma_engine_uninit(&p->engine);
}


int
player_play(Player *p, const char file_path[], int64_t offt)
{
	player_stop(p);
	if (ma_sound_init_from_file(&p->engine, file_path, _FLAGS, NULL, NULL, &p->sound) != MA_SUCCESS) {
		log_err(0, "player_play: failed to play: \"%s\"", file_path);
		return -1;
	}

	ma_sound_start(&p->sound);
	p->state = PLAYER_STATE_PLAYING;
	p->sound_init = 1;
	return 0;
}


void
player_stop(Player *p)
{
	if (p->sound_init == 1) {
		ma_sound_stop(&p->sound);
		ma_sound_uninit(&p->sound);
		p->state = PLAYER_STATE_STOPPED;
		p->sound_init = 0;
	}
}

