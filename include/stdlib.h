#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>

#define RAND_MAX 32767

typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;

void exit(int status);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

int atexit(void (*function)(void));
void abort(void);
char *getenv(const char *name);
int system(const char *command);
int putenv(char *string);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

int atoi(const char *str);
long atol(const char *str);
long long atoll(const char *str);
void itoa(int n, char str[]);
void hex_to_ascii(unsigned int n, char str[]);
double atof(const char *str);
double strtod(const char *str, char **endptr);
unsigned long strtoul(const char *str, char **endptr, int base);
long strtol(const char *str, char **endptr, int base);
long long strtoll(const char *str, char **endptr, int base);
unsigned long long strtoull(const char *str, char **endptr, int base);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));
typedef int (*__compar_d_fn_t)(const void *, const void *, void *);
void qsort_r(void *base, size_t nmemb, size_t size, __compar_d_fn_t compar, void *arg);

int abs(int x);
long labs(long x);
long long llabs(long long x);
int rand(void);
void srand(unsigned seed);
div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);

int mkstemp(char *tmpl);
int mkostemp(char *tmpl, int flags);

char *realpath(const char *path, char *resolved_path);

#endif
