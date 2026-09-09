#ifndef STDIO_H
#define STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include "unistd.h"

#define EOF (-1)

typedef struct {
    int fd;
    int flags;
    int ungotten;
    char buf[1];
} FILE;

extern FILE __stdio_stdin;
extern FILE __stdio_stdout;
extern FILE __stdio_stderr;

#define stdin  (&__stdio_stdin)
#define stdout (&__stdio_stdout)
#define stderr (&__stdio_stderr)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

void kprint(const char *s);
int printf(const char* format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);
int puts(const char* str);
char *strerror(int errnum);
int putchar(int c);
int fputs(const char *s, FILE *stream);
int fputc(int c, FILE *stream);
int rename(const char *oldpath, const char *newpath);

FILE *fopen(const char *pathname, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *open_memstream(char **ptr, size_t *sizeloc);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fflush(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fgetc(FILE *stream);
int ungetc(int c, FILE *stream);
int remove(const char *pathname);
void perror(const char *s);
int fileno(FILE *stream);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
FILE *tmpfile(void);
int scanf(const char *format, ...);
int fscanf(FILE *stream, const char *format, ...);
int sscanf(const char *str, const char *format, ...);
int vfscanf(FILE *stream, const char *format, va_list ap);
int vsscanf(const char *str, const char *format, va_list ap);
FILE *popen(const char *command, const char *type);
int pclose(FILE *stream);

#endif
