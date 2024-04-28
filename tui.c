#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <sys/ioctl.h>

#include "tui.h"
#include "util.h"
#include "config.h"


static const char _player_state_chr[] = {
	[PLAYER_STATE_STOPPED] = '#',
	[PLAYER_STATE_PLAYING] = '>',
	[PLAYER_STATE_PAUSED]  = '^',
	[PLAYER_STATE_UNKNOWN] = '?',
};


static void _draw_begin(Tui *t);
static void _draw_end(Tui *t);
static void _clear(void);
static void _resize(Tui *t);
static int  _raw_mode(Tui *t);
static void _set_playlist(Tui *t, int idx, int pos);
static void _set_header(Tui *t);
static void _set_body(Tui *t);
static void _set_footer(Tui *t);
static int  _get_playlist_relative_len(const Tui *t);
static void _playlist_cursor(Tui *t, int step, int is_scroll);


/*
 * public
 */
int
tui_init(Tui *t, const char dir_name[])
{
	Str *const str = &t->str_buffer;
	int ret = str_init_alloc(str, 4096);
	if (ret < 0) {
		log_err(ret, "tui: tui_init: str_init_alloc");
		return -1;
	}

	if (_raw_mode(t) < 0)
		goto err0;

	t->dir_name = dir_name;
	t->playlist.state = PLAYER_STATE_STOPPED;
	t->playlist.top = 0;
	t->playlist.curr = 0;
	t->playlist.active = -1;
	t->playlist.duration = 0;
	t->playlist.items = NULL;
	t->playlist.len = 0;
	return 0;

err0:
	str_deinit(str);
	return -1;
}


void
tui_deinit(Tui *t)
{
	Str *const str = &t->str_buffer;

	// disable alternative buffer
	str_set_n(str, "\x1b[?1049l", 8);

	// restore screen
	str_append_n(str, "\x1b[?47l", 6);

	// restore cursor position
	str_append_n(str, "\x1b[u", 3);

	// show the cursor
	str_append_n(str, "\x1b[?25h", 6);

	// enable line wrap
	str_append_n(str, "\x1b[?7h", 5);

	// write all
	str_write_all(str, STDOUT_FILENO);

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &t->termios_orig);
	str_deinit(str);

	free(t->playlist.items);
}


void
tui_draw(Tui *t)
{
	_clear();
	_resize(t);
	_draw_begin(t);
	_set_header(t);

	const int end = _get_playlist_relative_len(t);
	if (end <= 0)
		goto out0;

	int diff = t->playlist.curr - end;
	const int sum = t->playlist.top + end;
	if (diff >= 0) {
		t->playlist.curr = (end - 1);
		t->playlist.top += (diff + 1);
	} else if ((sum > t->playlist.len) && (t->playlist.top > 0)) {
		diff = (sum - (t->playlist.len - 1)) - 1;
		if (diff > t->playlist.top)
			diff -= (diff - t->playlist.top);

		t->playlist.curr += diff;
		t->playlist.top -= diff;
	}

	_set_body(t);

out0:
	_set_footer(t);
	_draw_end(t);
}


void
tui_show_dialog(Tui *t, const char message[])
{
	Str *const str = &t->str_buffer;
	if (message != NULL) {
		str_set_fmt(str, "\x1b[%d;1H\x1b[1;" CFG_FOOTER_COLOR_FG ";" CFG_FOOTER_COLOR_BG "m\x1b[K> %s\x1b[m",
			    t->footer_pos, message);
	} else {
		_draw_begin(t);
		_set_footer(t);
	}

	_draw_end(t);
}


