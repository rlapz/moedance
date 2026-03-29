#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <sys/ioctl.h>

#include "tui.h"
#include "util.h"
#include "config.h"


enum {
	_STATE_NORMAL,
	_STATE_FINDING,
};


enum {
	_FIND_STATE_FOUND,
	_FIND_STATE_NOT_FOUND,
	_FIND_STATE_END,
};


enum {
	_PLAYER_STATE_STOPPED,
	_PLAYER_STATE_PLAYING,
	_PLAYER_STATE_PAUSED,
};


static const char _player_state_chr[] = {
	[_PLAYER_STATE_STOPPED] = '#',
	[_PLAYER_STATE_PLAYING] = '>',
	[_PLAYER_STATE_PAUSED]  = '^',
};


static const char _dialog_type_chr[] = {
	[TUI_DIALOG_TYPE_INFO]     = 'i',
	[TUI_DIALOG_TYPE_QUESTION] = '?',
	[TUI_DIALOG_TYPE_ERROR]    = 'E',
	[TUI_DIALOG_TYPE_UNKNOWN]  = 'U',
};


static void _draw_begin(Tui *t);
static void _draw_end(Tui *t);
static void _clear(Tui *t);
static void _resize(Tui *t);
static int  _raw_mode(Tui *t);

static void _add_playlist_item(Tui *t, int idx, int pos);
static void _set_header(Tui *t);
static void _set_body(Tui *t);
static void _set_footer(Tui *t);
static int  _get_playlist_relative_len(const Tui *t);
static void _playlist_cursor(Tui *t, int step, int is_scroll);
static void _playlist_cursor_at(Tui *t, int idx);

static int  _playlist_cmp(const PlaylistItem *pl, const char query[]);


/*
 * public
 */
int
tui_init(Tui *t, const char root_dir[])
{
	int ret = str_init_alloc(&t->buffer, 4096);
	if (ret < 0) {
		log_err(ret, "tui: tui_init: str_init_alloc: buffer");
		return -1;
	}

	ret = str_init_alloc(&t->input_buffer, CFG_FINDING_QUERY_SIZE + 1);
	if (ret < 0) {
		log_err(ret, "tui: tui_init: str_init_alloc: input_buffer");
		goto err0;
	}

	const int tty_fd = open("/dev/tty", O_WRONLY);
	if (tty_fd < 0) {
		log_err(errno, "tui: tui_init: open: /dev/tty");
		goto err1;
	}

	t->tty_fd = tty_fd;
	if (_raw_mode(t) < 0)
		goto err2;

	t->state = _STATE_NORMAL;
	t->root_dir = root_dir;
	t->playlist.state = _PLAYER_STATE_STOPPED;
	t->playlist.top = 0;
	t->playlist.curr = 0;
	t->playlist.item_active = -1;
	t->playlist.item_selected = 0;
	t->playlist.item_duration = 0;
	t->playlist.items = NULL;
	t->playlist.items_len = 0;
	return 0;

err2:
	close(tty_fd);
err1:
	str_deinit(&t->input_buffer);
err0:
	str_deinit(&t->buffer);
	return -1;
}


void
tui_deinit(Tui *t)
{
	_draw_begin(t);
	_clear(t);
	Str *const str = &t->buffer;

	/* disable alternative buffer */
	str_append_n(str, "\x1b[?1049l", 8);

	/* restore screen */
	str_append_n(str, "\x1b[?47l", 6);

	/* restore cursor position */
	str_append_n(str, "\x1b[u", 3);

	/* show the cursor */
	str_append_n(str, "\x1b[?25h", 6);

	/* enable line wrap */
	str_append_n(str, "\x1b[?7h", 5);

	_draw_end(t);

	/* restore termios */
	if (tcsetattr(t->tty_fd, TCSAFLUSH, &t->termios_orig) < 0)
		log_err(errno, "tui: tui_deinit: tcsetattr");

	str_deinit(str);
	str_deinit(&t->input_buffer);

	close(t->tty_fd);
}


