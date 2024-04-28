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

#include <sys/stat.h>
#include <sys/signalfd.h>

#include "moedance.h"
#include "config.h"


#define CTRL_KEY(C) ((C) & 0x1f)


enum {
	_FLAG_ALIVE        = (1 << 0),
	_FLAG_READY        = (1 << 1),
	_FLAG_QUIT         = (1 << 2),
	_FLAG_PLAYLIST_TOP = (1 << 3),
};


static void _set_playlist(MoeDance *m);

static int  _set_signal_handler(MoeDance *m);
static int  _event_loop(MoeDance *m);
static void _event_handle_kbd(MoeDance *m, int fd);
static void _event_handle_kbd_escape(MoeDance *m, const char keys[], int len);
static void _event_handle_kbd_normal(MoeDance *m, char key);
static void _event_handle_signal(MoeDance *m, int fd);

static void _tui_quit_dialog(MoeDance *m);

static void _kbd_handle_key_stop(MoeDance *m);
static void _kbd_handle_key_enter(MoeDance *m);
static void _kbd_handle_key_toggle_play(MoeDance *m);
static void _kbd_handle_key_next(MoeDance *m);
static void _kbd_handle_key_prev(MoeDance *m);


/*
 * public
 */
void
moedance_init(MoeDance *m, const char root_dir[])
{
	m->flags = 0;
	m->root_dir = root_dir;
	m->poll_fds[MOEDANCE_FD_KBD].fd = STDIN_FILENO;
	m->poll_fds[MOEDANCE_FD_KBD].events = POLLIN;

	playlist_init(&m->playlist);
}


void
moedance_deinit(MoeDance *m)
{
	playlist_deinit(&m->playlist);
}


int
moedance_run(MoeDance *m)
{
	log_file_init(CFG_LOG_FILE);

	int ret = player_init(&m->player);
	if (ret < 0)
		goto out0;

	ret = tui_init(&m->tui, m->root_dir);
	if (ret < 0)
		goto out1;

	tui_draw(&m->tui);

	_set_playlist(m);

	ret = _event_loop(m);
	tui_deinit(&m->tui);

out1:
	player_deinit(&m->player);
out0:
	log_file_deinit();
	return ret;
}


/*
 * private
 */
static void
_set_playlist(MoeDance *m)
{
	tui_show_dialog(&m->tui, "Loading...");

	int items_len = 0;
	const PlaylistItem **const items = playlist_load(&m->playlist, m->root_dir, &items_len);
	if (items == NULL)
		tui_show_dialog(&m->tui, "Failed to load file(s) from the given root dir!");

	player_set_playlist(&m->player, items, items_len);
	tui_set_playlist(&m->tui, items, items_len);
}


static int
_set_signal_handler(MoeDance *m)
{
	int ret;
	sigset_t sigmask;


	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGWINCH);
	sigaddset(&sigmask, SIGHUP);
	sigaddset(&sigmask, SIGQUIT);
	sigaddset(&sigmask, SIGINT);
	sigaddset(&sigmask, SIGTERM);

	ret = sigprocmask(SIG_BLOCK, &sigmask, NULL);
	if (ret < 0) {
		log_err(errno, "moedance: _set_signal_handler: sigprocmask");
		return -1;
	}

	const int sigfd = signalfd(-1, &sigmask, SFD_NONBLOCK);
	if (sigfd < 0) {
		log_err(errno, "moedance: _set_signal_handler: signalfd");
		return -1;
	}

	m->poll_fds[MOEDANCE_FD_SIGNAL].fd = sigfd;
	m->poll_fds[MOEDANCE_FD_SIGNAL].events = POLLIN;
	return 0;
}


