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

typedef struct tui_playlist {
	int                  state;
	int                  top;
	int                  curr;
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
	int          tty_fd;
	TermIOS      termios_orig;
} Tui;


int  tui_init(Tui *t, const char root_dir[]);
void tui_deinit(Tui *t);
void tui_draw(Tui *t);

void tui_show_dialog(Tui *t, const char message[], TuiDialogType type);

void tui_set_playlist(Tui *t, const PlaylistItem *items[], int len);
void tui_set_duration(Tui *t, int64_t duration);

void tui_playlist_cursor_up(Tui *t);
void tui_playlist_cursor_down(Tui *t);
void tui_playlist_page_up(Tui *t);
void tui_playlist_page_down(Tui *t);
void tui_playlist_top(Tui *t);
void tui_playlist_bottom(Tui *t);
void tui_playlist_find(Tui *t);

const PlaylistItem *tui_playlist_play(Tui *t);
const PlaylistItem *tui_playlist_stop(Tui *t);
const PlaylistItem *tui_playlist_toggle(Tui *t);
const PlaylistItem *tui_playlist_next(Tui *t);
const PlaylistItem *tui_playlist_prev(Tui *t);


#endif

