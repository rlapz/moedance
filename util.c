#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "util.h"


/*
 * cstr
 */
void
cstr_copy(char dest[], const char src[])
{
	const size_t slen = strlen(src);
	memcpy(dest, src, slen);
	dest[slen] = '\0';
}


char *
cstr_copy_s(char dest[], size_t size, const char src[])
{
	if (size == 0)
		return NULL;

	if (size == 1) {
		dest[0] = '\0';
		return dest;
	}

	memcpy(dest, src, size - 1);
	dest[size] = '\0';
	return dest;
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
		cstr_copy(dest, "??:??:??");

	return dest;
}


/*
 * Str
 */
static int
_str_resize(Str *s, size_t slen)
{
	size_t remn_size = 0;
	const size_t size = s->size;
	if (size > slen)
		remn_size = size - s->len;

	if (slen >= remn_size) {
		if (s->is_alloc == 0)
			return -ENOMEM;

		const size_t _rsize = (slen - remn_size) + size + 1;
		char *const _new_cstr = realloc(s->cstr, _rsize);
		if (_new_cstr == NULL)
			return -errno;

		s->size = _rsize;
		s->cstr = _new_cstr;

	}

	return 0;
}


int
str_init(Str *s, char buffer[], size_t size)
{
	if (size == 0)
		return -EINVAL;

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


char *
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

	memcpy(s->cstr, cstr, cstr_len);
	s->len = cstr_len;
	s->cstr[cstr_len] = '\0';
	return s->cstr;
}


char *
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


char *
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


char *
str_append(Str *s, const char cstr[])
{
	const size_t cstr_len = strlen(cstr);
	if (cstr_len == 0)
		return s->cstr;

	if (_str_resize(s, cstr_len) < 0)
		return NULL;

	size_t len = s->len;
	memcpy(s->cstr + len, cstr, cstr_len);

	len += cstr_len;
	s->len = len;
	s->cstr[len] = '\0';
	return s->cstr;
}


char *
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


char *
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
	const size_t len = s->len + 1;
	char *const ret = malloc(len);
	if (ret == NULL)
		return NULL;

	return memcpy(ret, s->cstr, len);
}


int
str_write_all(Str *s, int fd)
{
	const size_t len = s->len;
	const char *const cstr = s->cstr;
	for (size_t i = 0; i < len;) {
		const ssize_t w = write(fd, cstr + i, len - i);
		if (w < 0)
			return -1;

		if (w == 0)
			break;

		i += (size_t)w;
	}

	return 0;
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
	uintptr_t **const items = realloc(a->items, sizeof(uintptr_t *) * new_len);
	if (items == NULL)
		return -errno;

	items[a->len] = item;
	a->len = new_len;
	a->items = items;
	return 0;
}


/*
 * Log
 */
static FILE *_log_file_out = NULL;


static inline const char *
_log_datetime_now(char dest[], size_t size)
{
	const time_t tm_raw = time(NULL);
	struct tm *const tm = localtime(&tm_raw);
	if (tm == NULL)
		goto out0;

	const char *const res = asctime(tm);
	if (res == NULL)
		goto out0;

	const size_t res_len = strlen(res);
	if ((res_len == 0) || (res_len >= size))
		goto out0;

	memcpy(dest, res, res_len - 1);
	dest[res_len - 1] = '\0';
	return dest;

out0:
	return "???";
}


void
log_file_init(const char path[])
{
	if (_log_file_out == NULL)
		_log_file_out = fopen(path, "a+");

	if (_log_file_out != NULL)
		log_info("starting...");
}


void
log_file_deinit(void)
{
	if (_log_file_out != NULL) {
		log_info("stopped");
		fclose(_log_file_out);
	}
}


void
log_err(int errnum, const char fmt[], ...)
{
	int ret;
	FILE *out = _log_file_out;
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

	const char *const dt_now = _log_datetime_now(datetm, LEN(datetm));
	if (out == NULL)
		out = stderr;

	if (errnum != 0)
		fprintf(out, "ERROR: [%s]: %s: %s\n", dt_now, buffer, strerror(abs(errnum)));
	else
		fprintf(out, "ERROR: [%s]: %s\n", dt_now, buffer);

	fflush(out);
}


void
log_info(const char fmt[], ...)
{
	int ret;
	FILE *out = _log_file_out;
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

	const char *const dt_now = _log_datetime_now(datetm, LEN(datetm));
	fprintf((out == NULL)? stdout:out, "INFO: [%s]: %s\n", dt_now, buffer);

	fflush(out);
}

