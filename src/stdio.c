#include "stdio.h"
#include "syscall.h"
#include "string.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include <stdarg.h>
#include <stdint.h>


void kprint(const char *s) {
    syscall(SYS_PRINT, (uintptr_t)s, 0, 0);
}

int putchar(int c) {
    char buf = (char)c;
    write(STDOUT_FILENO, &buf, 1);
    return c;
}

int puts(const char *str) {
    int len = strlen(str);
    write(STDOUT_FILENO, str, len);
    char nl = '\n';
    write(STDOUT_FILENO, &nl, 1);
    return 0;
}

int rename(const char *oldpath, const char *newpath) {
    return (int)syscall(SYS_RENAME, (uintptr_t)oldpath, (uintptr_t)newpath, 0);
}


FILE __stdio_stdin  = { .fd = 0, .flags = 0, .ungotten = -1, .buf = {0} };
FILE __stdio_stdout = { .fd = 1, .flags = 0, .ungotten = -1, .buf = {0} };
FILE __stdio_stderr = { .fd = 2, .flags = 0, .ungotten = -1, .buf = {0} };

FILE *fopen(const char *pathname, const char *mode) {
    int flags = O_RDONLY;
    if (mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a') flags = O_WRONLY | O_CREAT | O_APPEND;
    else if (mode[0] == 'r') flags = O_RDONLY;
    if (mode[0] == 'r' && mode[1] == '+') flags = O_RDWR;
    else if (mode[0] == 'w' && mode[1] == '+') flags = O_RDWR | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a' && mode[1] == '+') flags = O_RDWR | O_CREAT | O_APPEND;

    int fd = open(pathname, flags);
    if (fd < 0) return 0;

    FILE *f = malloc(sizeof(FILE));
    if (!f) { close(fd); return 0; }
    f->fd = fd;
    f->flags = 0;
    f->ungotten = -1;
    return f;
}

FILE *fdopen(int fd, const char *mode) {
    (void)mode;
    FILE *f = malloc(sizeof(FILE));
    if (!f) return 0;
    f->fd = fd;
    f->flags = 0;
    f->ungotten = -1;
    return f;
}

int fclose(FILE *stream) {
    if (stream == stdin || stream == stdout || stream == stderr)
        return 0;
    int ret = close(stream->fd);
    free(stream);
    return ret;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    ssize_t r = read(stream->fd, ptr, total);
    return (r > 0) ? (r / size) : 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    ssize_t w = write(stream->fd, ptr, total);
    return (w > 0) ? (w / size) : 0;
}

int fseek(FILE *stream, long offset, int whence) {
    off_t r = lseek(stream->fd, offset, whence);
    return (r < 0) ? -1 : 0;
}

long ftell(FILE *stream) {
    return (long)lseek(stream->fd, 0, SEEK_CUR);
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

char *fgets(char *s, int size, FILE *stream) {
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return 0;
    s[i] = '\0';
    return s;
}

int fgetc(FILE *stream) {
    if (stream->ungotten >= 0) {
        int c = stream->ungotten;
        stream->ungotten = -1;
        return c;
    }
    unsigned char c;
    ssize_t r = read(stream->fd, &c, 1);
    if (r <= 0) return EOF;
    return c;
}

int ungetc(int c, FILE *stream) {
    stream->ungotten = c;
    return c;
}

int fputc(int c, FILE *stream) {
    unsigned char uc = (unsigned char)c;
    ssize_t w = write(stream->fd, &uc, 1);
    return (w == 1) ? c : EOF;
}

int fputs(const char *s, FILE *stream) {
    size_t len = strlen(s);
    ssize_t w = write(stream->fd, s, len);
    return (w >= 0) ? 0 : EOF;
}

int remove(const char *pathname) {
    return unlink(pathname);
}

void perror(const char *s) {
    write(2, s, strlen(s));
    write(2, ": error\n", 7);
}


static void _print_str(FILE *f, const char *s) {
    fputs(s, f);
}

static void _print_dec(FILE *f, int val) {
    char buf[32];
    int i = 0, neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) { buf[i++] = '0'; }
    else { while (val > 0) { buf[i++] = '0' + val % 10; val /= 10; } }
    if (neg) buf[i++] = '-';
    while (i > 0) fputc(buf[--i], f);
}

static void _print_hex(FILE *f, unsigned int val) {
    char buf[32];
    int i = 0;
    if (val == 0) { buf[i++] = '0'; }
    else { while (val > 0) { buf[i++] = "0123456789abcdef"[val % 16]; val /= 16; } }
    while (i > 0) fputc(buf[--i], f);
}

int vfprintf(FILE *stream, const char *format, va_list args) {
    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] == '%') {
            i++;
            if (format[i] == 'd') {
                int val = va_arg(args, int);
                _print_dec(stream, val);
            } else if (format[i] == 'u') {
                unsigned int val = va_arg(args, unsigned int);
                _print_dec(stream, (int)val);
            } else if (format[i] == 's') {
                char *val = va_arg(args, char *);
                _print_str(stream, val ? val : "(null)");
            } else if (format[i] == 'x' || format[i] == 'X') {
                unsigned int val = va_arg(args, unsigned int);
                _print_hex(stream, val);
            } else if (format[i] == 'c') {
                int val = va_arg(args, int);
                fputc(val, stream);
            } else if (format[i] == 'l') {
                i++;
                if (format[i] == 'd') {
                    long val = va_arg(args, long);
                    _print_dec(stream, (int)val);
                } else if (format[i] == 'u') {
                    unsigned long val = va_arg(args, unsigned long);
                    _print_dec(stream, (int)val);
                } else if (format[i] == 'x' || format[i] == 'X') {
                    unsigned long val = va_arg(args, unsigned long);
                    _print_hex(stream, (unsigned int)val);
                }
            } else if (format[i] == 'p') {
                void *val = va_arg(args, void *);
                fputc('0', stream); fputc('x', stream);
                _print_hex(stream, (unsigned int)(uintptr_t)val);
            } else if (format[i] == '%') {
                fputc('%', stream);
            }
        } else {
            fputc(format[i], stream);
        }
    }
    return 0;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vfprintf(stream, format, args);
    va_end(args);
    return ret;
}

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vfprintf(stdout, format, args);
    va_end(args);
    return ret;
}

