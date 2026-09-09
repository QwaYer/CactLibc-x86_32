#include "stdlib.h"
#include "syscall.h"
#include "string.h"
#include "unistd.h"
#include "signal.h"
#include "fcntl.h"
#include "stat.h"
#include "errno.h"
#include "random.h"
#include <stdint.h>

#define ATEXIT_MAX 32
static void (*_atexit_handlers[ATEXIT_MAX])(void);
static int  _atexit_count = 0;

int atexit(void (*function)(void)) {
    if (!function || _atexit_count >= ATEXIT_MAX) {
        errno = ENOMEM;
        return -1;
    }
    _atexit_handlers[_atexit_count++] = function;
    return 0;
}

void exit(int status) {
    while (_atexit_count > 0)
        _atexit_handlers[--_atexit_count]();
    syscall(SYS_EXIT, status, 0, 0);
    while(1);
}

void abort(void) {
    kill(getpid(), SIGABRT);
    _exit(134);
}

static unsigned long _rand_state = 1;

int rand(void) {
    _rand_state = _rand_state * 1103515245UL + 12345UL;
    return (int)((_rand_state >> 16) & 0x7FFF);
}

void srand(unsigned seed) {
    _rand_state = seed ? seed : 1;
}

int abs(int x) {
    return x < 0 ? -x : x;
}

long labs(long x) {
    return x < 0 ? -x : x;
}

div_t div(int numer, int denom) {
    div_t r;
    r.quot = numer / denom;
    r.rem  = numer % denom;
    return r;
}

ldiv_t ldiv(long numer, long denom) {
    ldiv_t r;
    r.quot = numer / denom;
    r.rem  = numer % denom;
    return r;
}

char *getenv(const char *name) {
    if (!name || !*name) return 0;
    size_t n = 0;
    while (name[n]) n++;
    if (!environ) return 0;
    for (int i = 0; environ[i]; i++) {
        if (strncmp(environ[i], name, n) == 0 && environ[i][n] == '=')
            return environ[i] + n + 1;
    }
    return 0;
}

int system(const char *command) {
    if (!command) return 1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[] = { "cactsole", "-c", (char *)command, 0 };
        execve("/bin/cactsole", argv, environ);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}


#define BLOCK_MAGIC 0xA110CA7E
#define ALIGN8(x)  (((x) + 7) & ~7)
#define BLOCK_SIZE  sizeof(struct block_header)

struct block_header {
    unsigned int magic;
    unsigned int size;
    unsigned int is_free;
    struct block_header *next;
};

static struct block_header *free_list = 0;
static int heap_initialized = 0;

static void heap_init(void) {
    if (heap_initialized) return;
    heap_initialized = 1;
    free_list = 0;
}

static struct block_header *find_free_block(unsigned int size) {
    struct block_header *cur = free_list;
    while (cur) {
        if (cur->is_free && cur->size >= size)
            return cur;
        cur = cur->next;
    }
    return 0;
}

static struct block_header *request_space(unsigned int size) {
    unsigned int total = BLOCK_SIZE + size;
    unsigned int request = total;
    if (request < 4096) request = 4096;

    uintptr_t cur_brk = (uintptr_t)syscall(SYS_BRK, 0, 0, 0);
    if (cur_brk == UINTPTR_MAX) return 0;

    uintptr_t req_brk = cur_brk + (uintptr_t)request;
    if (req_brk < cur_brk) return 0;
    uintptr_t new_brk = (uintptr_t)syscall(SYS_BRK, req_brk, 0, 0);
    if (new_brk == UINTPTR_MAX) return 0;

    struct block_header *block = (struct block_header *)cur_brk;
    block->magic   = BLOCK_MAGIC;
    block->size    = request - BLOCK_SIZE;
    block->is_free = 0;
    block->next    = 0;

    if (block->size > size + BLOCK_SIZE + 16) {
        struct block_header *remainder = (struct block_header *)
            ((char *)block + BLOCK_SIZE + size);
        remainder->magic   = BLOCK_MAGIC;
        remainder->size    = block->size - size - BLOCK_SIZE;
        remainder->is_free = 1;
        remainder->next    = 0;
        block->size = size;
        block->next = remainder;

        if (!free_list) {
            free_list = block;
        } else {
            struct block_header *last = free_list;
            while (last->next) last = last->next;
            last->next = block;
        }
        return block;
    }

    if (!free_list) {
        free_list = block;
    } else {
        struct block_header *last = free_list;
        while (last->next) last = last->next;
        last->next = block;
    }
    return block;
}

