#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <signal.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/signalfd.h>

#include "moedance.h"
#include "config.h"


#define CTRL_KEY(C) ((C) & 0x1f)


enum {
	_FLAG_ALIVE = (1 << 0),
	_FLAG_READY = (1 << 1),
	_FLAG_QUIT  = (1 << 2),
};

enum {
	_FILE_TYPE_FLAC,
	_FILE_TYPE_MP3,
	_FILE_TYPE_WAV,
};

static char        _tmp_buffer[4096]; /* general purpose buffer, be aware! */
static const char  _player_state_chr[] = {
	[PLAYER_STATE_STOPPED] = '#',
	[PLAYER_STATE_PLAYING] = '>',
	[PLAYER_STATE_PAUSED]  = '^',
	[PLAYER_STATE_UNKNOWN] = '?',
};


static int  _set_signal_handler(MoeDance *m);
static int  _event_loop(MoeDance *m);
static void _event_handle_kbd(MoeDance *m, int fd);
static void _event_handle_kbd_escape(MoeDance *m, const char keys[], int len);
static void _event_handle_kbd_normal(MoeDance *m, char key);
static void _event_handle_signal(MoeDance *m, int fd);

static void _kbd_handle_key_up(MoeDance *m);
static void _kbd_handle_key_down(MoeDance *m);
static void _kbd_handle_key_scroll_up(MoeDance *m);
static void _kbd_handle_key_scroll_down(MoeDance *m);
static void _kbd_handle_key_left(MoeDance *m);
static void _kbd_handle_key_right(MoeDance *m);
static void _kbd_handle_key_stop(MoeDance *m);
static void _kbd_handle_key_enter(MoeDance *m);
static void _kbd_handle_key_toggle_play(MoeDance *m);
static void _kbd_handle_key_next(MoeDance *m);
static void _kbd_handle_key_prev(MoeDance *m);

static void _tui_draw_all(MoeDance *m);
static void _tui_draw_header(MoeDance *m);
static void _tui_draw_body(MoeDance *m);
static void _tui_draw_footer(MoeDance *m);
static void _tui_draw_quit_dialog(MoeDance *m);

static int  _item_new(MoeDanceItem **new_item, const char path[], int path_len);
static int  _item_get_type(const char path[]);
static void _item_get_duration_flac(MoeDanceItem *m, const char map[], size_t len);
static void _item_get_duration_mp3(MoeDanceItem *m, const char map[], size_t len);
static void _item_get_duration_wav(MoeDanceItem *m, const char map[], size_t len);

static int  _sort_dir_cb(const struct dirent **a, const struct dirent **b);
static void _load_files(Str *str, ArrayPtr *file_arr, const char path[], int max_depth);
static void _load_files_recurse(MoeDance *m, int max_depth);


/*
 * private
 */
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
		ret = -errno;
		log_err(ret, "_set_signal_handler: sigprocmask");
		return ret;
	}

	const int sigfd = signalfd(-1, &sigmask, 0);
	if (sigfd < 0) {
		ret = -errno;
		log_err(ret, "_set_signal_handler: signalfd");
		return ret;
	}

	m->poll_fds[MOEDANCE_FD_SIGNAL].fd = sigfd;
	m->poll_fds[MOEDANCE_FD_SIGNAL].events = POLLIN;
	return ret;
}


static int
_event_loop(MoeDance *m)
{
	int ret = _set_signal_handler(m);
	if (ret < 0)
		return ret;

	if (m->items_len > 0) {
		m->items[0]->is_selected = 1;
		m->items[0]->now_playing = 1;
		m->item_active = 0;
	}

	_tui_draw_all(m);
	SET(m->flags, (_FLAG_ALIVE | _FLAG_READY));


	struct pollfd *const pfds = m->poll_fds;
	while (CHECK(m->flags, _FLAG_ALIVE)) {
		ret = poll(pfds, __MOEDANCE_FD_SIZE, 100);
		if (ret < 0) {
			ret = -errno;
			log_err(ret, "_event_loop: poll");
			goto out0;
		}

		if (ret == 0) {
			// TODO: refresh durations
			continue;
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

				log_info("_event_loop: revents: %s", revents_str);
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
				log_err(0, "_event_loop: invalid event: %d", i);
				UNSET(m->flags, _FLAG_ALIVE);
				ret = -1;
			}
		}
	}

