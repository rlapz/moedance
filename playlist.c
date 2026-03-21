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
#include <threads.h>

#include <libavformat/avformat.h>

#include <sys/stat.h>
#include <sys/sysinfo.h>

#include "playlist.h"
#include "util.h"
#include "config.h"


static const char *_allowed_file_types[] = CFG_FILE_TYPES;


typedef struct item_chunk {
	int            thrd_ok;
	int            len;
	PlaylistItem **list;
	thrd_t         thrd;
} ItemChunk;


static int  _verify(const char *name);
static int  _item_new(PlaylistItem **new_item, const char path[], int path_len);
static void _item_new_load(PlaylistItem *item);
static int  _item_new_load_thrd(void *udata);
static int  _sort_dir_cb(const struct dirent **a, const struct dirent **b);
static void _load_files(Str *str, ArrayPtr *file_arr, const char path[], int max_depth);
static int  _load_files_meta(ArrayPtr *file_arr);


/*
 * public
 */
int
playlist_init(Playlist *p, const char root_dir[])
{
	if (chdir(root_dir) < 0)
		return -1;

#ifdef DEBUG
	av_log_set_level(AV_LOG_VERBOSE);
#else
	av_log_set_level(AV_LOG_QUIET);
#endif

	p->items = NULL;
	p->items_len = 0;
	return 0;
}


int
playlist_load(Playlist *p, const PlaylistItem **items[])
{
	Str str;
	if (str_init_alloc(&str, 4096) < 0) {
		log_err(errno, "playlist: _load_files_recurse: str_init_alloc");
		return -1;
	}

	ArrayPtr arr;
	array_ptr_init(&arr);

	_load_files(&str, &arr, ".", CFG_DIR_RECURSIVE_SIZE);
	if (_load_files_meta(&arr) < 0) {
		str_deinit(&str);
		array_ptr_deinit(&arr);
		return -1;
	}

	/* transfer the ownership */
	p->items = (PlaylistItem **)arr.items;
	p->items_len = (int)arr.len;

	str_deinit(&str);

	*items = (const PlaylistItem **)p->items;
	return p->items_len;
}


void
playlist_deinit(Playlist *p)
{
	PlaylistItem **const items = p->items;
	for (int i = 0; i < p->items_len; i++) {
		PlaylistItem *const itm = items[i];
		free(itm->title);
		free(itm->artist);
		free(itm->album);
		free(itm->genre);
		free(itm);
	}

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

	for (int i = 0; i < (int)LEN(_allowed_file_types); i++) {
		if (strcasecmp(ext, _allowed_file_types[i]) == 0)
			return 0;
	}

	return -1;
}


static int
_item_new(PlaylistItem **new_item, const char path[], int path_len)
{
	const size_t path_len_n = ((size_t)path_len) + 1;
	PlaylistItem *const item = malloc(sizeof(PlaylistItem) + path_len_n);
	if (item == NULL) {
		log_err(errno, "playlist: _item_new: item: malloc: \"%s\"", path);
		return -1;
	}

	cstr_copy_n(item->file_path, path_len_n, path, path_len);

#if (CFG_PLAYLIST_SHOW_FULL_PATH != 0)
	/* skip './' */
	item->name = item->file_path + 2;
#else
	const char *const name = strrchr(item->file_path, '/');
	if (name != NULL)
		item->name = name + 1;
	else
		item->name = item->file_path;
#endif

	item->title = NULL;
	item->artist = NULL;
	item->album = NULL;
	item->genre = NULL;
	item->duration = 0;
	*new_item = item;
	return 0;
}


static void
_item_new_load(PlaylistItem *item)
{
	int ret;
	AVFormatContext *ctx = NULL;
#ifdef DEBUG
	log_info("playlist: _item_new_load: \"%s\"", item->file_path);
#endif

	ret = avformat_open_input(&ctx, item->file_path, NULL, NULL);
	if (ret < 0) {
		log_err(0, "playlist: _item_new_load: avformat_open_input: \"%s\": %s",
			item->file_path, av_err2str(ret));
		return;
	}

	if (ctx->probe_score <= CFG_PROBE_SCORE_MIN) {
		log_err(0, "playlist: _item_new_load: probe_score \"%s\": %d <= %d",
			item->file_path, ctx->probe_score, CFG_PROBE_SCORE_MIN);
		goto out0;
	}

	ret = avformat_find_stream_info(ctx, NULL);
	if (ret < 0) {
		log_err(0, "playlist: _item_new_load: avformat_find_stream_info: \"%s\": %s",
			item->file_path, av_err2str(ret));
		goto out0;
	}

	AVDictionaryEntry * ent = av_dict_get(ctx->metadata, "title", NULL, 0);
	if (ent != NULL)
		item->title = strdup(ent->value);
	ent = av_dict_get(ctx->metadata, "artist", NULL, 0);
	if (ent != NULL)
		item->artist = strdup(ent->value);
	ent = av_dict_get(ctx->metadata, "album", NULL, 0);
	if (ent != NULL)
		item->album = strdup(ent->value);
	ent = av_dict_get(ctx->metadata, "genre", NULL, 0);
	if (ent != NULL)
		item->genre = strdup(ent->value);

	item->duration = ctx->duration / AV_TIME_BASE;

out0:
	avformat_close_input(&ctx);
}


