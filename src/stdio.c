#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "nodeio.h"
#include "errno.h"
#include <stdarg.h>
#include <stdint.h>

#define F_EOF 1
#define F_ERR 2


void kprint(const char *s) {
    int fd = nio_open("/dev/console", O_WRONLY);
    if (fd < 0) return;
    size_t n = strlen(s);
    while (n > 0) {
        ssize_t w = nio_write(fd, s, n);
        if (w <= 0) break;
        s += w;
        n -= (size_t)w;
    }
    nio_close(fd);
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
    /* DIRCTL rename работает внутри одного открытого каталога */
    char obase[128];
    int  fd = nio_open_parent(oldpath, obase, sizeof(obase));
    if (fd < 0) return -1;

    const char *slash = 0;
    for (const char *s = newpath; *s; s++)
        if (*s == '/') slash = s;
    const char *b = slash ? slash + 1 : newpath;
    char nbase[128];
    size_t n = 0;
    while (b[n] && n + 1 < sizeof(nbase)) { nbase[n] = b[n]; n++; }
    nbase[n] = '\0';
    if (!nbase[0]) { nio_close(fd); errno = EINVAL; return -1; }

    cact_rename_arg_t a;
    a.oldname = obase;
    a.newname = nbase;
    int r = nio_ioctl(fd, CACT_DIRCTL_RENAME, &a);
    nio_close(fd);
    return nio_map(r);
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
    if (total == 0) return 0;
    ssize_t r = read(stream->fd, ptr, total);
    if (r < 0) { stream->flags |= F_ERR; return 0; }
    if (r == 0) { stream->flags |= F_EOF; return 0; }
    return (r / size);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    if (total == 0) return 0;
    ssize_t w = write(stream->fd, ptr, total);
    if (w < 0) { stream->flags |= F_ERR; return 0; }
    return (w / size);
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

int feof(FILE *stream) {
    return (stream->flags & F_EOF) != 0;
}

int ferror(FILE *stream) {
    return (stream->flags & F_ERR) != 0;
}

void clearerr(FILE *stream) {
    stream->flags &= ~(F_EOF | F_ERR);
}

int fgetc(FILE *stream) {
    if (stream->ungotten >= 0) {
        int c = stream->ungotten;
        stream->ungotten = -1;
        return c;
    }
    unsigned char c;
    ssize_t r = read(stream->fd, &c, 1);
    if (r == 0) { stream->flags |= F_EOF; return EOF; }
    if (r < 0)  { stream->flags |= F_ERR; return EOF; }
    return c;
}

int ungetc(int c, FILE *stream) {
    stream->ungotten = c;
    return c;
}

int fputc(int c, FILE *stream) {
    unsigned char uc = (unsigned char)c;
    ssize_t w = write(stream->fd, &uc, 1);
    if (w != 1) { stream->flags |= F_ERR; return EOF; }
    return c;
}

int fputs(const char *s, FILE *stream) {
    size_t len = strlen(s);
    ssize_t w = write(stream->fd, s, len);
    if (w < 0) { stream->flags |= F_ERR; return EOF; }
    return 0;
}

int remove(const char *pathname) {
    return unlink(pathname);
}

int fileno(FILE *stream) {
    return stream->fd;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n) return -1;
    if (!*lineptr) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr) return -1;
    }
    size_t len = 0;
    for (;;) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (len == 0) return -1;
            break;
        }
        if (len + 1 >= *n) {
            size_t nn = *n * 2;
            char *np = realloc(*lineptr, nn);
            if (!np) return -1;
            *lineptr = np;
            *n = nn;
        }
        (*lineptr)[len++] = (char)c;
        if (c == '\n') break;
    }
    (*lineptr)[len] = '\0';
    return (ssize_t)len;
}

FILE *tmpfile(void) {
    char t[] = "/tmp/tmpXXXXXX";
    int fd = mkstemp(t);
    if (fd < 0) return 0;
    FILE *f = fdopen(fd, "w+");
    if (!f) close(fd);
    return f;
}

