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
#include <threads.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

#include "moedance.h"
#include "kbd.h"
#include "cmd.h"
#include "config.h"


#define _TIMER_VALUE_S (1)


enum {
	_FLAG_ALIVE         = (1 << 0),
	_FLAG_STARTED       = (1 << 1),
	_FLAG_KEY_QUIT      = (1 << 2),
	_FLAG_FINDING_QUERY = (1 << 3),
	_FLAG_FINDING_FIND  = (1 << 4),
	_FLAG_COMMAND       = (1 << 5),
};

enum {
	_EVENT_KBD = 0,
	_EVENT_TIMER,

	_EVENT_END,
};


static int  _set_signal_handler(void);
static void _signal_handler(int sig);
static int  _timerfd_init(time_t timeout_s);

static void _set_playlist(Moedance *m);

static int  _event_loop(Moedance *m);
static void _event_kbd_handler(Moedance *m, int fd);
static void _event_timerfd_handler(Moedance *m, int fd);

static void _tui_refresh(Moedance *m);
static void _tui_quit_dialog(Moedance *m);
static void _tui_loading_dialog(Moedance *m);
static void _tui_error_dialog(Moedance *m);
static void _tui_playlist_find_begin(Moedance *m);
static void _tui_playlist_find_end(Moedance *m);
static void _tui_command_begin(Moedance *m);
static void _tui_command_end(Moedance *m, int set_footer);
static void _handle_command(Moedance *m);
static int  _handle_command_sleep(Moedance *m, Cmd *cmd);

static void _player_play(Moedance *m);
static void _player_stop(Moedance *m);
static void _player_toggle(Moedance *m);
static void _player_next(Moedance *m);
static void _player_prev(Moedance *m);
static void _player_error(Moedance *m);


// TODO: avoid global variables
static Moedance *_moe = NULL;


/*
 * public
 */
int
moedance_init(Moedance *m, const char root_dir[])
{
	if (mtx_init(&m->mutex, mtx_plain) != 0) {
		fprintf(stderr, "moedance: moedance_init: mtx_init: failed\n");
		return -1;
	}

	if (playlist_init(&m->playlist, root_dir) < 0) {
		fprintf(stderr, "moedance: moedance_init: playlist_init: \"%s\": %s\n", root_dir, strerror(errno));
		mtx_destroy(&m->mutex);
		return -1;
	}

	m->flags = 0;
	m->root_dir = root_dir;
	m->sleep_s = 0;
	_moe = m;
	return 0;
}


void
moedance_deinit(Moedance *m)
{
	playlist_deinit(&m->playlist);
	mtx_destroy(&m->mutex);
}


int
moedance_run(Moedance *m)
{
	if (log_file_init(CFG_LOG_FILE) < 0)
		return -1;

	int ret = tui_init(&m->tui, m->root_dir);
	if (ret < 0)
		goto out0;

	ret = _set_signal_handler();
	if (ret < 0)
		goto out1;

	tui_draw(&m->tui);
	_set_playlist(m);

	ret = player_init(&m->player);
	if (ret < 0)
		goto out2;

	ret = _event_loop(m);

out2:
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


	const char *ctx;
	if (sigaction(SIGQUIT, &act, NULL) < 0) {
		ctx = "SIGQUIT";
		goto err0;
	}

	if (sigaction(SIGINT, &act, NULL) < 0) {
		ctx = "SIGINT";
		goto err0;
	}

	if (sigaction(SIGWINCH, &act, NULL) < 0) {
		ctx = "SIGWINCH";
		goto err0;
	}

	return 0;

err0:
	log_err(errno, "moedance: _set_signal_handler: sigaction: %s", ctx);
	return -1;
}


static void
_signal_handler(int sig)
{
	switch (sig) {
	case SIGWINCH:
		_tui_refresh(_moe);
		break;
	case SIGHUP:
	case SIGINT:
	case SIGQUIT:
		UNSET(_moe->flags, _FLAG_ALIVE);
		break;
	default:
		log_err(0, "moedance: _event_handle_signal: invalid signal: %d", sig);
		break;
	}
}


static int
_timerfd_init(time_t timeout_s)
{
	const int fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
	if (fd < 0) {
		log_err(errno, "moedance: _timerfd_init: timerfd_create");
		return -1;
	}

	const struct itimerspec tms = {
		.it_value = (struct timespec) { .tv_sec = timeout_s },
		.it_interval = (struct timespec) { .tv_sec = timeout_s },
	};

	if (timerfd_settime(fd, 0, &tms, NULL) < 0) {
		log_err(errno, "moedance: _timerfd_init: timerfd_settime");
		close(fd);
		return -1;
	}

	return fd;
}


