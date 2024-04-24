#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <signal.h>

#include <sys/stat.h>
#include <sys/signalfd.h>

#include "moedance.h"
#include "config.h"


#define CTRL_KEY(C) ((C) & 0x1f)


enum {
	_FLAG_ALIVE        = (1 << 0),
	_FLAG_READY        = (1 << 1),
	_FLAG_QUIT         = (1 << 2),
	_FLAG_PLAYLIST_TOP = (1 << 3),
};


static char _tmp_buffer[4096]; /* general purpose buffer, be aware! */


static int  _set_signal_handler(MoeDance *m);
static int  _event_loop(MoeDance *m);
static void _event_handle_kbd(MoeDance *m, int fd);
static void _event_handle_kbd_escape(MoeDance *m, const char keys[], int len);
static void _event_handle_kbd_normal(MoeDance *m, char key);
static void _event_handle_signal(MoeDance *m, int fd);

static void _tui_quit_dialog(MoeDance *m);

static void _kbd_handle_key_stop(MoeDance *m);
static void _kbd_handle_key_enter(MoeDance *m);
static void _kbd_handle_key_toggle_play(MoeDance *m);
static void _kbd_handle_key_next(MoeDance *m);
static void _kbd_handle_key_prev(MoeDance *m);

static int  _item_new(TuiPlaylistItem **new_item, const char path[], int path_len);

static int  _sort_dir_cb(const struct dirent **a, const struct dirent **b);
static void _load_files(Str *str, ArrayPtr *file_arr, const char path[], int max_depth);
static void _load_files_recurse(MoeDance *m, int max_depth);


/*
 * public
 */
void
moedance_init(MoeDance *m, const char root_dir[])
{
	m->flags = 0;
	m->root_dir = root_dir;
	m->playlist_items = NULL;
	m->playlist_items_len = 0;
	m->poll_fds[MOEDANCE_FD_KBD].fd = STDIN_FILENO;
	m->poll_fds[MOEDANCE_FD_KBD].events = POLLIN;
}


void
moedance_deinit(MoeDance *m)
{
	TuiPlaylistItem **const items = m->playlist_items;
	for (int i = 0; i < m->playlist_items_len; i++)
		free(items[i]);

	free(items);
}


int
moedance_run(MoeDance *m)
{
	int ret = chdir(m->root_dir);
	if (ret < 0) {
		log_err(errno, "moedance: moedance_run: chdir: \"%s\"", m->root_dir);
		return -1;
	}

	log_file_init(CFG_LOG_FILE);

	ret = player_init(&m->player);
	if (ret < 0)
		goto out0;

	ret = tui_init(&m->tui, m->root_dir);
	if (ret < 0)
		goto out1;

	tui_draw(&m->tui);
	_load_files_recurse(m, CFG_DIR_RECURSIVE_SIZE);

	ret = _event_loop(m);
	tui_deinit(&m->tui);

out1:
	player_deinit(&m->player);
out0:
	log_file_deinit();
	return ret;
}


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
		log_err(errno, "moedance: _set_signal_handler: sigprocmask");
		return -1;
	}

	const int sigfd = signalfd(-1, &sigmask, SFD_NONBLOCK);
	if (sigfd < 0) {
		log_err(errno, "moedance: _set_signal_handler: signalfd");
		return -1;
	}

	m->poll_fds[MOEDANCE_FD_SIGNAL].fd = sigfd;
	m->poll_fds[MOEDANCE_FD_SIGNAL].events = POLLIN;
	return 0;
}


static int
_event_loop(MoeDance *m)
{
	int ret = _set_signal_handler(m);
	if (ret < 0)
		return -1;

	SET(m->flags, (_FLAG_ALIVE | _FLAG_READY));

	struct pollfd *const pfds = m->poll_fds;
	while (CHECK(m->flags, _FLAG_ALIVE)) {
		ret = poll(pfds, __MOEDANCE_FD_SIZE, 100);
		if (ret < 0) {
			log_err(errno, "moedance: _event_loop: poll");
			goto out0;
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

				log_info("moedance; _event_loop: revents: %s", revents_str);
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
				log_err(0, "moedance: _event_loop: invalid event: %d", i);
				UNSET(m->flags, _FLAG_ALIVE);
				ret = -1;
			}
		}
	}

out0:
	close(m->poll_fds[MOEDANCE_FD_SIGNAL].fd);
	return ret;
}


static void
_event_handle_kbd(MoeDance *m, int fd)
{
	const ssize_t rd = read(fd, _tmp_buffer, LEN(_tmp_buffer));
	if (rd < 0) {
		log_err(errno, "moedance: _event_handle_kbd: read");
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
	case 'A': key = 'k'; break;
	case 'B': key = 'j'; break;
	case '5': key = ('u' & 0x1f); break;	/* page up */
	case '6': key = ('d' & 0x1f); break;	/* page down */
	default: return;
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
		tui_show_dialog(&m->tui, NULL);
		return;
	}

	if (CHECK(m->flags, _FLAG_PLAYLIST_TOP)) {
		UNSET(m->flags, _FLAG_PLAYLIST_TOP);
		if (key == 'g') {
			tui_playlist_top(&m->tui);
			return;
		}
	}

	switch (key) {
	case 'q':
		SET(m->flags, _FLAG_QUIT);
		_tui_quit_dialog(m);
		break;
	case CTRL_KEY('u'): tui_playlist_page_up(&m->tui); break;
	case CTRL_KEY('d'): tui_playlist_page_down(&m->tui); break;
	case 'k': tui_playlist_cursor_up(&m->tui); break;
	case 'j': tui_playlist_cursor_down(&m->tui); break;
	case 'g': SET(m->flags, _FLAG_PLAYLIST_TOP); break;
	case 'G': tui_playlist_bottom(&m->tui); break;
	case 's': _kbd_handle_key_stop(m); break;
	case 13:  _kbd_handle_key_enter(m); break;
	case ' ': _kbd_handle_key_toggle_play(m); break;
	case 'n': _kbd_handle_key_next(m); break;
	case 'p': _kbd_handle_key_prev(m); break;
	}
}


