#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <pthread.h>

#include <libavutil/log.h>

#include <sys/stat.h>

#include "moedance.h"
#include "kbd.h"
#include "config.h"


#define CTRL_KEY(C) ((C) & 0x1f)


enum {
	_FLAG_ALIVE     = (1 << 0),
	_FLAG_READY     = (1 << 1),
	_FLAG_TUI_READY = (1 << 2),
	_FLAG_KEY_QUIT  = (1 << 3),
};


static int  _set_signal_handler(void);
static void _signal_handler(int sig);

static void _set_playlist(MoeDance *m);

static int  _event_loop(MoeDance *m);
static void _event_handle_kbd(MoeDance *m, int fd);

static void _tui_quit_dialog(MoeDance *m);

static void _kbd_handle_key_stop(MoeDance *m);
static void _kbd_handle_key_enter(MoeDance *m);
static void _kbd_handle_key_toggle_play(MoeDance *m);
static void _kbd_handle_key_next(MoeDance *m);
static void _kbd_handle_key_prev(MoeDance *m);

static void _on_player_begin(void *udata);
static void _on_player_end(void *udata);
static void _on_player_duration(void *udata);


static MoeDance *_moedance = NULL;


/*
 * public
 */
void
moedance_init(MoeDance *m, const char root_dir[])
{
	m->flags = 0;
	cstr_copy(m->root_dir, root_dir);

	playlist_init(&m->playlist);

	pthread_mutex_init(&m->mutex, NULL);

	av_log_set_level(AV_LOG_QUIET);
	_moedance = m;
}


void
moedance_deinit(MoeDance *m)
{
	playlist_deinit(&m->playlist);
	pthread_mutex_destroy(&m->mutex);
}


int
moedance_run(MoeDance *m)
{
	log_file_init(CFG_LOG_FILE);

	int ret = tui_init(&m->tui, m->root_dir);
	if (ret < 0)
		goto out0;

	ret = player_init(&m->player, _on_player_begin, _on_player_end, _on_player_duration, m);
	if (ret < 0)
		goto out1;

	ret = player_run(&m->player);
	if (ret < 0)
		goto out2;

	ret = _set_signal_handler();
	if (ret < 0)
		goto out2;

	tui_draw(&m->tui);
	_set_playlist(m);

	SET(m->flags, _FLAG_TUI_READY);
	ret = _event_loop(m);
	UNSET(m->flags, _FLAG_TUI_READY);

out2:
	player_stop(&m->player);
	player_deinit(&m->player);
out1:
	tui_deinit(&m->tui);
out0:
	log_file_deinit();
	return ret;
}


/*
 * private
 */
static int
_set_signal_handler(void)
{
	struct sigaction act;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = _signal_handler;


	if (sigaction(SIGHUP, &act, NULL) < 0) {
		log_err(errno, "moedance: _set_signal_handler: sigaction: SIGHUP");
		return -1;
	}

	if (sigaction(SIGQUIT, &act, NULL) < 0) {
		log_err(errno, "moedance: _set_signal_handler: sigaction: SIGQUIT");
		return -1;
	}

	if (sigaction(SIGINT, &act, NULL) < 0) {
		log_err(errno, "moedance: _set_signal_handler: sigaction: SIGINT");
		return -1;
	}

	if (sigaction(SIGTERM, &act, NULL) < 0) {
		log_err(errno, "moedance: _set_signal_handler: sigaction: SIGTERM");
		return -1;
	}

	if (sigaction(SIGWINCH, &act, NULL) < 0) {
		log_err(errno, "moedance: _set_signal_handler: sigaction: SIGWINCH");
		return -1;
	}

	return 0;
}


static void
_signal_handler(int sig)
{
	switch (sig) {
	case SIGWINCH:
		tui_draw(&_moedance->tui);
		if (ISSET(_moedance->flags, _FLAG_KEY_QUIT))
			_tui_quit_dialog(_moedance);
		break;
	case SIGHUP:
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		UNSET(_moedance->flags, _FLAG_ALIVE);
		break;
	default:
		log_err(0, "moedance: _event_handle_signal: invalid signal: %d", sig);
		break;
	}
}


static void
_set_playlist(MoeDance *m)
{
	tui_show_dialog(&m->tui, "Loading...");

	int items_len = 0;
	const PlaylistItem **const items = playlist_load(&m->playlist, m->root_dir, &items_len);
	if (items == NULL)
		tui_show_dialog(&m->tui, "Failed to load file(s) from the given root dir!");

	tui_set_playlist(&m->tui, items, items_len);
}


