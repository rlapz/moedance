#ifndef __PLAYER_H__
#define __PLAYER_H__


#include <stdint.h>
#include <stdatomic.h>
#include <threads.h>

#include <pipewire/pipewire.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>


typedef struct player_context {
	atomic_int        is_active;
	atomic_int        is_stopped;
	unsigned          index;
	AVPacket         *pkt;
	AVFrame          *frame;
	AVFormatContext  *format;
	AVCodecContext   *codec;
	SwrContext       *swr;
	uint8_t          *swr_buffer;
	atomic_size_t     frames_total;
	const char       *file;
	thrd_t            thrd;
} PlayerContext;

typedef struct player {
	atomic_int        is_paused;
	struct pw_stream *stream;
	struct pw_loop   *pw_ctx;
	PlayerContext     context;
	uint8_t          *swr_buffer;
} Player;


int  player_init(Player *p);
void player_deinit(Player *p);
int  player_get_fd(Player *p);
int  player_iterate(Player *p);

int     player_item_play(Player *p, const char file[]);
void    player_item_stop(Player *p);
void    player_item_toggle(Player *p);
int64_t player_item_get_time(Player *p);
int     player_item_is_stopped(Player *p);


#endif