void
tui_draw(Tui *t)
{
	_resize(t);
	_draw_begin(t);
	_clear(t);
	_set_header(t);

	const int end = _get_playlist_relative_len(t);
	if (end <= 0)
		goto out0;

	int diff = t->playlist.curr - end;
	const int sum = t->playlist.top + end;
	const int len = t->playlist.items_len;
	if (diff >= 0) {
		t->playlist.curr = (end - 1);
		t->playlist.top += (diff + 1);
	} else if ((sum > len) && (t->playlist.top > 0)) {
		diff = (sum - (len - 1)) - 1;
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
tui_show_dialog(Tui *t, const char message[], TuiDialogType type)
{
	Str *const str = &t->buffer;
	if (message != NULL) {
		char _type;
		switch (type) {
		case TUI_DIALOG_TYPE_INFO: _type = _dialog_type_chr[TUI_DIALOG_TYPE_INFO]; break;
		case TUI_DIALOG_TYPE_QUESTION: _type = _dialog_type_chr[TUI_DIALOG_TYPE_QUESTION]; break;
		case TUI_DIALOG_TYPE_ERROR: _type = _dialog_type_chr[TUI_DIALOG_TYPE_ERROR]; break;
		default: _type = _dialog_type_chr[TUI_DIALOG_TYPE_UNKNOWN]; break;
		}

		str_set_fmt(str, "\x1b[%d;1H\x1b[1;" CFG_FOOTER_COLOR_FG ";" CFG_FOOTER_COLOR_BG
			    "m\x1b[K[%c] %s\x1b[m", t->footer_pos, _type, message);
	} else {
		_draw_begin(t);
		_set_footer(t);
	}

	_draw_end(t);
}


void
tui_show_cursor(Tui *t, int enable)
{
	_draw_begin(t);

	if (enable)
		str_append_n(&t->buffer, "\x1b[?25h", 6);
	else
		str_append_n(&t->buffer, "\x1b[?25l", 6);

	_draw_end(t);
}


void
tui_set_playlist(Tui *t, const PlaylistItem *items[], int len)
{
	if ((items == NULL) || (len <= 0)) {
		len = 0;
		items = NULL;
		goto out0;
	}

	t->playlist.item_active = 0;

out0:
	t->playlist.items = items;
	t->playlist.items_len = len;
	tui_draw(t);
}


void
tui_set_duration(Tui *t, int64_t duration)
{
	t->playlist.item_duration = duration;

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
		if ((t->playlist.top + t->playlist.curr) == (t->playlist.items_len - 1))
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
	if ((t->playlist.items_len <= 0) || (t->playlist.top == 0) || (end <= 0))
		return;

	int step = t->playlist.top;
	step = (step >= end)? end:step;
	_playlist_cursor(t, -step, 1);
}


void
tui_playlist_page_down(Tui *t)
{
	const int len = t->playlist.items_len;
	const int end = _get_playlist_relative_len(t);
	if ((len < end) || (end <= 0))
		return;

	int step = len - (t->playlist.top + end);
	step = (step >= end)? end:step;
	_playlist_cursor(t, step, 1);
}


void
tui_playlist_top(Tui *t)
{
	const int end = _get_playlist_relative_len(t);
	if ((t->playlist.items_len <= 0) || (end <= 0))
		return;

	t->playlist.curr = 0;
	t->playlist.top = 0;
	t->playlist.item_selected = 0;

	_draw_begin(t);
	_set_header(t);
	_set_body(t);
	_draw_end(t);
}


void
tui_playlist_bottom(Tui *t)
{
	int end = _get_playlist_relative_len(t);
	const int len = t->playlist.items_len;
	if ((end <= 0) || (len <= 0))
		return;

	end = (end > len)? len:end;
	t->playlist.curr = end - 1;
	t->playlist.top = len - end;
	t->playlist.item_selected = len - 1;

	_draw_begin(t);
	_set_header(t);
	_set_body(t);
	_draw_end(t);
}


void
tui_playlist_curr(Tui *t)
{
	const int idx = t->playlist.item_active;
	if (idx < 0)
		return;

	_draw_begin(t);
	_playlist_cursor_at(t, idx);
	_draw_end(t);
}


void
tui_playlist_find_begin(Tui *t)
{
	if (t->state != _STATE_NORMAL)
		return;

	t->state = _STATE_FINDING;
	t->playlist.found = -1;
	t->playlist.find_state = _FIND_STATE_FOUND;
	str_set_n(&t->input_buffer, NULL, 0);

	_draw_begin(t);
	_set_footer(t);
	_draw_end(t);
}


void
tui_playlist_find_query(Tui *t, const char query[], int len)
{
	if (len < 0) {
		// delete input buffer chars
		str_shrink(&t->input_buffer, (size_t)(-len));
	} else {
		if (t->input_buffer.len >= CFG_FINDING_QUERY_SIZE)
			return;

		str_append_n(&t->input_buffer, query, (size_t)len);
	}

	_draw_begin(t);
	_set_footer(t);
	_draw_end(t);
}


void
tui_playlist_find_query_clear(Tui *t)
{
	t->playlist.found = -1;
	t->playlist.find_state = _FIND_STATE_FOUND;
	str_set_n(&t->input_buffer, NULL, 0);

	_draw_begin(t);
	_set_footer(t);
	_draw_end(t);
}


void
tui_playlist_find_next(Tui *t)
{
	TuiPlaylist *const p = &t->playlist;
	const char *const query = t->input_buffer.cstr;

	int next = t->playlist.found;
	if (next >= t->playlist.items_len)
		return;

	next++;
	int idx = -1;
	for (int i = next; i < p->items_len; i++) {
		if (_playlist_cmp(p->items[i], query)) {
			idx = i;
			break;
		}
	}

	if (idx < 0) {
		if (p->found < 0)
			p->find_state = _FIND_STATE_NOT_FOUND;
		else
			p->find_state = _FIND_STATE_END;

		p->found = next - 1;
	} else {
		p->found = idx;
	}

	_draw_begin(t);
	_playlist_cursor_at(t, idx);
	_set_footer(t);
	_draw_end(t);
}


void
tui_playlist_find_prev(Tui *t)
{
	TuiPlaylist *const p = &t->playlist;
	const char *const query = t->input_buffer.cstr;

	int prev = t->playlist.found;
	if (prev <= 0)
		return;

	prev--;
	int idx = -1;
	for (int i = prev; i >= 0; i--) {
		if (_playlist_cmp(p->items[i], query)) {
			idx = i;
			break;
		}
	}

	if (idx < 0) {
		p->found = prev + 1;
	} else {
		p->find_state = _FIND_STATE_FOUND;
		p->found = idx;
	}

	_draw_begin(t);
	_playlist_cursor_at(t, idx);
	_set_footer(t);
	_draw_end(t);
}


void
tui_playlist_find_end(Tui *t)
{
	t->state = _STATE_NORMAL;
	t->playlist.found = -1;
	t->playlist.find_state = _FIND_STATE_FOUND;
	str_set_n(&t->input_buffer, NULL, 0);

	_draw_begin(t);
	_set_footer(t);
	_draw_end(t);
}


const PlaylistItem *
tui_playlist_play(Tui *t)
{
	const PlaylistItem *ret;
	const int idx = t->playlist.item_active;
	if ((idx < 0) || (t->playlist.items_len <= 0)) {
		t->playlist.state = _PLAYER_STATE_STOPPED;
		ret = NULL;
	} else {
		const int sel_idx = t->playlist.curr + t->playlist.top;
		t->playlist.item_active = sel_idx;
		t->playlist.item_duration = 0;
		t->playlist.state = _PLAYER_STATE_PLAYING;
		ret = t->playlist.items[sel_idx];
	}

	_draw_begin(t);
	_set_body(t);
	_set_footer(t);
	_draw_end(t);
	return ret;
}


const PlaylistItem *
tui_playlist_stop(Tui *t)
{
	const PlaylistItem *ret;
	const int idx = t->playlist.item_active;
	if ((idx < 0) || (t->playlist.items_len <= 0)) {
		t->playlist.state = _PLAYER_STATE_STOPPED;
		ret = NULL;
	} else {
		t->playlist.state = _PLAYER_STATE_STOPPED;
		t->playlist.item_duration = 0;
		ret = t->playlist.items[idx];
	}

	_draw_begin(t);
	_set_footer(t);
	_draw_end(t);
	return ret;
}


const PlaylistItem *
tui_playlist_toggle(Tui *t)
{
	const PlaylistItem *ret;
	const int idx = t->playlist.item_active;
	if ((idx < 0) || (t->playlist.items_len <= 0)) {
		t->playlist.state = _PLAYER_STATE_STOPPED;
		ret = NULL;
	} else {
		switch (t->playlist.state) {
		case _PLAYER_STATE_PAUSED:
		case _PLAYER_STATE_STOPPED:
			t->playlist.state = _PLAYER_STATE_PLAYING;
			break;
		case _PLAYER_STATE_PLAYING:
			t->playlist.state = _PLAYER_STATE_PAUSED;
			break;
		default:
			t->playlist.state = _PLAYER_STATE_STOPPED;
			break;
		}

		ret = t->playlist.items[idx];
	}

	_draw_begin(t);
	_set_footer(t);
	_draw_end(t);
	return ret;
}


const PlaylistItem *
tui_playlist_next(Tui *t)
{
	const PlaylistItem *ret;
	const int idx = t->playlist.item_active;
	const int len = t->playlist.items_len;
	if ((idx < 0) || ((idx + 1) >= len) || (len <= 0)) {
		t->playlist.state = _PLAYER_STATE_STOPPED;
		ret = NULL;
	} else {
		t->playlist.state = _PLAYER_STATE_PLAYING;
		t->playlist.item_active = idx + 1;
		ret = t->playlist.items[idx + 1];
	}

	_draw_begin(t);
	_set_body(t);
	_set_footer(t);
	_draw_end(t);
	return ret;
}


const PlaylistItem *
tui_playlist_prev(Tui *t)
{
	const PlaylistItem *ret;
	const int idx = t->playlist.item_active;
	if (((idx - 1) < 0) || (t->playlist.items_len <= 0)) {
		t->playlist.state = _PLAYER_STATE_STOPPED;
		ret = NULL;
	} else {
		t->playlist.state = _PLAYER_STATE_PLAYING;
		t->playlist.item_active = idx - 1;
		ret = t->playlist.items[idx - 1];
	}

	_draw_begin(t);
	_set_body(t);
	_set_footer(t);
	_draw_end(t);
	return ret;
}


/*
 * private
 */
static inline void
_draw_begin(Tui *t)
{
	str_set_n(&t->buffer, NULL, 0);
}


static void
_draw_end(Tui *t)
{
	const size_t len = t->buffer.len;
	const char *const cstr = t->buffer.cstr;
	for (size_t i = 0; i < len;) {
		const ssize_t w = write(t->tty_fd, cstr + i, len - i);
		if (w < 0) {
			log_err(errno, "tui: _draw_end: write");
			return;
		}

		if (w == 0)
			break;

		i += (size_t)w;
	}
}


static void
_clear(Tui *t)
{
	str_append_n(&t->buffer, "\x1b[2J", 4);
}


static void
_resize(Tui *t)
{
	struct winsize ws;
	if (ioctl(t->tty_fd, TIOCGWINSZ, &ws) < 0) {
		log_err(errno, "tui: _resize: ioctl");
		return;
	}

	t->width = ws.ws_col;
	t->height = ws.ws_row;
	t->header_pos = 1;
	t->body_pos = t->header_pos + 2;
	t->footer_pos = t->height;
}


static int
_raw_mode(Tui *t)
{
	/* backup termios */
	if (tcgetattr(t->tty_fd, &t->termios_orig) < 0) {
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
	if (tcsetattr(t->tty_fd, TCSAFLUSH, &termios) < 0) {
		log_err(errno, "tui: _raw_mode: tcsetattr");
		return -1;
	}

	Str *const str = &t->buffer;
	_draw_begin(t);

	/* save cursor position */
	str_append_n(str, "\x1b[s", 3);

	/* save current screen */
	str_append_n(str, "\x1b[?47h", 6);

	/* enable alternative buffer */
	str_append_n(str, "\x1b[?1049h", 8);

	/* hide the cursor */
	str_append_n(str, "\x1b[?25l", 6);

	/* disable line wrap */
	str_append_n(str, "\x1b[?7l", 5);

	_draw_end(t);
	return 0;
}


static void
_add_playlist_item(Tui *t, int idx, int pos)
{
	char buff[64];
	Str *const str = &t->buffer;
	const PlaylistItem *const item = t->playlist.items[idx];
	const char *const duration = cstr_time_fmt(buff, sizeof(buff), item->duration);
	const int dpos = t->width - (int)strlen(duration) - 2;
	if (dpos < 0)
		return;

	pos += t->body_pos;
	str_append_fmt(str, "\x1b[%d;1H", pos);

	char flag = ' ';
	int is_bld = 0;
	if (idx == t->playlist.item_active) {
		flag = _player_state_chr[_PLAYER_STATE_PLAYING];
		is_bld = 1;
	}

	if (idx == t->playlist.item_selected)
		str_append_fmt(str, "\x1b[%d;" CFG_BODY_SEL_COLOR_FG ";" CFG_BODY_SEL_COLOR_BG "m\x1b[K", is_bld);
	else
		str_append_fmt(str, "\x1b[%d;" CFG_BODY_COLOR_FG ";" CFG_BODY_COLOR_BG "m\x1b[K", is_bld);

	str_append_fmt(str, "%c\x1b[K", flag);

	int col = 2;
#if (CFG_META_TITLE_ENABLE == 1)
	str_append_fmt(str, "\x1b[%d;%dH│ %s\x1b[K", pos, col, ALT_EMPTY(item->title, "-"));
	col += CFG_META_TITLE_WIDTH;
#endif
#if (CFG_META_ARTIST_ENABLE == 1)
	str_append_fmt(str, "\x1b[%d;%dH│ %s\x1b[K", pos, col, ALT_EMPTY(item->artist, "-"));
	col += CFG_META_ARTIST_WIDTH;
#endif
#if (CFG_META_ALBUM_ENABLE == 1)
	str_append_fmt(str, "\x1b[%d;%dH│ %s\x1b[K", pos, col, ALT_EMPTY(item->album, "-"));
	col += CFG_META_ALBUM_WIDTH;
#endif
#if (CFG_META_GENRE_ENABLE == 1)
	str_append_fmt(str, "\x1b[%d;%dH│ %s\x1b[K", pos, col, ALT_EMPTY(item->genre, "-"));
	col += CFG_META_GENRE_WIDTH;
#endif

	str_append_fmt(str, "\x1b[%d;%dH│ %s\x1b[K", pos, col, item->name);
	str_append_fmt(str, "\x1b[%d;%dH│ %s \x1b[m", pos, dpos, duration);
}


static void
_set_header(Tui *t)
{
	int curr = 0;
	const int len = t->playlist.items_len;
	if ((t->playlist.items != NULL) || (len > 0))
		curr = t->playlist.curr + t->playlist.top + 1;

	Str *const str = &t->buffer;
	const int clen = snprintf(NULL, 0, CFG_HEADER_LABEL " [%u/%u]", curr, len);
	const int cpos = t->width - clen;
	int dpos = t->width - (14 - 3);
	if (dpos < 0)
		dpos = 201;

	// row 0
	str_append_fmt(str, "\x1b[%d;1H\x1b[1;" CFG_HEADER_COLOR_FG ";" CFG_HEADER_COLOR_BG "m\x1b[K%s",
		       t->header_pos, t->root_dir);
	str_append_fmt(str, "\x1b[%d;%dH " CFG_HEADER_LABEL " [%u/%u]\x1b[m", t->header_pos, cpos, curr, len);

	// row 1
	const int pos = t->header_pos + 1;
	str_append_fmt(str, "\x1b[%d;1H\x1b[1;" CFG_HEADER_COLOR_FG ";" CFG_HEADER_COLOR_BG "m\x1b[K", pos);

	int col = 1;
#if (CFG_META_TITLE_ENABLE == 1)
	str_append_fmt(str, "\x1b[%d;%dH * %s\x1b[K", pos, col, "Title");
	col += CFG_META_TITLE_WIDTH;
#endif
#if (CFG_META_ARTIST_ENABLE == 1)
	str_append_fmt(str, "\x1b[%d;%dH * %s\x1b[K", pos, col, "Artist");
	col += CFG_META_ARTIST_WIDTH;
#endif
#if (CFG_META_ALBUM_ENABLE == 1)
	str_append_fmt(str, "\x1b[%d;%dH * %s\x1b[K", pos, col, "Album");
	col += CFG_META_ALBUM_WIDTH;
#endif
#if (CFG_META_GENRE_ENABLE == 1)
	str_append_fmt(str, "\x1b[%d;%dH * %s\x1b[K", pos, col, "Genre");
	col += CFG_META_GENRE_WIDTH;
#endif

	str_append_fmt(str, "\x1b[%d;%dH * %s\x1b[K", pos, col, "File");
	str_append_fmt(str, "\x1b[%d;%dH * %s\x1b[K\x1b[m", pos, dpos, "Duration");
}


static void
_set_body(Tui *t)
{
	int rlen = t->playlist.top + _get_playlist_relative_len(t);
	const int len = t->playlist.items_len;
	if (rlen > len)
		rlen -= (rlen - len);

	for (int i = t->playlist.top, pos = 0; i < rlen; i++, pos++)
		_add_playlist_item(t, i, pos);
}


static void
_set_footer(Tui *t)
{
	Str *const str = &t->buffer;
	if (t->playlist.items_len <= 0) {
		str_append_fmt(str, "\x1b[%d;1H\x1b[1;" CFG_FOOTER_COLOR_FG ";" CFG_FOOTER_COLOR_BG
			       "m\x1b[K> %s\x1b[m", t->footer_pos, "No Data!");
		return;
	}

	if (t->state == _STATE_FINDING) {
		const char *lbl = "Find";
		switch (t->playlist.find_state) {
		case _FIND_STATE_NOT_FOUND:
			lbl = "Find [Not Found]";
			break;
		case _FIND_STATE_END:
			lbl = "Find [End]";
			break;
		}

		str_append_fmt(str, "\x1b[%d;1H", t->footer_pos);
		str_append_fmt(str, "\x1b[1;" CFG_FOOTER_COLOR_FG ";" CFG_FOOTER_COLOR_BG
			       "m\x1b[K%s: %s", lbl, t->input_buffer.cstr);
		return;
	}

	char dur0[64];
	char dur1[64];
	const PlaylistItem *const item = t->playlist.items[t->playlist.item_active];
	const char *const d0 = cstr_time_fmt(dur0, sizeof(dur0), t->playlist.item_duration);
	const char *const d1 = cstr_time_fmt(dur1, sizeof(dur1), item->duration);
	const int dpos = t->width - snprintf(NULL, 0, "[%s - %s]", d0, d1);
	const char *name = strrchr(item->name, '/');
	if (name != NULL)
		name++;
	else
		name = item->name;

	str_append_fmt(str, "\x1b[%d;1H", t->footer_pos);
	str_append_fmt(str, "\x1b[1;" CFG_FOOTER_COLOR_FG ";" CFG_FOOTER_COLOR_BG "m\x1b[K[%c] %d. %s",
		       _player_state_chr[t->playlist.state], t->playlist.item_active + 1, name);
	str_append_fmt(str, "\x1b[%d;%dH [%s - %s]\x1b[m", t->footer_pos, dpos, d0, d1);
}


static inline int
_get_playlist_relative_len(const Tui *t)
{
	return t->footer_pos - 3;
}


static void
_playlist_cursor(Tui *t, int step, int is_scroll)
{
	const int idx = t->playlist.curr + t->playlist.top;
	if ((step > 0) && (idx >= t->playlist.items_len - 1))
		return;

	if (is_scroll)
		t->playlist.top += step;
	else
		t->playlist.curr += step;

	t->playlist.item_selected = idx + step;

	_draw_begin(t);
	_set_header(t);
	_set_body(t);
	_draw_end(t);
}


static void
_playlist_cursor_at(Tui *t, int idx)
{
	if (idx < 0)
		return;

	const int end = _get_playlist_relative_len(t) - 1;
	// scroll down
	if (idx > (t->playlist.top + end))
		t->playlist.top += (idx - (t->playlist.top + end));

	// scroll up
	else if (idx < t->playlist.top)
		t->playlist.top = idx;

	t->playlist.curr = (idx - t->playlist.top);
	t->playlist.item_selected = idx;

	_set_header(t);
	_set_body(t);
}


static int
_playlist_cmp(const PlaylistItem *pl, const char query[])
{
	if (cstr_case_str(pl->title, query) != NULL)
		return 1;
	if (cstr_case_str(pl->artist, query) != NULL)
		return 1;
	if (cstr_case_str(pl->file_path, query) != NULL)
		return 1;

	return 0;
}