void *malloc(size_t size) {
    if (size == 0) return 0;
    heap_init();

    unsigned int aligned = ALIGN8((unsigned int)size);

    struct block_header *block = find_free_block(aligned);
    if (block) {
        if (block->size > aligned + BLOCK_SIZE + 16) {
            struct block_header *remainder = (struct block_header *)
                ((char *)block + BLOCK_SIZE + aligned);
            remainder->magic   = BLOCK_MAGIC;
            remainder->size    = block->size - aligned - BLOCK_SIZE;
            remainder->is_free = 1;
            remainder->next    = block->next;
            block->size = aligned;
            block->next = remainder;
        }
        block->is_free = 0;
        return (void *)((char *)block + BLOCK_SIZE);
    }

    block = request_space(aligned);
    if (!block) return 0;
    block->is_free = 0;
    return (void *)((char *)block + BLOCK_SIZE);
}

void free(void *ptr) {
    if (!ptr) return;
    struct block_header *block = (struct block_header *)
        ((char *)ptr - BLOCK_SIZE);
    if (block->magic != BLOCK_MAGIC) return;
    block->is_free = 1;

    struct block_header *cur = free_list;
    while (cur) {
        if (cur->is_free && cur->next && cur->next->is_free) {
            cur->size += BLOCK_SIZE + cur->next->size;
            cur->next = cur->next->next;
            continue; 
        }
        cur = cur->next;
    }
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }

    struct block_header *block = (struct block_header *)
        ((char *)ptr - BLOCK_SIZE);
    if (block->magic != BLOCK_MAGIC) return 0;
    if (block->size >= (unsigned int)size) return ptr;

    void *new_ptr = malloc(size);
    if (!new_ptr) return 0;
    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}

double strtod(const char *str, char **endptr) {
    double result = 0.0;
    int sign = 1, i = 0;
    while (str[i] == ' ') i++;
    if (str[i] == '-') { sign = -1; i++; }
    else if (str[i] == '+') i++;
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10.0 + (str[i] - '0');
        i++;
    }
    if (str[i] == '.') {
        i++;
        double frac = 1.0;
        while (str[i] >= '0' && str[i] <= '9') {
            frac /= 10.0;
            result += frac * (str[i] - '0');
            i++;
        }
    }
    if (endptr) *endptr = (char*)(str + i);
    return sign * result;
}

double atof(const char *str) {
    return strtod(str, 0);
}

unsigned long strtoul(const char *str, char **endptr, int base) {
    unsigned long result = 0;
    int i = 0;
    while (str[i] == ' ') i++;
    if (base == 0) {
        if (str[i] == '0') {
            if (str[i+1] == 'x' || str[i+1] == 'X') { base = 16; i += 2; }
            else base = 8;
        } else {
            base = 10;
        }
    }
    if (base == 16 && str[i] == '0' && (str[i+1] == 'x' || str[i+1] == 'X')) i += 2;
    while (str[i]) {
        int d;
        if (str[i] >= '0' && str[i] <= '9') d = str[i] - '0';
        else if (str[i] >= 'a' && str[i] <= 'f') d = str[i] - 'a' + 10;
        else if (str[i] >= 'A' && str[i] <= 'F') d = str[i] - 'A' + 10;
        else break;
        if (d >= base) break;
        result = result * base + d;
        i++;
    }
    if (endptr) *endptr = (char*)(str + i);
    return result;
}

static void _qsort_swap(void *a, void *b, size_t size) {
    unsigned char tmp[64];
    unsigned char *pa = (unsigned char*)a, *pb = (unsigned char*)b;
    for (size_t i = 0; i < size; i += sizeof(tmp)) {
        size_t chunk = size - i;
        if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
        for (size_t j = 0; j < chunk; j++) tmp[j] = pa[i+j];
        for (size_t j = 0; j < chunk; j++) pa[i+j] = pb[i+j];
        for (size_t j = 0; j < chunk; j++) pb[i+j] = tmp[j];
    }
}

static void _qsort_r(void *base, size_t nmemb, size_t size,
                     __compar_d_fn_t compar, void *arg) {
    if (nmemb <= 1) return;
    char *arr = (char *)base;
    size_t last = nmemb - 1;
    size_t pivot = 0;
    for (size_t i = 0; i < last; i++) {
        if (compar(arr + i * size, arr + last * size, arg) < 0) {
            _qsort_swap(arr + i * size, arr + pivot * size, size);
            pivot++;
        }
    }
    _qsort_swap(arr + pivot * size, arr + last * size, size);
    if (pivot > 1) _qsort_r(arr, pivot, size, compar, arg);
    if (pivot < nmemb - 1)
        _qsort_r(arr + (pivot + 1) * size, nmemb - pivot - 1, size, compar, arg);
}

