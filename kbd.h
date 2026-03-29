#ifndef __KBD_H__
#define __KBD_H__


enum {
	KBD_ARROW_UP,
	KBD_ARROW_DOWN,
	KBD_ARROW_RIGHT,
	KBD_ARROW_LEFT,
	KBD_PAGE_UP,
	KBD_PAGE_DOWN,
	KBD_HOME,
	KBD_END,
	KBD_ESCAPE,
	KBD_ENTER,
	KBD_BACKSPACE,
	KBD_SPACE,
	KBD_SLASH,
	KBD_C,
	KBD_N,
	KBD_P,
	KBD_Q,
	KBD_S,
	KBD_Y,

	KBD_UNKNOWN,
};


int kbd_parse(const char raw[], int len);


#endif

