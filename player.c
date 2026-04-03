#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <threads.h>

#include "player.h"
#include "util.h"


#define _AUDIO_CHANNELS_COUNT		(2)
#define _AUDIO_SAMPLE_FORMAT		paFloat32
#define _AUDIO_SAMPLE_RATE		(44100)
#define _AUDIO_FRAME_BUFFER_SIZE	(4096)
#define _AUDIO_WAIT_TIME_MS		(100)
#define _FILE_SAMPLE_RATE		_AUDIO_SAMPLE_RATE
#define _FILE_SAMPLE_FORMAT		AV_SAMPLE_FMT_FLT
#define _SWR_BUFFER_SIZE		(1024 * 1024)
#define _RING_BUFFER_SIZE		(1024 * 32)
#define _RING_BUFFER_ELEM_SIZE		(_AUDIO_CHANNELS_COUNT * sizeof(float))


/*
 * PlayerContext
 */
static int  _context_init(PlayerContext *c, uint8_t swr_buffer[], PaUtilRingBuffer *buffer);
static int  _context_av_init(PlayerContext *c);
static int  _context_swr_init(PlayerContext *c);
static void _context_deinit(PlayerContext *c);
static void _context_writer(PlayerContext *p);


/*
 * Player
 */
static int  _open_device(Player *p);
static void _close_device(Player *p);
static int  _stream_cb(const void *input, void *output, unsigned long count,
		       const PaStreamCallbackTimeInfo *time_info,
		       PaStreamCallbackFlags flags, void *udata);
static int  _file_reader_thrd(void *udata);


/*
 * Public
 */
int
player_init(Player *p)
{
	memset(p, 0, sizeof(*p));
	atomic_store(&p->is_paused, 1);
	atomic_store(&p->context.is_active, 0);
	atomic_store(&p->context.is_stopped, 1);

	uint8_t *const buffer = malloc(_RING_BUFFER_ELEM_SIZE * _RING_BUFFER_SIZE);
	if (buffer == NULL) {
		log_err(errno, "player: player_init: malloc: ring buffer");
		return -1;
	}
	
	long ret = PaUtil_InitializeRingBuffer(&p->buffer, _RING_BUFFER_ELEM_SIZE,
					       _RING_BUFFER_SIZE, buffer);
	if (ret < 0) {
		log_err(0, "player: player_init: PaUtil_InitializeRingBuffer: invalid buffer size");
		goto err0;
	}
	
	uint8_t *const swr_buffer = malloc(_SWR_BUFFER_SIZE);
	if (swr_buffer == NULL) {
		log_err(errno, "player: player_init: malloc: swr buffer");
		goto err0;
	}

	ret = _open_device(p);
	if (ret < 0)
		goto err1;

	p->swr_buffer = swr_buffer;
	return 0;

err1:
	free(swr_buffer);
err0:
	free(buffer);
	return -1;
}


void
player_deinit(Player *p)
{
	atomic_store(&p->is_paused, 1);
	Pa_StopStream(p->stream);
	Pa_CloseStream(p->stream);

	_close_device(p);
	free(p->buffer.buffer);
	free(p->swr_buffer);
}


int
player_item_play(Player *p, const char file[])
{
	PlayerContext *const c = &p->context;
	if (atomic_load(&c->is_stopped) == 0)
		player_item_stop(p);

	c->file = file;
	if (thrd_create(&c->thrd, _file_reader_thrd, p) != thrd_success) {
		log_err(0, "player: player_item_play: thrd_create: failed");
		return -1;
	}

	return 0;
}


void
player_item_stop(Player *p)
{
	PlayerContext *const c = &p->context;
	if (atomic_load(&c->is_active) == 0)
		return;

	atomic_store(&c->is_active, 0);
	thrd_join(c->thrd, NULL);
}


void
player_item_toggle(Player *p)
{
	if (atomic_load(&p->context.is_active) == 0) {
		atomic_store(&p->is_paused, 1);
		return;
	}

	if (atomic_load(&p->is_paused))
		atomic_store(&p->is_paused, 0);
	else
		atomic_store(&p->is_paused, 1);
}


int64_t
player_item_get_time(Player *p)
{
	const size_t frm = atomic_load(&p->context.frames_total);
	return (int64_t)((double)(frm / _AUDIO_SAMPLE_RATE));
}


int
player_item_is_playing(Player *p)
{
	return ((atomic_load(&p->is_paused) == 0) &&
		(atomic_load(&p->context.is_stopped) == 0));
}


int
player_item_is_stopped(Player *p)
{
	return atomic_load(&p->context.is_stopped);
}


/*
 * Private
 */
