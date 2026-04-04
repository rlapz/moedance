#ifndef __TUI_H__
#define __TUI_H__


#include <termios.h>
#include <unistd.h>
#include <stdint.h>

#include "playlist.h"
#include "util.h"
#include "config.h"


typedef enum tui_dialog_type {
	TUI_DIALOG_TYPE_INFO,
	TUI_DIALOG_TYPE_QUESTION,
	TUI_DIALOG_TYPE_ERROR,
	TUI_DIALOG_TYPE_UNKNOWN,
} TuiDialogType;

typedef enum tui_repeat_type {
	TUI_REPEAT_TYPE_NONE,
	TUI_REPEAT_TYPE_ONE,
	TUI_REPEAT_TYPE_ALL,
} TuiRepeatType;

typedef struct tui_playlist {
	int                  state;
	TuiRepeatType        repeat;
	int                  top;
	int                  curr;
	int                  found;
	int                  find_state;
	int                  item_active;
	int                  item_selected;
	int64_t              item_duration;	/* min duration */
	const PlaylistItem **items;
	int                  items_len;
} TuiPlaylist;

typedef struct termios TermIOS;

typedef struct tui {
	int          state;
	int          width;
	int          height;
	int          header_pos;
	int          body_pos;
	int          footer_pos;
	const char  *root_dir;
	TuiPlaylist  playlist;
	Str          buffer;
	Str          input_buffer;
	int          tty_fd;
	TermIOS      termios_orig;
	int64_t      sleep_duration;
} Tui;


int  tui_init(Tui *t, const char root_dir[]);
void tui_deinit(Tui *t);
void tui_draw(Tui *t);

void tui_show_dialog(Tui *t, const char message[], TuiDialogType type);
void tui_show_cursor(Tui *t, int enable);

void tui_set_playlist(Tui *t, const PlaylistItem *items[], int len);
void tui_set_duration(Tui *t, int64_t duration);
void tui_set_sleep_duration(Tui *t, int64_t duration);
void tui_set_repeat(Tui *t, TuiRepeatType type);

void tui_playlist_cursor_up(Tui *t);
void tui_playlist_cursor_down(Tui *t);
void tui_playlist_page_up(Tui *t);
void tui_playlist_page_down(Tui *t);
void tui_playlist_top(Tui *t);
void tui_playlist_bottom(Tui *t);
void tui_playlist_curr(Tui *t);

void tui_playlist_find_begin(Tui *t);
void tui_playlist_find_query(Tui *t, const char query[], int len);
void tui_playlist_find_query_clear(Tui *t);
void tui_playlist_find_next(Tui *t);
void tui_playlist_find_prev(Tui *t);
void tui_playlist_find_end(Tui *t);

const PlaylistItem *tui_playlist_play(Tui *t);
const PlaylistItem *tui_playlist_play_repeat_one(Tui *t);
const PlaylistItem *tui_playlist_play_repeat_all(Tui *t);
const PlaylistItem *tui_playlist_stop(Tui *t);
const PlaylistItem *tui_playlist_toggle(Tui *t);
const PlaylistItem *tui_playlist_next(Tui *t);
const PlaylistItem *tui_playlist_prev(Tui *t);

void        tui_command_begin(Tui *t);
void        tui_command_query(Tui *t, const char query[], int len);
const char *tui_command_query_get(Tui *t);
void        tui_command_end(Tui *t, int set_footer);


#endif