static int
_event_loop(MoeDance *m)
{
	struct pollfd pfds = {
		.fd = STDIN_FILENO,
		.events = POLLIN,
	};

	SET(m->flags, (_FLAG_ALIVE | _FLAG_READY));
	while (ISSET(m->flags, _FLAG_ALIVE)) {
		int ret = poll(&pfds, 1, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;

			log_err(errno, "moedance: _event_loop: poll");
			UNSET(m->flags, _FLAG_ALIVE);
			return -1;
		}

		if (ret == 0)
			continue;

		const short int rv = pfds.revents;
		if (ISSET(rv, POLLHUP | POLLERR)) {
			const char *revents_str = "???";
			if (ISSET(rv, POLLHUP))
				revents_str = "POLLHUP";
			else if (ISSET(rv, POLLERR))
				revents_str = "POLLERR";

			log_info("moedance; _event_loop: revents: %s", revents_str);
			UNSET(m->flags, _FLAG_ALIVE);
			return -1;
		}

		if (ISSET(rv, POLLIN) == 0)
			continue;

		_event_handle_kbd(m, pfds.fd);
	}

	tui_show_dialog(&m->tui, "Please wait...");
	return 0;
}


static void
_event_handle_kbd(MoeDance *m, int fd)
{
	char buffer[32];
	const ssize_t rd = read(fd, buffer, sizeof(buffer));
	if (rd < 0) {
		log_err(errno, "moedance: _event_handle_kbd: read");
		return;
	}

	if (ISSET(m->flags, _FLAG_READY) == 0)
		return;

	const int kbd = kbd_parse(buffer, (int)rd);
	if (ISSET(m->flags, _FLAG_KEY_QUIT)) {
		if (kbd == KBD_Y) {
			UNSET(m->flags, _FLAG_ALIVE);
			return;
		}

		UNSET(m->flags, _FLAG_KEY_QUIT);
		tui_show_dialog(&m->tui, NULL);
		return;
	}

	switch (kbd) {
	case KBD_ARROW_UP: tui_playlist_cursor_up(&m->tui); break;
	case KBD_ARROW_DOWN: tui_playlist_cursor_down(&m->tui); break;
	case KBD_HOME: tui_playlist_top(&m->tui); break;
	case KBD_END: tui_playlist_bottom(&m->tui); break;
	case KBD_PAGE_UP: tui_playlist_page_up(&m->tui); break;
	case KBD_PAGE_DOWN: tui_playlist_page_down(&m->tui); break;
	case KBD_SPACE: _kbd_handle_key_toggle_play(m); break;
	case KBD_ENTER: _kbd_handle_key_enter(m); break;
	case KBD_N: _kbd_handle_key_next(m); break;
	case KBD_P: _kbd_handle_key_prev(m); break;
	case KBD_S: _kbd_handle_key_stop(m); break;
	case KBD_Q:
		SET(m->flags, _FLAG_KEY_QUIT);
		_tui_quit_dialog(m);
		return;
	}
}


static void
_tui_quit_dialog(MoeDance *m)
{
	tui_show_dialog(&m->tui, "Quit? (y)");
}


static void
_kbd_handle_key_stop(MoeDance *m)
{
	tui_playlist_stop(&m->tui);
}


static void
_kbd_handle_key_enter(MoeDance *m)
{
	const PlaylistItem *const item = tui_playlist_play(&m->tui);
	if (item != NULL)
		log_info("%s", item->name);
}


static void
_kbd_handle_key_toggle_play(MoeDance *m)
{
	if (m->tui.playlist.state == PLAYER_STATE_PLAYING)
		tui_playlist_pause(&m->tui);
	else
		tui_playlist_play(&m->tui);
}


static void
_kbd_handle_key_next(MoeDance *m)
{
	const PlaylistItem *const item = tui_playlist_next(&m->tui);
	if (item != NULL)
		log_info("next: %s", item->name);
}


static void
_kbd_handle_key_prev(MoeDance *m)
{
	const PlaylistItem *const item = tui_playlist_prev(&m->tui);
	if (item != NULL)
		log_info("prev: %s", item->name);
}


static void
_on_player_begin(void *udata)
{
	MoeDance *const m = (MoeDance *)udata;
	pthread_mutex_lock(&m->mutex); /* LOCK */

	/* TODO */

	pthread_mutex_unlock(&m->mutex); /* UNLOCK */
}


static void
_on_player_end(void *udata)
{
	MoeDance *const m = (MoeDance *)udata;
	pthread_mutex_lock(&m->mutex); /* LOCK */

	/* TODO */

	pthread_mutex_unlock(&m->mutex); /* UNLOCK */
}


static void
_on_player_duration(void *udata)
{
	MoeDance *const m = (MoeDance *)udata;
	pthread_mutex_lock(&m->mutex); /* LOCK */

	if (ISSET(m->flags, _FLAG_TUI_READY | _FLAG_KEY_QUIT)) {
		if (ISSET(m->flags, _FLAG_KEY_QUIT) == 0) {
			const int64_t duration = m->tui.playlist.duration + 1;
			tui_set_duration(&m->tui, duration);
		}
	}

	pthread_mutex_unlock(&m->mutex); /* UNLOCK */
}