static int
_context_init(PlayerContext *c, uint8_t swr_buffer[], PaUtilRingBuffer *buffer)
{
	if (c->file == NULL) {
		log_err(0, "player: _context_init: file == NULL");
		return -1;
	}

	AVPacket *pkt = av_packet_alloc();
	if (pkt == NULL) {
		log_err(0, "player: _context_init: av_packet_alloc: failed");
		return -1;
	}
	
	AVFrame *frame = av_frame_alloc();
	if (frame == NULL) {
		log_err(0, "player: _context_init: av_frame_alloc: failed");
		goto err0;
	}
	
	int ret = _context_av_init(c);
	if (ret < 0)
		goto err1;
	
	ret = _context_swr_init(c);
	if (ret < 0)
		goto err2;
	
	c->pkt = pkt;
	c->frame = frame;
	c->swr_buffer = swr_buffer;
	c->buffer = buffer;
	atomic_store(&c->frames_total, 0);
	return 0;
	
err2:
	avcodec_free_context(&c->codec);
	avformat_close_input(&c->format);
err1:
	av_frame_free(&frame);
err0:
	av_packet_free(&pkt);
	return -1;
}


static int
_context_av_init(PlayerContext *c)
{
	int ret = avformat_open_input(&c->format, c->file, NULL, NULL);
	if (ret < 0) {
		log_err(0, "player: _context_av_init: avformat_open_input: %s: %s", av_err2str(ret), c->file);
		return -1;
	}
	
	ret = avformat_find_stream_info(c->format, NULL);
	if (ret < 0) {
		log_err(0, "player: _context_av_init: avformat_find_stream_info: %s", av_err2str(ret));
		goto err0;
	}
	
	int is_ok = 0;
	for (unsigned i = 0; i < c->format->nb_streams; i++) {
		if (c->format->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
			continue;
		
		c->index = i;
		is_ok = 1;
		break;
	}

	if (is_ok == 0) {
		log_err(0, "player: _context_av_init: no decoder found");
		goto err0;
	}

	const AVCodecParameters *const cpar = c->format->streams[c->index]->codecpar;
	const AVCodec *const codec = avcodec_find_decoder(cpar->codec_id);
	c->codec = avcodec_alloc_context3(codec);
	if (c->codec == NULL) {
		log_err(0, "player: _context_av_init: avcodec_alloc_context3: failed");
		goto err0;
	}
	
	ret = avcodec_parameters_to_context(c->codec, cpar);
	if (ret < 0) {
		log_err(0, "player: _context_av_init: avcodec_parameters_to_context: %s", av_err2str(ret));
		goto err1;
	}
	
	ret = avcodec_open2(c->codec, codec, NULL);
	if (ret < 0) {
		log_err(0, "player: _context_av_init: avcodec_open2: %s", av_err2str(ret));
		goto err1;
	}
	
	return 0;

err1:
	avcodec_free_context(&c->codec);
err0:
	avformat_close_input(&c->format);
	return -1;
}


static int
_context_swr_init(PlayerContext *c)
{
	c->swr = swr_alloc();
	if (c->swr == NULL) {
		log_err(0, "player: _context_swr_init: swr_alloc: failed");
		return -1;
	}
	
	AVChannelLayout chan;
	av_channel_layout_default(&chan, _AUDIO_CHANNELS_COUNT);
	av_opt_set_chlayout(c->swr, "out_chlayout", &chan, 0);
	av_opt_set_int(c->swr, "out_sample_fmt", _FILE_SAMPLE_FORMAT, 0);
	av_opt_set_int(c->swr, "out_sample_rate", _FILE_SAMPLE_RATE, 0);
	av_opt_set_chlayout(c->swr, "in_chlayout", &c->codec->ch_layout, 0);
	av_opt_set_int(c->swr, "in_sample_fmt", c->codec->sample_fmt, 0);
	av_opt_set_int(c->swr, "in_sample_rate", c->codec->sample_rate, 0);

	const int ret = swr_init(c->swr);
	if (ret < 0) {
		log_err(0, "player: _context_swr_init: swr_init: %s", av_err2str(ret));
		return -1;
	}

	return 0;
}


static void
_context_deinit(PlayerContext *c)
{
	swr_close(c->swr);
	avcodec_free_context(&c->codec);
	avformat_close_input(&c->format);

	swr_free(&c->swr);
	av_frame_free(&c->frame);
	av_packet_free(&c->pkt);
}


static void
_context_writer(PlayerContext *c)
{
	AVCodecContext *const codec = c->codec;
	AVFrame *const frm = c->frame;
	SwrContext *const swr = c->swr;
	uint8_t *const buffer = c->swr_buffer;
	uint8_t *swr_buffer = buffer;


	while (atomic_load(&c->is_active)) {
		int ret = avcodec_receive_frame(codec, frm);
		if (AVERROR(ret) == EAGAIN)
			return;

		if (ret < 0) {
			log_err(0, "player: _context_writer: avcodec_receive_frame: %s", av_err2str(ret));
			break;
		}

		ret = swr_convert(swr, &swr_buffer, _SWR_BUFFER_SIZE,
				  (const uint8_t **)frm->data, frm->nb_samples);

		while (ret > 0) {
			if (PaUtil_GetRingBufferWriteAvailable(c->buffer) < ret) {
				if (atomic_load(&c->is_active) == 0)
					return;

				// maybe this is not a good idea...
				Pa_Sleep(_AUDIO_WAIT_TIME_MS);
				continue;
			}

			PaUtil_WriteRingBuffer(c->buffer, buffer, ret);

			// flushing...
			ret = swr_convert(swr, &swr_buffer, _SWR_BUFFER_SIZE, NULL, 0);
		}

		if (ret < 0)
			log_err(0, "player: _context_writer: swr_convert: %s", av_err2str(ret));
	}
}


/*
 * Player
 */
static int
_open_device(Player *p)
{
	PaError pe = Pa_Initialize();
	if (pe != paNoError) {
		log_err(0, "player: _open_device: Pa_Initialize: %s", Pa_GetErrorText(pe));
		return -1;
	}
	
	const int host_api = Pa_GetDefaultHostApi();
	if (host_api < 0) {
		log_err(0, "player: _open_device: Pa_GetDefaultHostApi: invalid index");
		goto err0;
	}
	
	const PaHostApiInfo *const host_api_info = Pa_GetHostApiInfo(host_api);
	if (host_api_info == NULL) {
		log_err(0, "player: _open_device: Pa_GetHostApiInfo: invalid index");
		goto err0;
	}
	
	const int device = host_api_info->defaultOutputDevice;
	const PaDeviceInfo *const device_info = Pa_GetDeviceInfo(device);
	if (device_info == NULL) {
		log_err(0, "player: _open_device: Pa_GetDeviceInfo: invalid index");
		goto err0;
	}
	
	const PaStreamParameters param = {
		.device = device,
		.channelCount = _AUDIO_CHANNELS_COUNT,
		.sampleFormat = _AUDIO_SAMPLE_FORMAT,
		.suggestedLatency = device_info->defaultLowOutputLatency,
	};

	pe = Pa_OpenStream(&p->stream, NULL, &param, _AUDIO_SAMPLE_RATE,
			   _AUDIO_FRAME_BUFFER_SIZE, paClipOff | paDitherOff,
			   _stream_cb, p);
	if (pe != paNoError) {
		log_err(0, "player: _open_device: Pa_OpenStream: %s", Pa_GetErrorText(pe));
		goto err0;
	}
	
	pe = Pa_StartStream(p->stream);
	if (pe != paNoError) {
		log_err(0, "player: _open_device: Pa_StartStream: %s", Pa_GetErrorText(pe));
		goto err1;
	}

	return 0;

err1:
	Pa_CloseStream(p->stream);
err0:
	Pa_Terminate();
	return -1;
}


static void
_close_device(Player *p)
{
	Pa_Terminate();
	(void)p;
}


static int
_stream_cb(const void *input, void *output, unsigned long count,
	   const PaStreamCallbackTimeInfo *time_info, PaStreamCallbackFlags flags,
	   void *udata)
{
	Player *const p = (Player *)udata;
	size_t silent_offt = 0;
	size_t silent_size = 0;

	if (atomic_load(&p->is_paused) == 0) {
		const long rd = PaUtil_ReadRingBuffer(&p->buffer, output, count);
		if (rd > 0) {
			const size_t old = atomic_load(&p->context.frames_total);
			atomic_store(&p->context.frames_total, old + (size_t) rd);
		}

		silent_offt = (rd * _RING_BUFFER_ELEM_SIZE);
		silent_size = (count - rd) * _RING_BUFFER_ELEM_SIZE;
	} else {
		// shut up!
		silent_size = (count * _RING_BUFFER_ELEM_SIZE);
	}

	memset(((char *)output) + silent_offt, 0, silent_size);

	(void)input;
	(void)time_info;
	(void)flags;
	return paContinue;
}


static int
_file_reader_thrd(void *udata)
{
	Player *const p = (Player *)udata;
	PlayerContext *const c = &p->context;

	atomic_store(&p->is_paused, 0);
	atomic_store(&c->is_active, 1);
	atomic_store(&c->is_stopped, 0);
	int ret = _context_init(c, p->swr_buffer, &p->buffer);
	if (ret < 0)
		goto out0;


	AVFormatContext *const ctx = c->format;
	AVCodecContext *const codec = c->codec;
	AVPacket *const pkt = c->pkt;
	while (atomic_load(&c->is_active)) {
		ret = av_read_frame(ctx, pkt);
		if (ret < 0) {
			log_err(0, "player: _file_thrd: av_read_frame: %s", av_err2str(ret));
			break;
		}
		
		if ((unsigned)pkt->stream_index != c->index) {
			av_packet_unref(pkt);
			continue;
		}

		ret = avcodec_send_packet(codec, pkt);
		if (ret != 0) {
			log_err(0, "player: _file_thrd: avcodec_send_packet: %s", av_err2str(ret));
			break;
		}
		
		av_packet_unref(pkt);

		_context_writer(c);
	}

	while (atomic_load(&c->is_active)) {
		// make sure there is no data left
		if (PaUtil_GetRingBufferReadAvailable(&p->buffer) == 0)
			break;
		
		Pa_Sleep(_AUDIO_WAIT_TIME_MS);
	}

	PaUtil_FlushRingBuffer(&p->buffer);
	_context_deinit(c);

out0:
	atomic_store(&c->is_active, 0);
	atomic_store(&p->is_paused, 0);
	atomic_store(&c->is_stopped, 1);
	atomic_store(&c->frames_total, 0);
	return 0;
}