static void
_set_playlist(Moedance *m)
{
	_tui_loading_dialog(m);

	const PlaylistItem **items;
	const int items_len = playlist_load(&m->playlist, &items);
	if (items_len < 0)
		tui_show_dialog(&m->tui, "Failed to load file(s) from the given dir!", TUI_DIALOG_TYPE_ERROR);
	else
		tui_set_playlist(&m->tui, items, items_len);
}


static int
_event_loop(Moedance *m)
{
	int ret = -1;
	struct pollfd pfds[_EVENT_END];

	const int tfd = _timerfd_init(_TIMER_VALUE_S);
	if (tfd < 0)
		return -1;

	pfds[_EVENT_KBD].fd = STDIN_FILENO;
	pfds[_EVENT_KBD].events = POLLIN;
	pfds[_EVENT_TIMER].fd = tfd;
	pfds[_EVENT_TIMER].events = POLLIN;


	/* flush input buffer */
	stream_in_flush(pfds[_EVENT_KBD].fd);

	SET(m->flags, _FLAG_ALIVE);
	while (ISSET(m->flags, _FLAG_ALIVE)) {
		int ret = poll(pfds, LEN(pfds), -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;

			log_err(errno, "moedance: _event_loop: poll");
			goto out0;
		}

		for (int i = 0; i < _EVENT_END; i++) {
			const short int rv = pfds[i].revents;
			if (ISSET(rv, POLLHUP | POLLERR)) {
				const char *revents_str = "???";
				if (ISSET(rv, POLLHUP))
					revents_str = "POLLHUP";
				else if (ISSET(rv, POLLERR))
					revents_str = "POLLERR";

				log_info("moedance: _event_loop: revents: %s", revents_str);
				goto out0;
			}

			if (ISSET(rv, POLLIN) == 0)
				continue;

			switch (i) {
			case _EVENT_KBD:
				_event_kbd_handler(m, pfds[i].fd);
				break;
			case _EVENT_TIMER:
				_event_timerfd_handler(m, pfds[i].fd);
				break;
			}
		}
	}

	tui_show_dialog(&m->tui, "Please wait...", TUI_DIALOG_TYPE_INFO);
	ret = 0;

out0:
	close(tfd);
	UNSET(m->flags, _FLAG_ALIVE);
	return ret;
}


static void
_event_kbd_handler(Moedance *m, int fd)
{
	char buffer[32];
	const ssize_t rd = read(fd, buffer, sizeof(buffer));
	if (rd < 0) {
		log_err(errno, "moedance: _event_kbd_handler: read");
		return;
	}

	if (!ISSET(m->flags, _FLAG_ALIVE))
		return;

	const int kbd = kbd_parse(buffer, (int)rd);
	if (ISSET(m->flags, _FLAG_FINDING_QUERY)) {
		switch (kbd) {
		case KBD_BACKSPACE:
			// TODO: handle UTF-8
			tui_playlist_find_query(&m->tui, buffer, -1);
			break;
		case KBD_ESCAPE:
			_tui_playlist_find_end(m);
			return;
		case KBD_ENTER:
			SET(m->flags, _FLAG_FINDING_FIND);
			UNSET(m->flags, _FLAG_FINDING_QUERY);
			tui_show_cursor(&m->tui, 0);
			tui_playlist_find_next(&m->tui);
			return;
		}

		// TODO: handle UTF-8
		if (is_ascii(buffer[0]) == 0)
			return;

		tui_playlist_find_query(&m->tui, buffer, (int)rd);
		return;
	}

	if (ISSET(m->flags, _FLAG_FINDING_FIND)) {
		switch (kbd) {
		case KBD_Q:
		case KBD_ESCAPE:
			_tui_playlist_find_end(m);
			break;
		case KBD_N:
			tui_playlist_find_next(&m->tui);
			break;
		case KBD_P:
			tui_playlist_find_prev(&m->tui);
			break;
		//case KBD_ENTER:
		//	_player_play(m);
		//	break;
		case KBD_SLASH:
			SET(m->flags, _FLAG_FINDING_QUERY);
			UNSET(m->flags, _FLAG_FINDING_FIND);
			tui_playlist_find_query_clear(&m->tui);
			tui_show_cursor(&m->tui, 1);
			break;
		}

		return;
	}

	if (ISSET(m->flags, _FLAG_COMMAND)) {
		switch (kbd) {
		case KBD_BACKSPACE:
			tui_command_query(&m->tui, buffer, -1);
			break;
		case KBD_ESCAPE:
			_tui_command_end(m, 1);
			break;
		case KBD_ENTER:
			_handle_command(m);
			return;
		}

		if (is_ascii(buffer[0]) == 0)
			return;

		tui_command_query(&m->tui, buffer, (int)rd);
		return;
	}

	if (ISSET(m->flags, _FLAG_KEY_QUIT)) {
		if (kbd == KBD_Y) {
			UNSET(m->flags, _FLAG_ALIVE);
			return;
		}

		UNSET(m->flags, _FLAG_KEY_QUIT);
		tui_show_dialog(&m->tui, NULL, TUI_DIALOG_TYPE_INFO);
		return;
	}

	switch (kbd) {
	case KBD_ARROW_UP: tui_playlist_cursor_up(&m->tui); break;
	case KBD_ARROW_DOWN: tui_playlist_cursor_down(&m->tui); break;
	case KBD_HOME: tui_playlist_top(&m->tui); break;
	case KBD_END: tui_playlist_bottom(&m->tui); break;
	case KBD_PAGE_UP: tui_playlist_page_up(&m->tui); break;
	case KBD_PAGE_DOWN: tui_playlist_page_down(&m->tui); break;
	case KBD_SLASH: _tui_playlist_find_begin(m); break;
	case KBD_SPACE: _player_toggle(m); break;
	case KBD_ENTER: _player_play(m); break;
	case KBD_COLON: _tui_command_begin(m);  break;
	case KBD_C: tui_playlist_curr(&m->tui); break;
	case KBD_N: _player_next(m); break;
	case KBD_P: _player_prev(m); break;
	case KBD_S: _player_stop(m); break;
	case KBD_Q:
		SET(m->flags, _FLAG_KEY_QUIT);
		_tui_quit_dialog(m);
		return;
	}
}