out0:
	close(m->poll_fds[MOEDANCE_FD_SIGNAL].fd);
	return ret;
}


/* TODO: simplfy */
static void
_event_handle_kbd(MoeDance *m, int fd)
{
	const ssize_t rd = read(fd, _tmp_buffer, LEN(_tmp_buffer));
	if (rd < 0) {
		log_err(errno, "_event_handle_kbd: read");
		return;
	}

	if (_tmp_buffer[0] == '\x1b')
		_event_handle_kbd_escape(m, &_tmp_buffer[1], (int)rd - 1);
	else
		_event_handle_kbd_normal(m, _tmp_buffer[0]);
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
	case 'A':
		key = 'k';
		break;
	case 'B':
		key = 'j';
		break;
	case 'C':
		key = 'l';
		break;
	case 'D':
		key = 'h';
		break;
	case '5':
		/* page up */
		key = ('u' & 0x1f);
		break;
	case '6':
		/* page down */
		key = ('d' & 0x1f);
		break;
	default:
		return;
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
		_tui_draw_footer(m);
		return;
	}

	switch (key) {
	case 'q':
		SET(m->flags, _FLAG_QUIT);
		_tui_draw_quit_dialog(m);
		break;
	case 'k':
		_kbd_handle_key_up(m);
		break;
	case 'j':
		_kbd_handle_key_down(m);
		break;
	case CTRL_KEY('u'):
		_kbd_handle_key_scroll_up(m);
		break;
	case CTRL_KEY('d'):
		_kbd_handle_key_scroll_down(m);
		break;
	case 'h':
		_kbd_handle_key_left(m);
		break;
	case 'l':
		_kbd_handle_key_right(m);
		break;
	case 's':
		_kbd_handle_key_stop(m);
		break;
	case 13:
		_kbd_handle_key_enter(m);
		break;
	case ' ':
		_kbd_handle_key_toggle_play(m);
		break;
	case 'n':
		_kbd_handle_key_next(m);
		break;
	case 'p':
		_kbd_handle_key_prev(m);
		break;
	}
}


static void
_event_handle_signal(MoeDance *m, int fd)
{
	struct signalfd_siginfo siginfo;
	if (read(fd, &siginfo, sizeof(siginfo)) < 0) {
		log_err(errno, "_event_handle_signal: read");
		siginfo.ssi_signo = SIGWINCH;
	}

	switch (siginfo.ssi_signo) {
	case SIGWINCH:
		tui_resize(&m->tui);

		const int end = tui_body_get_items_len(&m->tui);
		if (end <= 0)
			return;

		int diff = m->item_cursor - end;
		const int sum = m->item_top + end;
		if (diff >= 0) {
			m->item_cursor = (end - 1);
			m->item_top += (diff + 1);
		} else if ((sum >= m->items_len - 1) && (m->item_top > 0)) {
			diff = sum - (m->items_len - 1);
			if (m->item_cursor + diff - 1 >= 0) {
				m->item_cursor += diff - 1;
				m->item_top -= diff - 1;
			}
		}

		tui_clear();
		_tui_draw_all(m);
		break;
	case SIGHUP:
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		UNSET(m->flags, _FLAG_ALIVE);
		break;
	default:
		log_err(0, "_event_handle_signal: invalid signal");
		break;
	}
}


static void
_kbd_handle_key_up(MoeDance *m)
{
	if (m->item_cursor == 0) {
		if (m->item_top == 0)
			return;

		/* scroll up */
		m->item_top--;
		m->item_cursor++;
	}


	const int idx = m->item_cursor + m->item_top;
	if (idx == 0)
		return;

	m->items[idx]->is_selected = 0;
	m->items[idx - 1]->is_selected = 1;
	m->item_cursor--;

	_tui_draw_header(m);
	_tui_draw_body(m);
}


static void
_kbd_handle_key_down(MoeDance *m)
{
	const int end = tui_body_get_items_len(&m->tui) - 1;
	if (m->item_cursor == end) {
		if ((m->item_top + m->item_cursor) == (m->items_len - 1))
			return;

		/* scroll down */
		m->item_top++;
		m->item_cursor--;
	}

	const int idx = m->item_cursor + m->item_top + 1;
	if (idx >= m->items_len)
		return;

	m->items[idx]->is_selected = 1;
	m->items[idx - 1]->is_selected = 0;
	m->item_cursor++;

	_tui_draw_header(m);
	_tui_draw_body(m);
}


