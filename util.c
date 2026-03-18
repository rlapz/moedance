#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <threads.h>

#include "util.h"


/*
 * cstr
 */
void
cstr_copy_n(char dest[], size_t dest_size, const char src[], size_t src_len)
{
	if (dest_size <= src_len)
		src_len = dest_size - 1;

	memcpy(dest, src, src_len);
	dest[src_len] = '\0';
}


char *
cstr_time_fmt(char dest[], size_t size, int64_t secs)
{
	if (size <= 9)
		return NULL;

	const int64_t h = secs / 3600;
	secs %= 3600;

	const int64_t m = secs / 60;
	secs %= 60;

	if (snprintf(dest, size, "%02" PRIi64 ":%02" PRIu64 ":%02" PRIi64, h, m, secs) < 0)
		cstr_copy_n(dest, size, "??:??:??", 8);

	return dest;
}


/*
 * Copied from: https://git.musl-libc.org/cgit/musl/tree/src/string/strverscmp.c (MIT license)
 */
int
cstr_cmp_vers(const char a[], const char b[])
{
	const unsigned char *l = (const void *)a;
	const unsigned char *r = (const void *)b;
	size_t i = 0, dp = 0, j;
	int z = 1;

	/* Find maximal matching prefix and track its maximal digit
	 * suffix and whether those digits are all zeros. */
	for (; l[i] == r[i]; i++) {
		int c = l[i];
		if (!c)
			return 0;

		if (!isdigit(c)) {
			dp = i + 1;
			z = 1;
		} else if (c!='0') {
			z = 0;
		}
	}

	if (((l[dp] - '1') < 9) && ((r[dp] - '1') < 9)) {
		/* If we're looking at non-degenerate digit sequences starting
		 * with nonzero digits, longest digit string is greater. */
		for (j = i; isdigit(l[j]); j++) {
			if (!isdigit(r[j]))
				return 1;
		}

		if (isdigit(r[j]))
			return -1;
	} else if (z && (dp < i) && (isdigit(l[i]) || isdigit(r[i]))) {
		/* Otherwise, if common prefix of digit sequence is
		 * all zeros, digits order less than non-digits. */
		return (unsigned char)(l[i] - '0') - (unsigned char)(r[i] - '0');
	}

	return l[i] - r[i];
}


/*
 * Str
 */
static int
_str_resize(Str *s, size_t len)
{
	const size_t size = s->size;
	const size_t remn_size = (size > len)? (size - s->len):0;
	if (len < remn_size)
		return 0;

	if (s->is_alloc == 0) {
		errno = ENOMEM;
		return -errno;
	}

	const size_t new_size = (len - remn_size) + size + 1;
	char *const new_cstr = realloc(s->cstr, new_size);
	if (new_cstr == NULL)
		return -errno;

	s->size = new_size;
	s->cstr = new_cstr;
	return 0;
}


int
str_init(Str *s, char buffer[], size_t size)
{
	if (size == 0) {
		errno = EINVAL;
		return -errno;
	}

	buffer[0] = '\0';
	s->len = 0;
	s->size = size;
	s->cstr = buffer;
	s->is_alloc = 0;
	return 0;
}


int
str_init_alloc(Str *s, size_t size)
{
	size = (size == 0)? 1:size;

	char *const cstr = malloc(size);
	if (cstr == NULL)
		return -errno;

	cstr[0] = '\0';
	s->len = 0;
	s->size = size;
	s->cstr = cstr;
	s->is_alloc = 1;
	return 0;
}


void
str_deinit(Str *s)
{
	if (s->is_alloc != 0)
		free(s->cstr);
}


const char *
str_set(Str *s, const char cstr[])
{
	const size_t cstr_len = strlen(cstr);
	if (cstr_len == 0) {
		s->len = 0;
		s->cstr[0] = '\0';
		return s->cstr;
	}

	if (_str_resize(s, cstr_len) < 0)
		return NULL;

	memcpy(s->cstr, cstr, cstr_len + 1);
	s->len = cstr_len;
	return s->cstr;
}


const char *
str_set_n(Str *s, const char cstr[], size_t len)
{
	if (len == 0) {
		s->len = 0;
		s->cstr[0] = '\0';
		return s->cstr;
	}

	if (_str_resize(s, len) < 0)
		return NULL;

	memcpy(s->cstr, cstr, len);
	s->len = len;
	s->cstr[len] = '\0';
	return s->cstr;
}


const char *
str_set_fmt(Str *s, const char fmt[], ...)
{
	int ret;
	va_list va;


	/* determine required size */
	va_start(va, fmt);
	ret = vsnprintf(NULL, 0, fmt, va);
	va_end(va);

	if (ret < 0)
		return NULL;

	const size_t cstr_len = (size_t)ret;
	if (cstr_len == 0) {
		s->len = 0;
		s->cstr[0] = '\0';
		return s->cstr;
	}

	if (_str_resize(s, cstr_len) < 0)
		return NULL;

	va_start(va, fmt);
	ret = vsnprintf(s->cstr, cstr_len + 1, fmt, va);
	va_end(va);

	if (ret < 0)
		return NULL;

	s->len = (size_t)ret;
	s->cstr[ret] = '\0';
	return s->cstr;
}


const char *
str_append(Str *s, const char cstr[])
{
	const size_t cstr_len = strlen(cstr);
	if (cstr_len == 0)
		return s->cstr;

	if (_str_resize(s, cstr_len) < 0)
		return NULL;

	const size_t len = s->len;
	memcpy(s->cstr + len, cstr, cstr_len + 1);

	s->len = len + cstr_len;
	return s->cstr;
}