int
tui_set_playlist(Tui *t, const PlaylistItem *items[], int len)
{
	TuiPlaylistItem *_items = NULL;
	if (len <= 0) {
		len = 0;
		goto out0;
	}

	_items = malloc(sizeof(TuiPlaylistItem) * (size_t)len);
	if (_items == NULL) {
		log_err(errno, "tui: tui_set_playlist: malloc");
		tui_show_dialog(t, "Failed to load file(s) from the given root dir!");
		return -1;
	}

	for (int i = 0; i < len; i++) {
		_items[i].is_selected = 0;
		_items[i].now_playing = 0;
		_items[i].item = items[i];
	}

	_items[0].is_selected = 1;
	_items[0].now_playing = 1;
	t->playlist.active = 0;

out0:
	t->playlist.items = _items;
	t->playlist.len = len;
	tui_draw(t);
	return 0;
}


void
tui_set_duration(Tui *t, int64_t duration)
{
	t->playlist.duration = duration;

	_draw_begin(t);
	_set_footer(t);
	_draw_end(t);
}


void
tui_playlist_cursor_up(Tui *t)
{
	if (t->playlist.curr == 0) {
		if (t->playlist.top == 0)
			return;

		/* scroll up */
		t->playlist.top--;
		t->playlist.curr++;
	}

	_playlist_cursor(t, -1, 0);
}


void
tui_playlist_cursor_down(Tui *t)
{
	const int end = _get_playlist_relative_len(t) - 1;
	if (t->playlist.curr == end) {
		if ((t->playlist.top + t->playlist.curr) == (t->playlist.len - 1))
			return;

		/* scroll down */
		t->playlist.top++;
		t->playlist.curr--;
	}

	_playlist_cursor(t, 1, 0);
}


void
tui_playlist_page_up(Tui *t)
{
	const int end = _get_playlist_relative_len(t) - 1;
	if ((t->playlist.len <= 0) || (t->playlist.top == 0) || (end <= 0))
		return;

	int step = t->playlist.top;
	if (step >= end)
		step = end;

	_playlist_cursor(t, -step, 1);
}


void
tui_playlist_page_down(Tui *t)
{
	const int len = t->playlist.len;
	const int end = _get_playlist_relative_len(t);
	if ((len < end) || (end <= 0))
		return;

	int step = len - (t->playlist.top + end);
	if (step >= end)
		step = end;

	_playlist_cursor(t, step, 1);
}


void
tui_playlist_top(Tui *t)
{
	const int end = _get_playlist_relative_len(t);
	if ((t->playlist.len <= 0) || (end <= 0))
		return;

	const int idx = t->playlist.curr + t->playlist.top;
	t->playlist.items[idx].is_selected = 0;
	t->playlist.items[0].is_selected = 1;

	t->playlist.curr = 0;
	t->playlist.top = 0;

	_draw_begin(t);
	_set_header(t);
	_set_body(t);
	_draw_end(t);
}


void
tui_playlist_bottom(Tui *t)
{
	int end = _get_playlist_relative_len(t);
	const int len = t->playlist.len;
	if ((end <= 0) || (len <= 0))
		return;

	const int idx = t->playlist.curr + t->playlist.top;
	t->playlist.items[idx].is_selected = 0;
	t->playlist.items[len - 1].is_selected = 1;

	if (end > len)
		end = len;

	t->playlist.curr = end - 1;
	t->playlist.top = len - end;

	_draw_begin(t);
	_set_header(t);
	_set_body(t);
	_draw_end(t);
}


const PlaylistItem *
tui_playlist_play(Tui *t)
{
	if (t->playlist.len <= 0)
		return NULL;

	const int act_idx = t->playlist.active;
	if (act_idx >= 0)
		t->playlist.items[act_idx].now_playing = 0;

	const int sel_idx = t->playlist.curr + t->playlist.top;
	t->playlist.items[sel_idx].now_playing = 1;
	t->playlist.active = sel_idx;
	t->playlist.state = PLAYER_STATE_PLAYING;
	t->playlist.duration = 0;

	_draw_begin(t);
	_set_body(t);
	_set_footer(t);
	_draw_end(t);
	return t->playlist.items[sel_idx].item;
}


const PlaylistItem *
tui_playlist_pause(Tui *t)
{
	if (t->playlist.len <= 0)
		return NULL;

	const int act_idx = t->playlist.active;
	if (act_idx < 0)
		return NULL;

	t->playlist.state = PLAYER_STATE_PAUSED;

	_draw_begin(t);
	_set_footer(t);
	_draw_end(t);
	return t->playlist.items[act_idx].item;
}