static void
_event_timerfd_handler(Moedance *m, int fd)
{
	uint64_t timer = 0;
	const ssize_t rd = read(fd, &timer, sizeof(timer));
	if (rd < 0) {
		log_err(0, "moedance: _event_timerfd_handler: read");
		return;
	}

	if (rd != sizeof(timer)) {
		log_err(0, "moedance: _event_timerfd_handler: read: invalid size [%zu:%zu]", rd, sizeof(timer));
		return;
	}

	if (ISSET(m->flags, _FLAG_STARTED)) {
		if (player_item_is_stopped(&m->player)) {
			tui_playlist_stop(&m->tui);
			_player_next(m);
		}

		tui_set_duration(&m->tui, player_item_get_time(&m->player));
	}

	if (ISSET(m->flags, _FLAG_KEY_QUIT))
		_tui_quit_dialog(m);

	if (m->sleep_s > 0) {
		m->sleep_s--;

		tui_set_sleep_duration(&m->tui, m->sleep_s);
		if (m->sleep_s > 0)
			return;

		_player_toggle(m);
	}
}


static void
_tui_refresh(Moedance *m)
{
	mtx_lock(&m->mutex); /* LOCK */

	tui_draw(&m->tui);
	if (ISSET(m->flags, _FLAG_KEY_QUIT))
		_tui_quit_dialog(m);

	mtx_unlock(&m->mutex); /* UNLOCK */
}


static void
_tui_quit_dialog(Moedance *m)
{
	tui_show_dialog(&m->tui, "Quit? (y)", TUI_DIALOG_TYPE_QUESTION);
}


static void
_tui_loading_dialog(Moedance *m)
{
	tui_show_dialog(&m->tui, "Loading...", TUI_DIALOG_TYPE_INFO);
}


static void
_tui_error_dialog(Moedance *m)
{
	tui_show_dialog(&m->tui, "Error: see log file.", TUI_DIALOG_TYPE_ERROR);
}


static void
_tui_playlist_find_begin(Moedance *m)
{
	if (m->playlist.items_len == 0)
		return;

	SET(m->flags, _FLAG_FINDING_QUERY);
	tui_playlist_find_begin(&m->tui);
	tui_show_cursor(&m->tui, 1);
}


static void
_tui_playlist_find_end(Moedance *m)
{
	UNSET(m->flags, _FLAG_FINDING_QUERY);
	UNSET(m->flags, _FLAG_FINDING_FIND);
	tui_playlist_find_end(&m->tui);
	tui_show_cursor(&m->tui, 0);
}


static void
_tui_command_begin(Moedance *m)
{
	SET(m->flags, _FLAG_COMMAND);
	tui_command_begin(&m->tui);
	tui_show_cursor(&m->tui, 1);
}


static void
_tui_command_end(Moedance *m, int set_footer)
{
	UNSET(m->flags, _FLAG_COMMAND);
	tui_command_end(&m->tui, set_footer);
	tui_show_cursor(&m->tui, 0);
}