static int _qsort_ctx(const void *a, const void *b, void *arg) {
    return ((int (*)(const void *, const void *))arg)(a, b);
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    _qsort_r(base, nmemb, size, _qsort_ctx, (void *)compar);
}

void qsort_r(void *base, size_t nmemb, size_t size, __compar_d_fn_t compar, void *arg) {
    _qsort_r(base, nmemb, size, compar, arg);
}

long strtol(const char *str, char **endptr, int base) {
    int i = 0, sign = 1;
    while (str[i] == ' ') i++;
    if (str[i] == '-') { sign = -1; i++; }
    else if (str[i] == '+') i++;
    return (long)strtoul(str + i, endptr, base) * sign;
}
#define PATHBUF_MAX 512

static void _rp_remove_last(char *out) {
    size_t n = strlen(out);
    if (n <= 1) return;
    while (n > 1 && out[n - 1] == '/') out[--n] = '\0';
    while (n > 1 && out[n - 1] != '/') out[--n] = '\0';
    if (n > 1) out[n - 1] = '\0';
}

char *realpath(const char *path, char *resolved_path) {
    if (!path || !*path) { errno = ENOENT; return 0; }

    char *out = resolved_path ? resolved_path : malloc(PATHBUF_MAX);
    if (!out) { errno = ENOMEM; return 0; }

    const char *p = path;
    if (p[0] == '/') {
        out[0] = '/';
        out[1] = '\0';
    } else {
        if (!getcwd(out, PATHBUF_MAX)) { errno = ENOENT; return 0; }
    }

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        char name[256];
        size_t k = 0;
        while (*p && *p != '/' && k < sizeof(name) - 1) name[k++] = *p++;
        name[k] = '\0';

        if (k == 1 && name[0] == '.') continue;
        if (k == 2 && name[0] == '.' && name[1] == '.') {
            _rp_remove_last(out);
            continue;
        }
        if (k == 0) continue;

        if (strlen(out) + k + 2 > PATHBUF_MAX) { errno = ENAMETOOLONG; goto fail; }
        if (strcmp(out, "/") != 0) strcat(out, "/");
        strcat(out, name);
    }

    return out;

fail:
    if (!resolved_path) free(out);
    return 0;
}

int mkstemp(char *tmpl) {
    return mkostemp(tmpl, 0);
}

int mkostemp(char *tmpl, int flags) {
    if (!tmpl) { errno = EINVAL; return -1; }
    size_t n = strlen(tmpl);
    if (n < 6 || strcmp(tmpl + n - 6, "XXXXXX") != 0) { errno = EINVAL; return -1; }

    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int attempt = 0; attempt < 128; attempt++) {
        unsigned char rnd[6];
        if (getrandom(rnd, sizeof(rnd), 0) != (int)sizeof(rnd)) break;
        for (int i = 0; i < 6; i++) tmpl[n - 6 + i] = chars[rnd[i] % (sizeof(chars) - 1)];
        int fd = open(tmpl, O_RDWR | O_CREAT | flags);
        if (fd >= 0) return fd;
    }
    errno = EEXIST;
    return -1;
}

unsigned long long strtoull(const char *str, char **endptr, int base) {
    unsigned long long result = 0;
    int i = 0;
    while (str[i] == ' ') i++;
    if (base == 16 && str[i] == '0' && (str[i+1] == 'x' || str[i+1] == 'X')) i += 2;
    while (str[i]) {
        int d;
        if (str[i] >= '0' && str[i] <= '9') d = str[i] - '0';
        else if (str[i] >= 'a' && str[i] <= 'f') d = str[i] - 'a' + 10;
        else if (str[i] >= 'A' && str[i] <= 'F') d = str[i] - 'A' + 10;
        else break;
        if (d >= base) break;
        result = result * base + d;
        i++;
    }
    if (endptr) *endptr = (char*)(str + i);
    return result;
}

long long strtoll(const char *str, char **endptr, int base) {
    int i = 0, sign = 1;
    while (str[i] == ' ') i++;
    if (str[i] == '-') { sign = -1; i++; }
    else if (str[i] == '+') i++;
    return (long long)strtoull(str + i, endptr, base) * sign;
}

long double strtold(const char *str, char **endptr) {
    return (long double)strtod(str, endptr);
}

long atol(const char *str) {
    return strtol(str, 0, 10);
}

long long atoll(const char *str) {
    return strtoll(str, 0, 10);
}

long long llabs(long long x) {
    return x < 0 ? -x : x;
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *)) {
    const char *p = (const char *)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = compar(key, p + mid * size);
        if (c == 0) return (void *)(p + mid * size);
        if (c < 0) hi = mid;
        else       lo = mid + 1;
    }
    return 0;
}
