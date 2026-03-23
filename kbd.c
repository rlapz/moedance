#include "kbd.h"


#define CTRL_KEY(C) ((C) & 0x1f)


static int _handle_special(const char raw[], int len);
static int _handle_normal(char key);


/*
 * public
 */
int
kbd_parse(const char raw[], int len)
{
	if (raw[0] == '\x1b')
		return _handle_special(&raw[1], len - 1);

	return _handle_normal(raw[0]);
}


int
kbd_contains_special(const char raw[])
{
	return (raw[0] == '\x1b')? 1 : 0;
}


/*
 * private
 */
static int
_handle_special(const char raw[], int len)
{
	if (len <= 1)
		return KBD_UNKNOWN;

	const char key0 = (raw[0] == 'O')? '[' : raw[0];
	if (key0 != '[')
		return KBD_UNKNOWN;

	const char key1 = raw[1];
	if ((key1 >= '0') && (key1 <= '9')) {
		if ((len < 2) || (raw[2] != '~'))
			return KBD_UNKNOWN;

		switch (key1) {
		case '1':
		case '7': return KBD_HOME;
		case '8':
		case '4': return KBD_END;
		case '5': return KBD_PAGE_UP;
		case '6': return KBD_PAGE_DOWN;
		}
	} else {
		switch (key1) {
		case 'A': return KBD_ARROW_UP;
		case 'B': return KBD_ARROW_DOWN;
		case 'C': return KBD_ARROW_RIGHT;
		case 'D': return KBD_ARROW_LEFT;
		case 'H': return KBD_HOME;
		case 'F': return KBD_END;
		}
	}

	return KBD_UNKNOWN;
}


static int
_handle_normal(char key)
{
	switch (key) {
	case 'k': return KBD_ARROW_UP;
	case 'j': return KBD_ARROW_DOWN;
	case CTRL_KEY('u'): return KBD_PAGE_UP;
	case CTRL_KEY('d'): return KBD_PAGE_DOWN;
	case 'g': return KBD_HOME;
	case 'G': return KBD_END;
	case 13: return KBD_ENTER;
	case ' ': return KBD_SPACE;
	case '/': return KBD_SLASH;
	case 127: return KBD_BACKSPACE;
	case 'n': return KBD_N;
	case 'p': return KBD_P;
	case 'q': return KBD_Q;
	case 's': return KBD_S;
	case 'y': return KBD_Y;
	}

	return KBD_UNKNOWN;
}