const PlaylistItem *
tui_playlist_stop(Tui *t)
{
	const int act_idx = t->playlist.active;
	if ((t->playlist.len <= 0) || (act_idx < 0))
		return NULL;

	t->playlist.state = PLAYER_STATE_STOPPED;
	t->playlist.duration = 0;

	_draw_begin(t);
	_set_body(t);
	_set_footer(t);
	_draw_end(t);
	return t->playlist.items[act_idx].item;
}


const PlaylistItem *
tui_playlist_next(Tui *t)
{
	if (t->playlist.len <= 0)
		return NULL;

	int idx = t->playlist.active;
	if ((idx < 0) || ((idx + 1) >= t->playlist.len))
		goto out0;

	t->playlist.items[idx++].now_playing = 0;
	t->playlist.items[idx].now_playing = 1;
	t->playlist.active = idx;

	_draw_begin(t);
	_set_body(t);
	_set_footer(t);
	_draw_end(t);

out0:
	return t->playlist.items[idx].item;
}


const PlaylistItem *
tui_playlist_prev(Tui *t)
{
	if (t->playlist.len <= 0)
		return NULL;

	int idx = t->playlist.active;
	if ((idx < 0) || ((idx - 1) < 0))
		goto out0;

	t->playlist.items[idx--].now_playing = 0;
	t->playlist.items[idx].now_playing = 1;
	t->playlist.active = idx;

	_draw_begin(t);
	_set_body(t);
	_set_footer(t);
	_draw_end(t);

out0:
	return t->playlist.items[idx].item;
}


/*
 * private
 */
static inline void
_draw_begin(Tui *t)
{
	str_set_n(&t->str_buffer, NULL, 0);
}


static inline void
_draw_end(Tui *t)
{
	str_write_all(&t->str_buffer, STDOUT_FILENO);
}


static void
_clear(void)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
}


static void
_resize(Tui *t)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0)
		return;

	t->width = ws.ws_col;
	t->height = ws.ws_row;
	t->header_pos = 1;
	t->body_pos = t->header_pos + 1;
	t->footer_pos = t->height;
}


static int
_raw_mode(Tui *t)
{
	Str *const str = &t->str_buffer;
	if (tcgetattr(STDIN_FILENO, &t->termios_orig) < 0) {
		log_err(errno, "tui: _raw_mode: tcgetattr");
		return -1;
	}

	TermIOS termios = t->termios_orig;
	UNSET(termios.c_lflag, (ECHO | ICANON | IEXTEN | ISIG));
	UNSET(termios.c_iflag, (BRKINT | ICRNL | INPCK | ISTRIP | IXON));
	UNSET(termios.c_oflag, OPOST);
	SET(termios.c_cflag, CS8);

	termios.c_cc[VMIN]  = 0;
	termios.c_cc[VTIME] = 1;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios) < 0) {
		log_err(errno, "tui: _raw_mode: tcsetattr");
		return -1;
	}

	// save cursor position
	str_set_n(str, "\x1b[s", 3);

	// save current screen
	str_append_n(str, "\x1b[?47h", 6);

	// enable alternative buffer
	str_append_n(str, "\x1b[?1049h", 8);

	// hide the cursor
	str_append_n(str, "\x1b[?25l", 6);

	// disable line wrap
	str_append_n(str, "\x1b[?7l", 5);

	// write all
	str_write_all(str, STDOUT_FILENO);
	return 0;
}