static void
_handle_command(Moedance *m)
{
	Cmd cmd;
	cmd_parse_query(&cmd, tui_command_query_get(&m->tui));

	int ret = -1;
	switch (cmd.type) {
	case CMD_TYPE_EMPTY:
		break;
	case CMD_TYPE_SLEEP:
		if (player_item_is_playing(&m->player) == 0) {
			tui_show_dialog(&m->tui, "Please play something.", TUI_DIALOG_TYPE_INFO);
			_tui_command_end(m, 0);
			return;
		}

		ret = _handle_command_sleep(m, &cmd);
		break;
	case CMD_TYPE_QUIT:
		UNSET(m->flags, _FLAG_ALIVE);
		break;
	}

	int set_footer = 0;
	switch (ret) {
	case -1:
		tui_show_dialog(&m->tui, "Unknown command!", TUI_DIALOG_TYPE_ERROR);
		break;
	case -2:
		tui_show_dialog(&m->tui, "Invalid argument!", TUI_DIALOG_TYPE_ERROR);
		break;
	case -3:
		_tui_error_dialog(m);
		break;
	default:
		set_footer = 1;
		break;
	}

	_tui_command_end(m, set_footer);
}


static int
_handle_command_sleep(Moedance *m, Cmd *cmd)
{
	if (cmd->args_len == 0)
		return -2;

	char buffer[32];
	SpaceTokenizer *const st = &cmd->args[0];
	if (st->len >= LEN(buffer))
		return -2;

	cstr_copy_n(buffer, LEN(buffer), st->value, st->len);
	if (strcmp(buffer, "cancel") == 0) {
		m->sleep_s = 0;
		tui_set_sleep_duration(&m->tui, m->sleep_s);
		return 0;
	}

	int mul;
	const char suffix = buffer[st->len - 1];
	switch (suffix) {
	case 's':
		mul = 1;
		break;
	case 'm':
		mul = 60;
		break;
	case 'h':
		mul = 3600;
		break;
	default:
		return -2;
	}

	int64_t val = 0;
	buffer[st->len - 1] = '\0';
	if (cstr_to_int64(buffer, &val) < 0) {
		log_err(errno, "moedance: _handle_command_sleep: cstr_to_int64: invalid value");
		return -3;
	}

	if (val <= 0)
		return -2;

	int64_t sleep_value;
	if (__builtin_mul_overflow(val, mul, &sleep_value)) {
		log_err(ERANGE, "moedance: _handle_command_sleep: __builtin_mul_overflow: value too big");
		return -3;
	}

	m->sleep_s = sleep_value;
	return 0;
}


static void
_player_play(Moedance *m)
{
	const PlaylistItem *const item = tui_playlist_play(&m->tui);
	if (item == NULL) {
		_player_stop(m);
		return;
	}

	if (player_item_play(&m->player, item->file_path) < 0) {
		_player_error(m);
		return;
	}

	SET(m->flags, _FLAG_STARTED);
}


static void
_player_stop(Moedance *m)
{
	player_item_stop(&m->player);
	tui_playlist_stop(&m->tui);
	UNSET(m->flags, _FLAG_STARTED);
}


static void
_player_toggle(Moedance *m)
{
	const PlaylistItem *const item = tui_playlist_toggle(&m->tui);
	if (item == NULL) {
		_player_stop(m);
		return;
	}

	if (ISSET(m->flags, _FLAG_STARTED) == 0) {
		if (player_item_play(&m->player, item->file_path) < 0) {
			_player_error(m);
			return;
		}

		SET(m->flags, _FLAG_STARTED);
		return;
	}

	player_item_toggle(&m->player);
}


static void
_player_next(Moedance *m)
{
	const PlaylistItem *const item = tui_playlist_next(&m->tui);
	if (item == NULL) {
		_player_stop(m);
		return;
	}
	
	if (player_item_play(&m->player, item->file_path) < 0) {
		_player_error(m);
		return;
	}

	SET(m->flags, _FLAG_STARTED);
}


static void
_player_prev(Moedance *m)
{
	const PlaylistItem *const item = tui_playlist_prev(&m->tui);
	if (item == NULL) {
		_player_stop(m);
		return;
	}

	if (player_item_play(&m->player, item->file_path) < 0) {
		_player_error(m);
		return;
	}

	SET(m->flags, _FLAG_STARTED);
}


static void
_player_error(Moedance *m)
{
	UNSET(m->flags, _FLAG_STARTED);
	tui_playlist_stop(&m->tui);
	_tui_error_dialog(m);
}
