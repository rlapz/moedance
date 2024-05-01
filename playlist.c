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
#include <unistd.h>

#include <sys/stat.h>

#include "playlist.h"
#include "util.h"
#include "config.h"


static const char *_allowed_file_types[] = CFG_FILE_TYPES;


static int                  _verify(const char *name);
static int                  _item_new(PlaylistItem **new_item, const char path[], int path_len);
static int                  _sort_dir_cb(const struct dirent **a, const struct dirent **b);
static void                 _load_files(Str *str, ArrayPtr *file_arr, const char path[], int max_depth);
static const PlaylistItem **_load_files_recurse(Playlist *p, int max_depth, int *len);


/*
 * public
 */
void
playlist_init(Playlist *p)
{
	p->items = NULL;
	p->items_len = 0;
}


const PlaylistItem **
playlist_load(Playlist *p, const char root_dir[], int *len)
{
	if (chdir(root_dir) < 0) {
		log_err(errno, "playlist: playlist_load: chdir: \"%s\"", root_dir);
		return NULL;
	}

	return _load_files_recurse(p, CFG_DIR_RECURSIVE_SIZE, len);
}


void
playlist_deinit(Playlist *p)
{
	PlaylistItem **const items = p->items;
	for (int i = 0; i < p->items_len; i++)
		free(items[i]);

	free(p->items);
}


/*
 * private
 */
static int
_verify(const char *name)
{
	const char *ext = strrchr(name, '.');
	if (ext == NULL)
		return -1;

	ext++;
	if (*ext == '\0')
		return -1;

	for (size_t i = 0; i < LEN(_allowed_file_types); i++) {
		if (strcasecmp(ext, _allowed_file_types[i]) == 0)
			return 0;
	}

	return -1;
}


static int
_item_new(PlaylistItem **new_item, const char path[], int path_len)
{
	PlaylistItem *const item = malloc(sizeof(PlaylistItem) + ((size_t)path_len + 1));
	if (item == NULL) {
		log_err(errno, "playlist: _item_new: item: malloc: \"%s\"", path);
		return 0;
	}

	memcpy(item->file_path, path, (size_t)path_len);
	item->file_path[path_len] = '\0';

#if CFG_PLAYLIST_SHOW_FULL_PATH == 1
	item->name = item->file_path + 2;
#else
	const char *const name = strrchr(item->file_path, '/');
	if (name != NULL)
		item->name = name + 1;
	else
		item->name = item->file_path;
#endif

	/* TODO: get file duration */

	item->duration = 0;
	*new_item = item;
	return 0;
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
	PlaylistItem *new_item;
	ArrayPtr dir_arr;
	char *dir_name;


	array_ptr_init(&dir_arr);
	if (file_arr->len == INT_MAX - 1)
		return;

	num = scandir(path, &list, NULL, _sort_dir_cb);
	if (num < 0) {
		ret = -errno;
		log_err(ret, "playlist: _load_files: scandir: %s", path);
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
				log_err(errno, "playlist: _load_files: str_dup: %s", str->cstr);
				break;
			}

			ret = array_ptr_append(&dir_arr, dir_name);
			if (ret < 0) {
				log_err(errno, "playlist: _load_files: array_ptr_append: %s", dir_name);
				free(dir_name);
			}

			break;
		case S_IFREG:
			if (_verify(str->cstr) < 0)
				break;

			if (_item_new(&new_item, str->cstr, (int)str->len) < 0)
				break;

			ret = array_ptr_append(file_arr, new_item);
			if (ret < 0) {
				log_err(ret, "playlist: _load_files: array_ptr_append: %s", str->cstr);
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


static const PlaylistItem **
_load_files_recurse(Playlist *p, int max_depth, int *len)
{
	Str str;
	ArrayPtr arr;
	char buffer[4096];


	str_init(&str, buffer, sizeof(buffer));
	array_ptr_init(&arr);

	_load_files(&str, &arr, ".", max_depth);

	/* transfer the ownership */
	p->items = (PlaylistItem **)arr.items;
	p->items_len = (int)arr.len;

	*len = p->items_len;
	return (const PlaylistItem **)p->items;
}

