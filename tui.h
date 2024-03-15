#ifndef __TUI_H__
#define __TUI_H__


#include <termios.h>
#include <unistd.h>
#include <time.h>

#include <sys/ioctl.h>

#include "util.h"


typedef struct {
	int            width;
	int            height;
	int            header_pos;
	int            body_pos;
	int            footer_pos;
	Str            str;
	char           str_buffer[4096];
	struct termios term_orig;
} Tui;

int  tui_init(Tui *t);
void tui_deinit(Tui *t);
int  tui_resize(Tui *t);
void tui_clear(void);
void tui_show_dialog(Tui *t, const char message[]);

void tui_header_set(Tui *t, const char dir_name[], int curr, int total);
void tui_body_set_item(Tui *t, const char name[], int64_t duration, int is_selected, int now_playing, int pos);
int  tui_body_get_items_len(const Tui *t);
void tui_footer_set(Tui *t, char ctrl, int num, int64_t dur_curr, int64_t dur_total, const char name[]);


#endif