static void
_kbd_handle_key_scroll_up(MoeDance *m)
{
	const int end = tui_body_get_items_len(&m->tui);
	if ((m->item_top == 0) || (end <= 0))
		return;

	const int step = (m->item_top < end)? m->item_top:end;
	const int idx = m->item_cursor + m->item_top;
	m->items[idx]->is_selected = 0;
	m->items[idx - step]->is_selected = 1;
	m->item_top -= step;

	_tui_draw_header(m);
	_tui_draw_body(m);
}


static void
_kbd_handle_key_scroll_down(MoeDance *m)
{
	const int end = tui_body_get_items_len(&m->tui);
	if ((end <= 0) || (m->items_len < end))
		return;

	const int diff = m->items_len - (m->item_top + end);
	const int step = (diff < end)? diff:end;

	const int idx = m->item_cursor + m->item_top;
	m->items[idx]->is_selected = 0;
	m->items[idx + step]->is_selected = 1;
	m->item_top += step;

	_tui_draw_header(m);
	_tui_draw_body(m);
}


static void
_kbd_handle_key_left(MoeDance *m)
{
	if (m->player.state == PLAYER_STATE_STOPPED)
		return;

	const int active_idx = m->item_active;
	if (active_idx < 0)
		return;

	m->items[active_idx]->duration_min--;
	_tui_draw_footer(m);
}


static void
_kbd_handle_key_right(MoeDance *m)
{
	if (m->player.state == PLAYER_STATE_STOPPED)
		return;

	const int active_idx = m->item_active;
	if (active_idx < 0)
		return;

	m->items[active_idx]->duration_min++;
	_tui_draw_footer(m);
}


static void
_kbd_handle_key_stop(MoeDance *m)
{
	const int active_idx = m->item_active;
	if (active_idx < 0)
		return;

	m->items[active_idx]->duration_min = 0;
	player_stop(&m->player);

	_tui_draw_body(m);
	_tui_draw_footer(m);
}


static void
_kbd_handle_key_enter(MoeDance *m)
{
	if (m->items_len == 0)
		return;

	const int sel_idx = m->item_cursor + m->item_top;
	const int active_idx = m->item_active;
	if (active_idx >= 0) {
		m->items[active_idx]->now_playing = 0;
		m->items[active_idx]->duration_min = 0;
	}

	m->items[sel_idx]->now_playing = 1;
	m->items[sel_idx]->duration_min = 0;
	m->item_active = sel_idx;
	m->player.state = PLAYER_STATE_PLAYING;

	log_info("playing: \"%s\"", m->items[sel_idx]->path);
	player_play(&m->player, m->items[sel_idx]->path, 0);

	_tui_draw_body(m);
	_tui_draw_footer(m);
}


static void
_kbd_handle_key_toggle_play(MoeDance *m)
{
	const int active_idx = m->item_active;
	if (active_idx < 0)
		return;

	if (m->player.state == PLAYER_STATE_PLAYING) {
		player_stop(&m->player);
		m->player.state = PLAYER_STATE_PAUSED;
	} else if ((m->player.state == PLAYER_STATE_PAUSED) || (m->player.state == PLAYER_STATE_STOPPED)) {
		player_play(&m->player, m->items[active_idx]->path, 0);
		m->player.state = PLAYER_STATE_PLAYING;
	}

	_tui_draw_footer(m);
}


static void
_kbd_handle_key_next(MoeDance *m)
{
	int idx = m->item_active;
	if (idx < 0)
		return;

	if ((idx + 1) >= m->items_len) {
		player_play(&m->player, m->items[idx]->path, 0);
		return;
	}

	player_play(&m->player, m->items[idx + 1]->path, 0);
	m->items[idx++]->now_playing = 0;
	m->items[idx]->now_playing = 1;
	m->item_active = idx;

	_tui_draw_body(m);
	_tui_draw_footer(m);
}


static void
_kbd_handle_key_prev(MoeDance *m)
{
	int idx = m->item_active;
	if (idx < 0)
		return;

	if ((idx - 1) < 0) {
		player_play(&m->player, m->items[idx]->path, 0);
		return;
	}

	player_play(&m->player, m->items[idx - 1]->path, 0);
	m->items[idx--]->now_playing = 0;
	m->items[idx]->now_playing = 1;
	m->item_active = idx;

	_tui_draw_body(m);
	_tui_draw_footer(m);
}