void perror(const char *s) {
    if (s && *s) {
        write(2, s, strlen(s));
        write(2, ": ", 2);
    }
    char *e = strerror(errno);
    write(2, e, strlen(e));
    write(2, "\n", 1);
}


static int _fmt_uint(unsigned long v, unsigned int base, int upper, char *out) {
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    char tmp[40];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (upper ? up : lo)[v % base]; v /= base; }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
    return n;
}

static int _fmt_u64(unsigned long long v, unsigned int base, int upper, char *out) {
    unsigned int hi = (unsigned int)(v >> 32);
    unsigned int lo = (unsigned int)(v & 0xffffffffu);
    static const char lo_d[] = "0123456789abcdef";
    static const char up_d[] = "0123456789ABCDEF";
    const char *dig = upper ? up_d : lo_d;

    if (base == 16) {
        char rev[24];
        int n = 0;
        if (lo == 0) rev[n++] = '0';
        while (lo) { rev[n++] = dig[lo % 16]; lo /= 16; }
        if (hi) {
            while (n < 8) rev[n++] = '0';
            while (hi) { rev[n++] = dig[hi % 16]; hi /= 16; }
        }
        for (int i = 0; i < n; i++) out[i] = rev[n - 1 - i];
        out[n] = '\0';
        return n;
    }

    /* base 10: 64/32-битное деление на unsigned int — одна divl, без libgcc */
    char rev[40];
    int n = 0;
    unsigned int d = 10u;
    if (v == 0) rev[n++] = '0';
    while (v) {
        rev[n++] = '0' + (unsigned int)(v % d);
        v /= d;
    }
    for (int i = 0; i < n; i++) out[i] = rev[n - 1 - i];
    out[n] = '\0';
    return n;
}