static int
_item_new_load_thrd(void *udata)
{
	ItemChunk *const chunk = (ItemChunk *)udata;
	for (int i = 0; i < chunk->len; i++)
		_item_new_load(chunk->list[i]);

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
	int num;
	struct stat st;
	struct dirent **list;
	PlaylistItem *new_item;
	ArrayPtr dir_arr;
	char *dir_name;


	if (file_arr->len == (INT_MAX - 1)) {
		log_err(errno, "playlist: _load_files: too many files!: max: %zu", (INT_MAX - 1));
		return;
	}

	num = scandir(path, &list, NULL, _sort_dir_cb);
	if (num < 0) {
		log_err(errno, "playlist: _load_files: scandir: %s", path);
		return;
	}

	array_ptr_init(&dir_arr);
	for (int i = 0; i < num; i++) {
		const char *const name = list[i]->d_name;
		if (name[0] == '.') {
			free(list[i]);
			continue;
		}

		const char *const fname = str_set_fmt(str, "%s/%s", path, name);
		if (fname == NULL) {
			log_err(errno, "playlist: _load_files: str_set_fmt");
			free(list[i]);
			continue;
		}

		if (stat(str->cstr, &st) < 0) {
			log_err(errno, "playlist: _load_files: stat: %s", fname);
			free(list[i]);
			continue;
		}

		switch (st.st_mode & S_IFMT) {
		case S_IFDIR:
			/* be aware! */
			if (max_depth == 0) {
				log_err(0, "playlist: _load_files: %s: too deep!", fname);
				for (; i < num; i++)
					free(list[i]);

				free(list);
				array_ptr_deinit(&dir_arr);
				return;
			}

			dir_name = str_dup(str);
			if (dir_name == NULL) {
				log_err(errno, "playlist: _load_files: str_dup: %s", fname);
				break;
			}

			if (array_ptr_append(&dir_arr, dir_name) < 0) {
				log_err(errno, "playlist: _load_files: array_ptr_append: %s", dir_name);
				free(dir_name);
			}

			break;
		case S_IFREG:
			if (_verify(str->cstr) < 0)
				break;

			if (_item_new(&new_item, fname, (int)str->len) < 0)
				break;

			if (array_ptr_append(file_arr, new_item) < 0) {
				log_err(errno, "playlist: _load_files: array_ptr_append: %s", fname);
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


static int
_load_files_meta(ArrayPtr *file_arr)
{
	if (file_arr->len == 0)
		return 0;

	int nprocs = CFG_FILE_META_THREADS_NUM;
	if (nprocs <= 0)
		nprocs = get_nprocs();
	if (nprocs <= 0)
		nprocs = 1;
	
	ItemChunk *const chunks = malloc((size_t)nprocs * sizeof(ItemChunk));
	if (chunks == NULL) {
		log_err(0, "playlist: _load_files_meta: malloc");
		return -1;
	}

	const int len = (int)file_arr->len;
	if (nprocs > len)
		nprocs = len;

	const int each = (len / nprocs);
	for (int i = 0; i < nprocs; i++) {
		ItemChunk *const chunk = &chunks[i];
		chunk->list = (PlaylistItem **)&file_arr->items[i * each];
		chunk->len = each;
	}

	// remaining item(s)
	const int total = (each * nprocs);
	if (total < len) {
		ItemChunk *const chunk = &chunks[nprocs - 1];
		chunk->len += (len - total);
	}

	for (int i = 0; i < nprocs; i++) {
		ItemChunk *const chunk = &chunks[i];
		chunk->thrd_ok = 1;
		if (thrd_create(&chunk->thrd, _item_new_load_thrd, chunk) != thrd_success) {
			log_err(0, "playlist: _load_files_meta: thrd_create[%d]", i);
			chunk->thrd_ok = 0;
		}
	}

	for (int i = 0; i < nprocs; i++) {
		ItemChunk *const chunk = &chunks[i];
		if (chunk->thrd_ok == 0)
			continue;
		
		thrd_join(chunk->thrd, NULL);
	}

	free(chunks);
	return 0;
}
