#ifndef __PLAYER_H__
#define __PLAYER_H__


#include <stdint.h>
#include <stdatomic.h>
#include <threads.h>

#include <portaudio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>

#include "pa/pa_ringbuffer.h"


typedef struct player_context {
	atomic_int         is_active;
	atomic_int         is_stopped;
	unsigned           index;
	AVPacket          *pkt;
	AVFrame           *frame;
	AVFormatContext   *format;
	AVCodecContext    *codec;
	SwrContext        *swr;
	uint8_t           *swr_buffer;
	thrd_t             thrd;
	atomic_size_t      frames_total;
	const char        *file;
} PlayerContext;

typedef struct player {
	atomic_int        is_paused;
	PaStream          *stream;
	PaUtilRingBuffer   buffer;
	PlayerContext      context;
	uint8_t           *swr_buffer;
} Player;


int     player_init(Player *p);
void    player_deinit(Player *p);
int     player_item_play(Player *p, const char file[]);
void    player_item_stop(Player *p);
void    player_item_pause(Player *p);
void    player_item_resume(Player *p);
int64_t player_item_get_time(Player *p);
int     player_item_is_stopped(Player *p);


#endif