int vfprintf(FILE *stream, const char *format, va_list args) {
    for (; *format; format++) {
        if (*format != '%') { fputc(*format, stream); continue; }

        format++;
        int zero = 0, minus = 0, width = 0, islong = 0, isll = 0;
        if (*format == '0') { zero = 1; format++; }
        if (*format == '-') { minus = 1; format++; }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        {
            int nl = 0;
            while (*format == 'l') { nl++; format++; }
            islong = (nl == 1);
            isll   = (nl >= 2);
        }

        char tmp[64];
        int len = 0, neg = 0, sign_len = 0;
        char *sval = 0;
        int cval = 0;

        switch (*format) {
        case 'd': {
            if (isll) {
                long long v = va_arg(args, long long);
                unsigned long long u;
                if (v < 0) { neg = 1; u = (unsigned long long)(-(v + 1)) + 1ULL; }
                else       { u = (unsigned long long)v; }
                len = _fmt_u64(u, 10, 0, tmp);
            } else {
                long v = islong ? va_arg(args, long) : (long)va_arg(args, int);
                if (v < 0) { neg = 1; v = -v; }
                len = _fmt_uint((unsigned long)v, 10, 0, tmp);
            }
            break;
        }
        case 'u': {
            if (isll) {
                unsigned long long v = va_arg(args, unsigned long long);
                len = _fmt_u64(v, 10, 0, tmp);
            } else {
                unsigned long v = islong ? va_arg(args, unsigned long)
                                         : (unsigned long)va_arg(args, unsigned int);
                len = _fmt_uint(v, 10, 0, tmp);
            }
            break;
        }
        case 'o': {
            unsigned long v = islong ? va_arg(args, unsigned long)
                                     : (unsigned long)va_arg(args, unsigned int);
            len = _fmt_uint(v, 8, 0, tmp);
            break;
        }
        case 'x': case 'X': {
            if (isll) {
                unsigned long long v = va_arg(args, unsigned long long);
                len = _fmt_u64(v, 16, *format == 'X', tmp);
            } else {
                unsigned long v = islong ? va_arg(args, unsigned long)
                                         : (unsigned long)va_arg(args, unsigned int);
                len = _fmt_uint(v, 16, *format == 'X', tmp);
            }
            break;
        }
        case 'p': {
            void *v = va_arg(args, void *);
            tmp[0] = '0'; tmp[1] = 'x';
            len = _fmt_uint((unsigned long)(uintptr_t)v, 16, 0, tmp + 2) + 2;
            break;
        }
        case 'c':
            cval = va_arg(args, int);
            len = 1;
            break;
        case 's':
            sval = va_arg(args, char *);
            if (!sval) sval = "(null)";
            len = strlen(sval);
            break;
        case '%':
            fputc('%', stream);
            continue;
        default:
            fputc('%', stream);
            fputc(*format, stream);
            continue;
        }

        if (neg) sign_len = 1;
        int pad = width - (len + sign_len);
        if (pad < 0) pad = 0;

        if (minus) {
            if (neg) fputc('-', stream);
        } else if (zero) {
            if (neg) fputc('-', stream);
            for (int i = 0; i < pad; i++) fputc('0', stream);
        } else {
            for (int i = 0; i < pad; i++) fputc(' ', stream);
            if (neg) fputc('-', stream);
        }

        if (*format == 'c') {
            fputc(cval, stream);
        } else if (*format == 's') {
            for (int i = 0; i < len; i++) fputc(sval[i], stream);
        } else {
            for (int i = 0; i < len; i++) fputc(tmp[i], stream);
        }
        if (minus) {
            for (int i = 0; i < pad; i++) fputc(' ', stream);
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

static void _buf_putn(char **pp, size_t *pos, size_t size, const char *s, int len) {
    for (int i = 0; i < len; i++) _buf_putc(pp, pos, size, s[i]);
}

static void _buf_vfmt(char **pp, size_t *pos, size_t size, const char *format, va_list args) {
    for (; *format; format++) {
        if (*format != '%') { _buf_putc(pp, pos, size, *format); continue; }

        format++;
        int zero = 0, minus = 0, width = 0, islong = 0, isll = 0;
        if (*format == '0') { zero = 1; format++; }
        if (*format == '-') { minus = 1; format++; }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        {
            int nl = 0;
            while (*format == 'l') { nl++; format++; }
            islong = (nl == 1);
            isll   = (nl >= 2);
        }

        char tmp[64];
        int len = 0, neg = 0, sign_len = 0;
        const char *sval = 0;
        int cval = 0;

        switch (*format) {
        case 'd': {
            if (isll) {
                long long v = va_arg(args, long long);
                unsigned long long u;
                if (v < 0) { neg = 1; u = (unsigned long long)(-(v + 1)) + 1ULL; }
                else       { u = (unsigned long long)v; }
                len = _fmt_u64(u, 10, 0, tmp);
            } else {
                long v = islong ? va_arg(args, long) : (long)va_arg(args, int);
                if (v < 0) { neg = 1; v = -v; }
                len = _fmt_uint((unsigned long)v, 10, 0, tmp);
            }
            break;
        }
        case 'u': {
            if (isll) {
                unsigned long long v = va_arg(args, unsigned long long);
                len = _fmt_u64(v, 10, 0, tmp);
            } else {
                unsigned long v = islong ? va_arg(args, unsigned long)
                                         : (unsigned long)va_arg(args, unsigned int);
                len = _fmt_uint(v, 10, 0, tmp);
            }
            break;
        }
        case 'o': {
            unsigned long v = islong ? va_arg(args, unsigned long)
                                     : (unsigned long)va_arg(args, unsigned int);
            len = _fmt_uint(v, 8, 0, tmp);
            break;
        }
        case 'x': case 'X': {
            if (isll) {
                unsigned long long v = va_arg(args, unsigned long long);
                len = _fmt_u64(v, 16, *format == 'X', tmp);
            } else {
                unsigned long v = islong ? va_arg(args, unsigned long)
                                         : (unsigned long)va_arg(args, unsigned int);
                len = _fmt_uint(v, 16, *format == 'X', tmp);
            }
            break;
        }
        case 'p': {
            void *v = va_arg(args, void *);
            tmp[0] = '0'; tmp[1] = 'x';
            len = _fmt_uint((unsigned long)(uintptr_t)v, 16, 0, tmp + 2) + 2;
            break;
        }
        case 'c':
            cval = va_arg(args, int);
            len = 1;
            break;
        case 's':
            sval = va_arg(args, const char *);
            if (!sval) sval = "(null)";
            len = strlen(sval);
            break;
        case '%':
            _buf_putc(pp, pos, size, '%');
            continue;
        default:
            _buf_putc(pp, pos, size, '%');
            _buf_putc(pp, pos, size, *format);
            continue;
        }

        if (neg) sign_len = 1;
        int pad = width - (len + sign_len);
        if (pad < 0) pad = 0;

        if (minus) {
            if (neg) _buf_putc(pp, pos, size, '-');
        } else if (zero) {
            if (neg) _buf_putc(pp, pos, size, '-');
            while (pad-- > 0) _buf_putc(pp, pos, size, '0');
        } else {
            while (pad-- > 0) _buf_putc(pp, pos, size, ' ');
            if (neg) _buf_putc(pp, pos, size, '-');
        }

        if (*format == 'c') {
            _buf_putc(pp, pos, size, cval);
        } else if (*format == 's') {
            _buf_putn(pp, pos, size, sval, len);
        } else {
            _buf_putn(pp, pos, size, tmp, len);
        }
        if (minus) {
            while (pad-- > 0) _buf_putc(pp, pos, size, ' ');
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

float strtof(const char *str, char **endptr) {
    return (float)strtod(str, endptr);
}

#define POPEN_MAX 8
static FILE *_pop_f[POPEN_MAX];
static pid_t _pop_pid[POPEN_MAX];

FILE *popen(const char *command, const char *type) {
    if (!command || !type || (type[0] != 'r' && type[0] != 'w') || type[1] == '+')
        return 0;
    if (access("/bin/cactsole", X_OK) != 0) {
        errno = ENOENT;
        return 0;
    }
    int fds[2];
    if (pipe(fds) != 0) return 0;
    int wr = (type[0] == 'w');

    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return 0; }
    if (pid == 0) {
        if (wr) dup2(fds[0], 0);
        else    dup2(fds[1], 1);
        close(fds[0]);
        close(fds[1]);
        char *argv[] = { "cactsole", "-c", (char *)command, 0 };
        execve("/bin/cactsole", argv, environ);
        _exit(127);
    }

    int keep = wr ? fds[1] : fds[0];
    close(wr ? fds[0] : fds[1]);

    FILE *f = fdopen(keep, wr ? "w" : "r");
    if (!f) {
        close(keep);
        waitpid(pid, 0, 0);
        return 0;
    }
    for (int i = 0; i < POPEN_MAX; i++) {
        if (_pop_pid[i] == 0) {
            _pop_f[i] = f;
            _pop_pid[i] = pid;
            break;
        }
    }
    return f;
}

int pclose(FILE *stream) {
    pid_t pid = 0;
    for (int i = 0; i < POPEN_MAX; i++) {
        if (_pop_f[i] == stream) {
            pid = _pop_pid[i];
            _pop_f[i] = 0;
            _pop_pid[i] = 0;
            break;
        }
    }
    int st = fclose(stream);
    if (pid) {
        int s = 0;
        waitpid(pid, &s, 0);
        return s;
    }
    return st;
}

/* CactOS: no dynamic memory FILEs. open_memstream() is used only by debug
 * logging (WAYLAND_DEBUG); returning NULL makes callers bail out. */
FILE *open_memstream(char **ptr, size_t *sizeloc) {
    (void)ptr;
    (void)sizeloc;
    return 0;
}
