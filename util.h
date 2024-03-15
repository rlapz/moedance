#ifndef __UTIL_H__
#define __UTIL_H__


#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>


#define LEN(X)      (sizeof(X) / sizeof(*X))
#define SET(X, F)   (X |= (F))
#define UNSET(X, F) (X &= ~(F))
#define CHECK(X, F) (X & (F))


/*
 * cstr
 */
void  cstr_copy(char dest[], const char src[]);
//char *cstr_copy_s(char dest[], size_t size, const char src[]);
char *cstr_time_fmt(char dest[], size_t size, int64_t secs);


/*
 * Str
 */
typedef struct str {
	int     is_alloc;
	char   *cstr;
	size_t  size;
	size_t  len;
} Str;

int   str_init(Str *s, char buffer[], size_t size);
int   str_init_alloc(Str *s, size_t len);
void  str_deinit(Str *s);
char *str_set(Str *s, const char cstr[]);
char *str_set_n(Str *s, const char cstr[], size_t len);
char *str_set_fmt(Str *s, const char fmt[], ...);
char *str_append(Str *s, const char cstr[]);
char *str_append_n(Str *s, const char cstr[], size_t len);
char *str_append_fmt(Str *s, const char fmt[], ...);
char *str_dup(Str *s);
int   str_write_all(Str *s, int fd);


/*
 * ArrayPtr
 */
typedef struct {
	size_t      len;
	uintptr_t **items;
} ArrayPtr;

void array_ptr_init(ArrayPtr *a);
void array_ptr_deinit(ArrayPtr *a);
int  array_ptr_append(ArrayPtr *a, void *item);


/*
 * log
 */
void log_file_init(const char path[]);
void log_file_deinit(void);
void log_err(int errnum, const char fmt[], ...);
void log_info(const char fmt[], ...);


#endif

