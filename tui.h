#ifndef __TUI_H__
#define __TUI_H__


#include <termios.h>
#include <unistd.h>
#include <stdint.h>

#include "player.h"
#include "playlist.h"
#include "util.h"
#include "config.h"


typedef struct tui_playlist_item {
	int                 is_selected;
	int                 now_playing;
	const PlaylistItem *item;
} TuiPlaylistItem;

typedef struct tui_playlist {
	int              state;
	int              top;
	int              curr;
	int              active;
	int64_t          duration;	/* min duration */
	TuiPlaylistItem *items;
	int              len;
} TuiPlaylist;

typedef struct termios TermIOS;

typedef struct tui {
	int          width;
	int          height;
	int          header_pos;
	int          body_pos;
	int          footer_pos;
	const char  *dir_name;
	TuiPlaylist  playlist;
	Str          str_buffer;
	TermIOS      termios_orig;
} Tui;


int  tui_init(Tui *t, const char dir_name[]);
void tui_deinit(Tui *t);
void tui_draw(Tui *t);

void tui_show_dialog(Tui *t, const char message[]);

int  tui_set_playlist(Tui *t, const PlaylistItem *items[], int len);
void tui_set_duration(Tui *t, int64_t duration);

void tui_playlist_cursor_up(Tui *t);
void tui_playlist_cursor_down(Tui *t);
void tui_playlist_page_up(Tui *t);
void tui_playlist_page_down(Tui *t);
void tui_playlist_top(Tui *t);
void tui_playlist_bottom(Tui *t);

const PlaylistItem *tui_playlist_play(Tui *t);
const PlaylistItem *tui_playlist_pause(Tui *t);
const PlaylistItem *tui_playlist_stop(Tui *t);
const PlaylistItem *tui_playlist_next(Tui *t);
const PlaylistItem *tui_playlist_prev(Tui *t);


#endif