static int
_event_loop(MoeDance *m)
{
	int ret = _set_signal_handler(m);
	if (ret < 0)
		return -1;

	SET(m->flags, (_FLAG_ALIVE | _FLAG_READY));

	struct pollfd *const pfds = m->poll_fds;
	while (CHECK(m->flags, _FLAG_ALIVE)) {
		ret = poll(pfds, __MOEDANCE_FD_SIZE, 100);
		if (ret < 0) {
			log_err(errno, "moedance: _event_loop: poll");
			goto out0;
		}

		for (int i = 0; i < __MOEDANCE_FD_SIZE; i++) {
			const short int rv = pfds[i].revents;
			if (CHECK(rv, POLLHUP) || CHECK(rv, POLLERR)) {
				UNSET(m->flags, _FLAG_ALIVE);

				const char *revents_str = "";
				if (CHECK(rv, POLLHUP))
					revents_str = "POLLHUP";
				else if (CHECK(rv, POLLERR))
					revents_str = "POLLERR";

				log_info("moedance; _event_loop: revents: %s", revents_str);
				break;
			}

			if (CHECK(rv, POLLIN) == 0)
				continue;

			switch (i) {
			case MOEDANCE_FD_KBD:
				_event_handle_kbd(m, pfds[i].fd);
				break;
			case MOEDANCE_FD_SIGNAL:
				_event_handle_signal(m, pfds[i].fd);
				break;
			default:
				log_err(0, "moedance: _event_loop: invalid event: %d", i);
				UNSET(m->flags, _FLAG_ALIVE);
				ret = -1;
			}
		}
	}

out0:
	close(m->poll_fds[MOEDANCE_FD_SIGNAL].fd);
	return ret;
}


static void
_event_handle_kbd(MoeDance *m, int fd)
{
	char buffer[4096];
	const ssize_t rd = read(fd, buffer, sizeof(buffer));
	if (rd < 0) {
		log_err(errno, "moedance: _event_handle_kbd: read");
		return;
	}

	if (buffer[0] == '\x1b')
		_event_handle_kbd_escape(m, &buffer[1], (int)rd - 1);
	else
		_event_handle_kbd_normal(m, buffer[0]);
}


static void
_event_handle_kbd_escape(MoeDance *m, const char keys[], int len)
{
	if ((len <= 0) || (keys[0] != '['))
		return;

	if ((len == 3) && (keys[2] != '~'))
		return;

	char key;
	switch (keys[1]) {
	case 'A': key = 'k'; break;
	case 'B': key = 'j'; break;
	case '5': key = ('u' & 0x1f); break;	/* page up */
	case '6': key = ('d' & 0x1f); break;	/* page down */
	default: return;
	}

	_event_handle_kbd_normal(m, key);
}


static void
_event_handle_kbd_normal(MoeDance *m, char key)
{
	if (CHECK(m->flags, _FLAG_READY) == 0)
		return;

	if (CHECK(m->flags, _FLAG_QUIT)) {
		if (tolower(key) == 'y') {
			UNSET(m->flags, _FLAG_ALIVE);
			return;
		}

		UNSET(m->flags, _FLAG_QUIT);
		player_stop(&m->player);
		tui_show_dialog(&m->tui, NULL);
		return;
	}

	if (CHECK(m->flags, _FLAG_PLAYLIST_TOP)) {
		UNSET(m->flags, _FLAG_PLAYLIST_TOP);
		if (key == 'g') {
			tui_playlist_top(&m->tui);
			return;
		}
	}

	switch (key) {
	case 'q':
		SET(m->flags, _FLAG_QUIT);
		_tui_quit_dialog(m);
		break;
	case CTRL_KEY('u'): tui_playlist_page_up(&m->tui); break;
	case CTRL_KEY('d'): tui_playlist_page_down(&m->tui); break;
	case 'k': tui_playlist_cursor_up(&m->tui); break;
	case 'j': tui_playlist_cursor_down(&m->tui); break;
	case 'g': SET(m->flags, _FLAG_PLAYLIST_TOP); break;
	case 'G': tui_playlist_bottom(&m->tui); break;
	case 's': _kbd_handle_key_stop(m); break;
	case 13:  _kbd_handle_key_enter(m); break;
	case ' ': _kbd_handle_key_toggle_play(m); break;
	case 'n': _kbd_handle_key_next(m); break;
	case 'p': _kbd_handle_key_prev(m); break;
	}
}


static void
_event_handle_signal(MoeDance *m, int fd)
{
	struct signalfd_siginfo siginfo;
	if (read(fd, &siginfo, sizeof(siginfo)) < 0) {
		log_err(errno, "moedance: _event_handle_signal: read");
		siginfo.ssi_signo = SIGWINCH;
	}

	switch (siginfo.ssi_signo) {
	case SIGWINCH:
		tui_draw(&m->tui);
		if (CHECK(m->flags, _FLAG_QUIT))
			_tui_quit_dialog(m);
		break;
	case SIGHUP:
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		UNSET(m->flags, _FLAG_ALIVE);
		break;
	default:
		log_err(0, "moedance: _event_handle_signal: invalid signal");
		break;
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
	tui_playlist_play(&m->tui);
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
	tui_playlist_next(&m->tui);
}


static void
_kbd_handle_key_prev(MoeDance *m)
{
	tui_playlist_prev(&m->tui);
}

