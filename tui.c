#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>

#include <sys/ioctl.h>

#include "util.h"
#include "tui.h"
#include "config.h"


static int  _set_raw_mode(Tui *t);


/*
 * private
 */
static int
_set_raw_mode(Tui *t)
{
	if (tcgetattr(STDIN_FILENO, &t->term_orig) < 0)
		return -errno;

	struct termios raw = t->term_orig;
	UNSET(raw.c_lflag, (ECHO | ICANON | IEXTEN | ISIG));
	UNSET(raw.c_iflag, (BRKINT | ICRNL | INPCK | ISTRIP | IXON));
	UNSET(raw.c_oflag, OPOST);
	SET(raw.c_cflag, CS8);

	raw.c_cc[VMIN]  = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0)
		return -errno;

	Str *const str = &t->str;

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


/*
 * public
 */
int
tui_init(Tui *t)
{
	int ret = str_init(&t->str, t->str_buffer, sizeof(t->str_buffer));
	assert(ret == 0);

	ret = tui_resize(t);
	if (ret < 0)
		return ret;

	ret = _set_raw_mode(t);
	if (ret < 0)
		return ret;

	return 0;
}


void
tui_deinit(Tui *t)
{
	Str *const str = &t->str;

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

	const int ret = tcsetattr(STDIN_FILENO, TCSAFLUSH, &t->term_orig);
	assert(ret == 0);

	str_deinit(str);
}


int
tui_resize(Tui *t)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0)
		return -errno;

	t->width = ws.ws_col;
	t->height = ws.ws_row;
	t->header_pos = 1;
	t->body_pos = t->header_pos + 1;
	t->footer_pos = t->height;
	return 0;
}


void
tui_clear(void)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
}


void
tui_show_dialog(Tui *t, const char message[])
{
	Str *const str = &t->str;
	str_set_fmt(str, "\x1b[%d;1H\x1b[1;" CFG_FOOTER_COLOR_FG ";" CFG_FOOTER_COLOR_BG "m\x1b[K> %s\x1b[m",
		t->footer_pos, message);
	str_write_all(str, STDOUT_FILENO);
}


void
tui_header_set(Tui *t, const char dir_name[], int curr, int total)
{
	Str *const str = &t->str;
	const int cpos = t->width - snprintf(NULL, 0, CFG_HEADER_LABEL " [%d/%d]", curr, total);

	str_set_fmt(str, "\x1b[%d;1H\x1b[1;" CFG_HEADER_COLOR_FG ";" CFG_HEADER_COLOR_BG "m\x1b[K%s",
		    t->header_pos, dir_name);
	str_append_fmt(str, "\x1b[%d;%dH " CFG_HEADER_LABEL " [%d/%d]\x1b[m", t->header_pos, cpos, curr, total);
	str_write_all(str, STDOUT_FILENO);
}


void
tui_body_set_item(Tui *t, const char name[], int64_t duration, int is_selected, int now_playing, int pos)
{
	char dur[64];
	Str *const str = &t->str;
	const char *const _duration = cstr_time_fmt(dur, sizeof(dur), duration);
	const int dpos = t->width - (int)strlen(_duration);

	pos += t->body_pos;
	str_set_fmt(str, "\x1b[%d;1H", pos);
	if (is_selected) {
		str_append(str, "\x1b[" CFG_BODY_SEL_COLOR_FG ";" CFG_BODY_SEL_COLOR_BG "m\x1b[K");
	} else {
		if (now_playing)
			str_append(str, "\x1b[1;" CFG_BODY_COLOR_FG ";" CFG_BODY_COLOR_BG "m\x1b[K");
		else
			str_append(str, "\x1b[" CFG_BODY_COLOR_FG ";" CFG_BODY_COLOR_BG "m\x1b[K");
	}

	str_append_fmt(str, "%c%s", (now_playing == 0)? ' ':'>', name);
	str_append_fmt(str, "\x1b[%d;%dH %s \x1b[m", pos, dpos - 1, _duration);
	str_write_all(&t->str, STDOUT_FILENO);
}


int
tui_body_get_items_len(const Tui *t)
{
	return t->footer_pos - 2;
}


void
tui_footer_set(Tui *t, char ctrl, int num, int64_t dur_curr, int64_t dur_total, const char name[])
{
	char dur0[64];
	char dur1[64];
	Str *const str = &t->str;
	const char *const d0 = cstr_time_fmt(dur0, sizeof(dur0), dur_curr);
	const char *const d1 = cstr_time_fmt(dur1, sizeof(dur1), dur_total);

	const int dpos = t->width - snprintf(NULL, 0, "[%s - %s]", d0, d1);
	str_set_fmt(str, "\x1b[%d;1H", t->footer_pos);
	str_append_fmt(str, "\x1b[1;" CFG_FOOTER_COLOR_FG ";" CFG_FOOTER_COLOR_BG "m\x1b[K[%c] %d. %s",
		       ctrl, num, name);
	str_append_fmt(str, "\x1b[%d;%dH [%s - %s]\x1b[m", t->footer_pos, dpos, d0, d1);
	str_write_all(str, STDOUT_FILENO);
}