static void
_tui_draw_all(MoeDance *m)
{
	_tui_draw_header(m);
	_tui_draw_body(m);
	_tui_draw_footer(m);
}


static void
_tui_draw_header(MoeDance *m)
{
	const int adder = (m->items_len > 0)? 1:0;
	tui_header_set(&m->tui, m->root_dir, m->item_cursor + m->item_top + adder, m->items_len);
}


static void
_tui_draw_body(MoeDance *m)
{
	Tui *const tui = &m->tui;
	const int end = tui_body_get_items_len(tui);
	if (end <= 0)
		return;

	int len = end + m->item_top;
	if (len > m->items_len)
		len -= (len - m->items_len);

	const MoeDanceItem *it;
	for (int i = m->item_top, j = 0; i < len; i++, j++) {
		it = m->items[i];
		tui_body_set_item(tui, it->name, it->duration_max, it->is_selected, it->now_playing, j);
	}
}


static void
_tui_draw_footer(MoeDance *m)
{
	if (CHECK(m->flags, _FLAG_QUIT)) {
		_tui_draw_quit_dialog(m);
		return;
	}

	const int idx = m->item_active;
	if (idx < 0) {
		tui_footer_set(&m->tui, _player_state_chr[PLAYER_STATE_STOPPED], 0, 0, 0, "???");
		return;
	}

	const MoeDanceItem *const item = m->items[idx];
	tui_footer_set(&m->tui, _player_state_chr[m->player.state], idx + 1, item->duration_min,
		       item->duration_max, item->name);
}


static void
_tui_draw_quit_dialog(MoeDance *m)
{
	tui_show_dialog(&m->tui, "Quit? (y)");
}


static int
_item_new(MoeDanceItem **new_item, const char path[], int path_len)
{
	int ret = -1;
	const int type = _item_get_type(path);
	if (type < 0)
		return ret;

	const int fd = open(path, O_RDONLY);
	if (fd < 0) {
		ret = -errno;
		log_err(ret, "_item_new: open: \"%s\"", path);
		return ret;
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		ret = -errno;
		log_err(ret, "_item_new: fstat: \"%s\"", path);
		goto out0;
	}

	const size_t map_len = (size_t)st.st_size;
	char *const map = mmap(NULL, map_len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map == MAP_FAILED) {
		ret = -errno;
		log_err(ret, "_item_new: mmap: \"%s\"", path);
		goto out0;
	}

	MoeDanceItem *const item = malloc(sizeof(MoeDanceItem) + ((size_t)path_len + 1));
	if (item == NULL) {
		ret = -errno;
		log_err(ret, "_item_new: malloc: \"%s\"", path);
		goto out1;
	}

	switch (type) {
	case _FILE_TYPE_FLAC:
		_item_get_duration_flac(item, map, map_len);
		break;
	case _FILE_TYPE_MP3:
		_item_get_duration_mp3(item, map, map_len);
		break;
	case _FILE_TYPE_WAV:
		_item_get_duration_wav(item, map, map_len);
		break;
	}

	memcpy(item->path, path, path_len);
	item->path[path_len] = '\0';

#if CFG_PLAYLIST_SHOW_FULL_PATH == 1
	item->name = item->path + 2;
#else
	const char *const name = strrchr(item->path, '/');
	if (name != NULL)
		item->name = name + 1;
	else
		item->name = item->path;
#endif

	item->is_selected = 0;
	item->now_playing = 0;
	item->duration_min = 0;
	*new_item = item;
	ret = 0;

out1:
	munmap(map, map_len);
out0:
	close(fd);
	return ret;
}


static int
_item_get_type(const char path[])
{
	const char *const ext = strrchr(path, '.');
	if (ext == NULL)
		return -1;

	if (strcasecmp(ext, ".flac") == 0)
		return _FILE_TYPE_FLAC;

	if (strcasecmp(ext, ".mp3") == 0)
		return _FILE_TYPE_MP3;

	if (strcasecmp(ext, ".wav") == 0)
		return _FILE_TYPE_WAV;

	return -1;
}