const char *
str_append_n(Str *s, const char cstr[], size_t len)
{
	if (len == 0)
		return s->cstr;

	if (_str_resize(s, len) < 0)
		return NULL;

	size_t slen = s->len;
	memcpy(s->cstr + slen, cstr, len);

	slen += len;
	s->len = slen;
	s->cstr[slen] = '\0';
	return s->cstr;
}


const char *
str_append_fmt(Str *s, const char fmt[], ...)
{
	int ret;
	va_list va;


	/* determine required size */
	va_start(va, fmt);
	ret = vsnprintf(NULL, 0, fmt, va);
	va_end(va);

	if (ret < 0)
		return NULL;

	const size_t cstr_len = (size_t)ret;
	if (cstr_len == 0)
		return s->cstr;

	if (_str_resize(s, cstr_len) < 0)
		return NULL;

	size_t len = s->len;
	va_start(va, fmt);
	ret = vsnprintf(s->cstr + len, cstr_len + 1, fmt, va);
	va_end(va);

	if (ret < 0)
		return NULL;

	len += (size_t)ret;
	s->len = len;
	s->cstr[len] = '\0';
	return s->cstr;
}


char *
str_dup(Str *s)
{
	const size_t len = s->len + 1; /* including '\0' */
	char *const ret = malloc(len);
	if (ret == NULL)
		return NULL;

	return (char *)memcpy(ret, s->cstr, len);
}


/*
 * ArrayPtr
 */
void
array_ptr_init(ArrayPtr *a)
{
	a->len = 0;
	a->items = NULL;
}


void
array_ptr_deinit(ArrayPtr *a)
{
	free(a->items);
}


int
array_ptr_append(ArrayPtr *a, void *item)
{
	const size_t new_len = a->len + 1;
	void **const items = realloc(a->items, (sizeof(void *) * new_len));
	if (items == NULL)
		return -errno;

	items[a->len] = item;
	a->len = new_len;
	a->items = items;
	return 0;
}


/*
 * Stream
 */
void
stream_in_flush(int fd)
{
	int c;
	while (read(fd, &c, 1) > 0);
	errno = 0;
}


/*
 * Log
 */
#define _LOG_ALT_NAME "./moedance.log"

static FILE *_log_file_out = NULL;
static FILE *_log_file_err = NULL;
static mtx_t _log_mutex;


static const char *
_log_datetime_now(char dest[], size_t size)
{
	const char *const ret = "???";
	const time_t tm_raw = time(NULL);
	struct tm *const tm = localtime(&tm_raw);
	if (tm == NULL)
		return ret;

	const char *const res = asctime(tm);
	if (res == NULL)
		return ret;

	const size_t res_len = strlen(res);
	if ((res_len == 0) || (res_len >= size))
		return ret;

	memcpy(dest, res, res_len - 1);
	dest[res_len - 1] = '\0';
	return dest;
}


int
log_file_init(const char path[])
{
	if (mtx_init(&_log_mutex, mtx_plain) != 0) {
		fprintf(stdout, "log: log_file_init: mtx_init: failed\n");
		return -1;
	}

	/* redirect all outputs to a file */
	_log_file_out = freopen(path, "a+", stdout);
	if (_log_file_out == NULL)
		_log_file_out = freopen(_LOG_ALT_NAME, "a+", stdout);

	assert(_log_file_out != NULL);

	_log_file_err = freopen(path, "a+", stderr);
	if (_log_file_err == NULL)
		_log_file_err = freopen(_LOG_ALT_NAME, "a+", stderr);

	assert(_log_file_err != NULL);


	setvbuf(_log_file_err, NULL, _IONBF, 0);
	setvbuf(_log_file_out, NULL, _IONBF, 0);

	log_info("starting...");
	return 0;
}


void
log_file_deinit(void)
{
	if (_log_file_out != NULL) {
		log_info("stopped");
		fflush(_log_file_out);
		fflush(_log_file_err);
		fclose(_log_file_out);
		fclose(_log_file_err);
	}

	puts("...");
	mtx_destroy(&_log_mutex);
	puts("...");
}


void
log_err(int errnum, const char fmt[], ...)
{
	int ret;
	va_list va;
	char datetm[32];
	char buffer[1024];


	va_start(va, fmt);
	ret = vsnprintf(buffer, LEN(buffer), fmt, va);
	va_end(va);

	if (ret <= 0)
		buffer[0] = '\0';

	if ((size_t)ret >= LEN(buffer))
		buffer[LEN(buffer) - 1] = '\0';

	mtx_lock(&_log_mutex); /* LOCK */

	const char *const dt_now = _log_datetime_now(datetm, LEN(datetm));
	if (errnum != 0)
		fprintf(_log_file_err, "ERROR: [%s]: %s: %s\n", dt_now, buffer, strerror(abs(errnum)));
	else
		fprintf(_log_file_err, "ERROR: [%s]: %s\n", dt_now, buffer);

	mtx_unlock(&_log_mutex); /* UNLOCK */
}


void
log_info(const char fmt[], ...)
{
	int ret;
	va_list va;
	char datetm[32];
	char buffer[1024];


	va_start(va, fmt);
	ret = vsnprintf(buffer, LEN(buffer), fmt, va);
	va_end(va);

	if (ret <= 0)
		buffer[0] = '\0';

	if ((size_t)ret >= LEN(buffer))
		buffer[LEN(buffer) - 1] = '\0';

	mtx_lock(&_log_mutex); /* LOCK */

	const char *const dt_now = _log_datetime_now(datetm, LEN(datetm));
	fprintf(_log_file_out, "INFO: [%s]: %s\n", dt_now, buffer);

	mtx_unlock(&_log_mutex); /* UNLOCK */
}