static void
_event_handle_signal(MoeDance *m, int fd)
{
	struct signalfd_siginfo siginfo;
	if (read(fd, &siginfo, sizeof(siginfo)) < 0) {
		log_err(errno, "moedance: _event_handle_signal: read");
		siginfo.ssi_signo = SIGWINCH;
	}

	switch (siginfo.ssi_signo) {
	case SIGWINCH:
		tui_draw(&m->tui);
		if (CHECK(m->flags, _FLAG_QUIT))
			_tui_quit_dialog(m);
		break;
	case SIGHUP:
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		UNSET(m->flags, _FLAG_ALIVE);
		break;
	default:
		log_err(0, "moedance: _event_handle_signal: invalid signal");
		break;
	}
}


static void
_tui_quit_dialog(MoeDance *m)
{
	tui_show_dialog(&m->tui, "Quit? (y)");
}


static void
_kbd_handle_key_stop(MoeDance *m)
{
	tui_playlist_stop(&m->tui);
}


static void
_kbd_handle_key_enter(MoeDance *m)
{
	tui_playlist_play(&m->tui);
}


static void
_kbd_handle_key_toggle_play(MoeDance *m)
{
	if (m->tui.playlist.state == PLAYER_STATE_PLAYING)
		tui_playlist_pause(&m->tui);
	else
		tui_playlist_play(&m->tui);
}


static void
_kbd_handle_key_next(MoeDance *m)
{
	tui_playlist_next(&m->tui);
}


static void
_kbd_handle_key_prev(MoeDance *m)
{
	tui_playlist_prev(&m->tui);
}


static int
_item_new(TuiPlaylistItem **new_item, const char path[], int path_len)
{
	int ret = -1;
	const int fd = open(path, O_RDONLY);
	if (fd < 0) {
		log_err(errno, "moedance: _item_new: open: \"%s\"", path);
		return -1;
	}

	struct stat st;
	if (fstat(fd, &st) < 0) {
		log_err(errno, "moedance: _item_new: fstat: \"%s\"", path);
		goto out0;
	}

	TuiPlaylistItem *const item = malloc(sizeof(TuiPlaylistItem) + ((size_t)path_len + 1));
	if (item == NULL) {
		log_err(errno, "moedance: _item_new: tui item: malloc: \"%s\"", path);
		free(item);
		goto out0;
	}

	memcpy(item->item.file_path, path, path_len);
	item->item.file_path[path_len] = '\0';

#if CFG_PLAYLIST_SHOW_FULL_PATH == 1
	item->item.name = item->item.file_path + 2;
#else
	const char *const name = strrchr(item->item.file_path, '/');
	if (name != NULL)
		item->item.name = name + 1;
	else
		item->item.name = item->item.file_path;
#endif

	const char *const ext = strrchr(item->item.file_path, '.');
	if (ext != NULL)
		item->item.file_ext = ext + 1;
	else
		item->item.file_ext = NULL;

	item->item.duration = 0;
	item->is_selected = 0;
	item->now_playing = 0;
	*new_item = item;
	ret = 0;

out0:
	close(fd);
	return ret;
}


static int
_sort_dir_cb(const struct dirent **a, const struct dirent **b)
{
	return cstr_cmp_vers((*a)->d_name, (*b)->d_name);
}


static void
_load_files(Str *str, ArrayPtr *file_arr, const char path[], int max_depth)
{
	int ret, num;
	struct stat st;
	struct dirent **list;
	TuiPlaylistItem *new_item;
	ArrayPtr dir_arr;
	char *dir_name;


	array_ptr_init(&dir_arr);
	if (file_arr->len == INT_MAX - 1)
		return;

	num = scandir(path, &list, NULL, _sort_dir_cb);
	if (num < 0) {
		ret = -errno;
		log_err(ret, "moedance: _load_files: scandir: %s", path);
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
				log_err(errno, "moedance: _load_files: str_dup: %s", str->cstr);
				break;
			}

			ret = array_ptr_append(&dir_arr, dir_name);
			if (ret < 0) {
				log_err(errno, "moedance: _load_files: array_ptr_append: %s", dir_name);
				free(dir_name);
			}

			break;
		case S_IFREG:
			if (_item_new(&new_item, str->cstr, str->len) < 0)
				break;

			ret = array_ptr_append(file_arr, new_item);
			if (ret < 0) {
				log_err(ret, "moedance: _load_files: array_ptr_append: %s", str->cstr);
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

	tui_show_dialog(&m->tui, "Loading...");
	_load_files(&str, &arr, ".", max_depth);


	/* transfer the ownership */
	m->playlist_items = (TuiPlaylistItem **)arr.items;
	m->playlist_items_len = (int)arr.len;

	tui_set_playlist(&m->tui, m->playlist_items, m->playlist_items_len);
}