static void
_set_playlist(Tui *t, int idx, int pos)
{
	char buff[64];
	Str *const str = &t->str_buffer;
	TuiPlaylistItem *const playlist = &t->playlist.items[idx];
	const PlaylistItem *const item = playlist->item;
	const char *const duration = cstr_time_fmt(buff, sizeof(buff), item->duration);
	const int dpos = t->width - (int)strlen(duration);

	pos += t->body_pos;
	str_append_fmt(str, "\x1b[%d;1H", pos);

	if (playlist->is_selected)
		str_append(str, "\x1b[" CFG_BODY_SEL_COLOR_FG ";" CFG_BODY_SEL_COLOR_BG "m\x1b[K");
	else if (playlist->now_playing)
		str_append(str, "\x1b[1;" CFG_BODY_COLOR_FG ";" CFG_BODY_COLOR_BG "m\x1b[K");
	else
		str_append(str, "\x1b[" CFG_BODY_COLOR_FG ";" CFG_BODY_COLOR_BG "m\x1b[K");

	const char flag = (playlist->now_playing == 0)? ' ' : '>';
	str_append_fmt(str, "%c%s", flag, item->name);
	str_append_fmt(str, "\x1b[%d;%dH %s \x1b[m", pos, dpos - 1, duration);
}


static void
_set_header(Tui *t)
{
	int curr = 0;
	if ((t->playlist.items != NULL) || (t->playlist.len > 0))
		curr = t->playlist.curr + t->playlist.top + 1;

	Str *const str = &t->str_buffer;
	const int total = t->playlist.len;
	const int clen = snprintf(NULL, 0, CFG_HEADER_LABEL " [%u/%u]", curr, total);
	const int cpos = t->width - clen;

	str_append_fmt(str, "\x1b[%d;1H\x1b[1;" CFG_HEADER_COLOR_FG ";" CFG_HEADER_COLOR_BG "m\x1b[K%s",
		       t->header_pos, t->dir_name);
	str_append_fmt(str, "\x1b[%d;%dH " CFG_HEADER_LABEL " [%u/%u]\x1b[m", t->header_pos, cpos, curr, total);
}


static void
_set_body(Tui *t)
{
	int len = t->playlist.top + _get_playlist_relative_len(t);
	if (len > t->playlist.len)
		len -= (len - t->playlist.len);

	int pos = 0;
	for (int i = t->playlist.top; i < len; i++)
		_set_playlist(t, i, pos++);
}


static void
_set_footer(Tui *t)
{
	Str *const str = &t->str_buffer;
	if (t->playlist.len <= 0) {
		str_append_fmt(str, "\x1b[%d;1H\x1b[1;" CFG_FOOTER_COLOR_FG ";" CFG_FOOTER_COLOR_BG
			       "m\x1b[K> %s\x1b[m", t->footer_pos, "No Data!");
		return;
	}

	char dur0[64];
	char dur1[64];
	const TuiPlaylistItem *const pl = &t->playlist.items[t->playlist.active];
	const char *const d0 = cstr_time_fmt(dur0, sizeof(dur0), t->playlist.duration);
	const char *const d1 = cstr_time_fmt(dur1, sizeof(dur1), pl->item->duration);
	const int dpos = t->width - snprintf(NULL, 0, "[%s - %s]", d0, d1);

	str_append_fmt(str, "\x1b[%d;1H", t->footer_pos);
	str_append_fmt(str, "\x1b[1;" CFG_FOOTER_COLOR_FG ";" CFG_FOOTER_COLOR_BG "m\x1b[K[%c] %d. %s",
		       _player_state_chr[t->playlist.state], t->playlist.active + 1, pl->item->name);
	str_append_fmt(str, "\x1b[%d;%dH [%s - %s]\x1b[m", t->footer_pos, dpos, d0, d1);
}


static inline int
_get_playlist_relative_len(const Tui *t)
{
	return t->footer_pos - 2;
}


static void
_playlist_cursor(Tui *t, int step, int is_scroll)
{
	const int idx = t->playlist.curr + t->playlist.top;
	if ((step > 0) && (idx >= t->playlist.len - 1))
		return;

	if (is_scroll)
		t->playlist.top += step;
	else
		t->playlist.curr += step;

	t->playlist.items[idx].is_selected = 0;
	t->playlist.items[idx + step].is_selected = 1;

	_draw_begin(t);
	_set_header(t);
	_set_body(t);
	_draw_end(t);
}