static void
_item_get_duration_flac(MoeDanceItem *m, const char map[], size_t len)
{
	/* TODO */
	m->duration_max = 0;
}


static void
_item_get_duration_mp3(MoeDanceItem *m, const char map[], size_t len)
{
	/* TODO */
	m->duration_max = 0;
}


static void
_item_get_duration_wav(MoeDanceItem *m, const char map[], size_t len)
{
	/* TODO */
	m->duration_max = 0;
}


static void
_load_files(Str *str, ArrayPtr *file_arr, const char path[], int max_depth)
{
	int ret, num;
	struct stat st;
	struct dirent **list;
	MoeDanceItem *new_item;
	ArrayPtr dir_arr;
	char *dir_name;


	array_ptr_init(&dir_arr);
	if (file_arr->len == INT_MAX - 1)
		return;

	num = scandir(path, &list, NULL, versionsort);
	if (num < 0) {
		ret = -errno;
		log_err(ret, "_load_files: scandir: %s", path);
		return;
	}

	for (int i = 0; i < num; i++) {
		const char *const name = list[i]->d_name;
		if (name[0] == '.') {
			free(list[i]);
			continue;
		}

		str_set_fmt(str, "%s/%s", path, name);
		if (stat(str->cstr, &st) < 0) {
			free(list[i]);
			continue;
		}

		switch (st.st_mode & S_IFMT) {
		case S_IFDIR:
			/* be aware! */
			if (max_depth == 0)
				return;

			dir_name = str_dup(str);
			if (dir_name == NULL) {
				log_err(errno, "_load_files: str_dup: %s", str->cstr);
				break;
			}

			ret = array_ptr_append(&dir_arr, dir_name);
			if (ret < 0) {
				log_err(errno, "_load_files: array_ptr_append: %s", dir_name);
				free(dir_name);
			}

			break;
		case S_IFREG:
			if (_item_new(&new_item, str->cstr, str->len) < 0)
				break;

			ret = array_ptr_append(file_arr, new_item);
			if (ret < 0) {
				log_err(ret, "_load_files: array_ptr_append: %s", str->cstr);
				free(new_item);
			}

			break;
		}

		free(list[i]);
	}

	free(list);
	for (size_t i = 0; i < dir_arr.len; i++) {
		dir_name = (char *)dir_arr.items[i];

		/* be aware! */
		_load_files(str, file_arr, dir_name, max_depth - 1);
		free(dir_name);
	}

	array_ptr_deinit(&dir_arr);
}


static void
_load_files_recurse(MoeDance *m, int max_depth)
{
	ArrayPtr arr;
	array_ptr_init(&arr);

	Str str;
	str_init(&str, _tmp_buffer, LEN(_tmp_buffer));

	_load_files(&str, &arr, ".", max_depth);

	/* transfer the ownership */
	m->items = (MoeDanceItem **)arr.items;
	m->items_len = (int)arr.len;
}


/*
 * public
 */
void
moedance_init(MoeDance *m, const char root_dir[])
{
	m->flags = 0;
	m->root_dir = root_dir;
	m->item_top = 0;
	m->item_active = -1;
	m->item_cursor = 0;
	m->items_len = 0;
	m->items = NULL;
	m->poll_fds[MOEDANCE_FD_KBD].fd = STDIN_FILENO;
	m->poll_fds[MOEDANCE_FD_KBD].events = POLLIN;
}


void
moedance_deinit(MoeDance *m)
{
	if (m->items != NULL) {
		for (int i = 0; i < m->items_len; i++)
			free(m->items[i]);

		free(m->items);
	}
}


int
moedance_run(MoeDance *m)
{
	int ret = chdir(m->root_dir);
	if (ret < 0) {
		ret = -errno;
		log_err(ret, "moedance_run: chdir: \"%s\"", m->root_dir);
		return ret;
	}

	log_file_init(CFG_LOG_FILE);
	_load_files_recurse(m, CFG_DIR_RECURSIVE_SIZE);

	ret = player_init(&m->player);
	if (ret < 0)
		goto out0;

	ret = tui_init(&m->tui);
	if (ret < 0) {
		log_err(ret, "moedance_run: tui_init");
		goto out1;
	}

	ret = _event_loop(m);
	tui_deinit(&m->tui);

out1:
	player_deinit(&m->player);
out0:
	log_file_deinit();
	return ret;
}

