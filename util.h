#ifndef __UTIL_H__
#define __UTIL_H__


#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>


#define LEN(X)      (sizeof(X) / sizeof(*X))
#define SET(X, F)   (X |= (F))
#define UNSET(X, F) (X &= ~(F))
#define ISSET(X, F) (X & (F))

#define ALT_NULL(X, Y)  ((X != NULL)? X : Y)
#define ALT_EMPTY(X, Y) ((X[0] != '\0')? X : Y)


/*
 * cstr
 */
void  cstr_copy_n(char dest[], size_t dest_size, const char src[], size_t src_len);
char *cstr_time_fmt(char dest[], size_t size, int64_t secs);
int   cstr_cmp_vers(const char a[], const char b[]);
char *cstr_case_str(const char h[], const char n[]);
int   cstr_is_empty(const char cstr[]);

char   *cstr_trim_left(char cstr[]);
size_t  cstr_trim_right_len(const char cstr[], size_t len);

int cstr_to_int64(const char cstr[], int64_t *out);


/*
 * Str
 */
typedef struct str {
	int     is_alloc;
	char   *cstr;
	size_t  size;
	size_t  len;
} Str;

int         str_init(Str *s, char buffer[], size_t size);
int         str_init_alloc(Str *s, size_t len);
void        str_deinit(Str *s);
const char *str_set(Str *s, const char cstr[]);
const char *str_set_n(Str *s, const char cstr[], size_t len);
const char *str_set_fmt(Str *s, const char fmt[], ...);
const char *str_append(Str *s, const char cstr[]);
const char *str_append_n(Str *s, const char cstr[], size_t len);
const char *str_append_fmt(Str *s, const char fmt[], ...);
char       *str_dup(Str *s);
void        str_shrink(Str *s, size_t count);


/*
 * ArrayPtr
 */
typedef struct array_ptr {
	size_t   len;
	void   **items;
} ArrayPtr;

void array_ptr_init(ArrayPtr *a);
void array_ptr_deinit(ArrayPtr *a);
int  array_ptr_append(ArrayPtr *a, void *item);


/*
 * Stream
 */
void stream_in_flush(int fd);


/*
 * SpaceTokenizer
 */
typedef struct space_tokenizer {
	const char *value;
	unsigned    len;
} SpaceTokenizer;

const char *space_tokenizer_next(SpaceTokenizer *s, const char raw[]);


/*
 * misc
 */
int is_ascii(int c);


/*
 * log
 */
int  log_file_init(const char path[]);
void log_file_deinit(void);
void log_err(int errnum, const char fmt[], ...);
void log_info(const char fmt[], ...);


#endif