static void _buf_putc(char **pp, size_t *pos, size_t size, int c) {
    if (*pos < size - 1) {
        *(*pp)++ = (char)c;
    }
    (*pos)++;
}

static void _buf_puts(char **pp, size_t *pos, size_t size, const char *s) {
    while (*s)
        _buf_putc(pp, pos, size, *s++);
}

static void _buf_putdec(char **pp, size_t *pos, size_t size, int val) {
    char buf[32];
    int i = 0, neg = 0;
    unsigned int v;
    if (val < 0) { neg = 1; v = (unsigned int)-val; }
    else         { v = (unsigned int)val; }
    if (v == 0) { buf[i++] = '0'; }
    else { while (v > 0) { buf[i++] = '0' + v % 10; v /= 10; } }
    if (neg) buf[i++] = '-';
    while (i > 0) _buf_putc(pp, pos, size, buf[--i]);
}

static void _buf_puthex(char **pp, size_t *pos, size_t size, unsigned int val, int upper) {
    char buf[32];
    int i = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (val == 0) { buf[i++] = '0'; }
    else { while (val > 0) { buf[i++] = digits[val % 16]; val /= 16; } }
    while (i > 0) _buf_putc(pp, pos, size, buf[--i]);
}

static void _buf_vfmt(char **pp, size_t *pos, size_t size, const char *format, va_list args) {
    for (; *format; format++) {
        if (*format == '%') {
            format++;
            unsigned int flen = 1;
            int is_long = 0;
            /* simple flag parsing */
            while (*format == 'l') { is_long = 1; format++; flen++; }
            if (*format == 'u') { format++; flen++; }
            if (*format == 'z') { format++; flen++; } /* skip z (size_t) */
            /* re-check for l after u */
            while (*format == 'l') { is_long = 1; format++; flen++; }

            switch (*format) {
            case 'd': {
                if (is_long) _buf_putdec(pp, pos, size, (int)va_arg(args, long));
                else         _buf_putdec(pp, pos, size, va_arg(args, int));
                break;
            }
            case 'u': {
                if (is_long) _buf_putdec(pp, pos, size, (int)va_arg(args, unsigned long));
                else         _buf_putdec(pp, pos, size, (int)va_arg(args, unsigned int));
                break;
            }
            case 's': {
                char *s = va_arg(args, char *);
                _buf_puts(pp, pos, size, s ? s : "(null)");
                break;
            }
            case 'x': case 'X': {
                unsigned int v;
                if (is_long) v = (unsigned int)va_arg(args, unsigned long);
                else         v = va_arg(args, unsigned int);
                _buf_puthex(pp, pos, size, v, *format == 'X');
                break;
            }
            case 'p': {
                void *val = va_arg(args, void *);
                _buf_puts(pp, pos, size, "0x");
                _buf_puthex(pp, pos, size, (unsigned int)(uintptr_t)val, 0);
                break;
            }
            case 'c': {
                int c = va_arg(args, int);
                _buf_putc(pp, pos, size, c);
                break;
            }
            case '%':
                _buf_putc(pp, pos, size, '%');
                break;
            default:
                _buf_putc(pp, pos, size, '%');
                if (flen > 1) format -= (flen - 1);
                break;
            }
        } else {
            _buf_putc(pp, pos, size, *format);
        }
    }
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    size_t pos = 0;
    char *p = str;
    if (size == 0) return 0;
    _buf_vfmt(&p, &pos, size, format, ap);
    if (pos < size) str[pos] = '\0';
    else            str[size - 1] = '\0';
    return (int)pos;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(str, size, format, args);
    va_end(args);
    return ret;
}

int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf(str, (size_t)-1, format, ap);
}

int sprintf(char *str, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(str, (size_t)-1, format, args);
    va_end(args);
    return ret;
}

char *strerror(int errnum) {
    (void)errnum;
    return "Unknown error";
}

float strtof(const char *str, char **endptr) {
    return (float)strtod(str, endptr);
}
