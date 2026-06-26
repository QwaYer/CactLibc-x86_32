#include "string.h"

int compare_string(const char* s1, const char* s2) {
    int i;
    for (i = 0; s1[i] == s2[i]; i++) {
        if (s1[i] == '\0') return 0;
    }
    return s1[i] - s2[i];
}

int strcmp(const char* s1, const char* s2) {
    return compare_string(s1, s2);
}

int strlen(const char* s) {
    int len = 0;
    while (s[len] != '\0') len++;
    return len;
}

char* strcat(char* dest, const char* src) {
    char* ptr = dest + strlen(dest);
    while (*src != '\0') *ptr++ = *src++;
    *ptr = '\0';
    return dest;
}

char* strcpy(char* dest, const char* src) {
    char* ptr = dest;
    while (*src != '\0') *ptr++ = *src++;
    *ptr = '\0';
    return dest;
}

char* copy_string(char* dest, const char* src) {
    return strcpy(dest, src);
}

void itoa(int n, char str[]) {
    int i, sign;
    if ((sign = n) < 0) n = -n;
    i = 0;
    do {
        str[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);
    if (sign < 0) str[i++] = '-';
    str[i] = '\0';

    for (int j = 0, k = i - 1; j < k; j++, k--) {
        char temp = str[j];
        str[j] = str[k];
        str[k] = temp;
    }
}

int atoi(const char* str) {
    int res = 0, sign = 1, i = 0;
    if (str[0] == '-') { sign = -1; i++; }
    for (; str[i] != '\0'; ++i) {
        if (str[i] < '0' || str[i] > '9') break;
        res = res * 10 + str[i] - '0';
    }
    return sign * res;
}

void hex_to_ascii(unsigned int n, char str[]) {
    str[0] = '0';
    str[1] = 'x';
    int i;
    for (i = 7; i >= 0; i--) {
        unsigned int nibble = (n >> (i * 4)) & 0x0F;
        if (nibble < 10)
            str[9 - i] = nibble + '0';
        else
            str[9 - i] = nibble - 10 + 'A';
    }
    str[10] = '\0';
}

void* memory_set(void* dest, int val, int len) {
    unsigned char* ptr = (unsigned char*)dest;
    while (len-- > 0) *ptr++ = (unsigned char)val;
    return dest;
}

void* memory_copy(void* dest, const void* src, int len) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (len-- > 0) *d++ = *s++;
    return dest;
}

int memory_compare(const void* s1, const void* s2, int n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    for (int i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

void* memset(void* dest, int val, unsigned int len) {
    unsigned char* ptr = (unsigned char*)dest;
    while (len-- > 0) *ptr++ = (unsigned char)val;
    return dest;
}

void* memcpy(void* dest, const void* src, unsigned int len) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (len-- > 0) *d++ = *s++;
    return dest;
}

int memcmp(const void* s1, const void* s2, unsigned int n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    for (unsigned int i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
}

int strncmp(const char* a, const char* b, unsigned int n) {
    while (n > 0 && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return *a - *b;
}

void strncpy(char* dst, const char* src, int n) {
    int i = 0;
    while (src[i] && i < n - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

int buf_append(char* buf, int pos, int max, const char* s) {
    while (*s && pos < max - 1) buf[pos++] = *s++;
    buf[pos] = '\0';
    return pos;
}

int buf_append_int(char* buf, int pos, int max, int n) {
    char tmp[32];
    itoa(n, tmp);
    return buf_append(buf, pos, max, tmp);
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == '\0') ? (char*)s : 0;
}

char* strrchr(const char* s, int c) {
    const char* last = 0;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return (char*)last;
}

size_t strspn(const char* s, const char* accept) {
    size_t n = 0;
    while (*s) {
        const char* a = accept;
        int found = 0;
        while (*a) { if (*s == *a) { found = 1; break; } a++; }
        if (!found) break;
        n++; s++;
    }
    return n;
}

size_t strcspn(const char* s, const char* reject) {
    size_t n = 0;
    while (*s) {
        const char* r = reject;
        int found = 0;
        while (*r) { if (*s == *r) { found = 1; break; } r++; }
        if (found) break;
        n++; s++;
    }
    return n;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    while (*haystack) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
        haystack++;
    }
    return 0;
}

char* strpbrk(const char* s, const char* accept) {
    while (*s) {
        const char* a = accept;
        while (*a) { if (*s == *a) return (char*)s; a++; }
        s++;
    }
    return 0;
}

void* memmove(void* dest, const void* src, unsigned int len) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        for (unsigned int i = 0; i < len; i++) d[i] = s[i];
    } else if (d > s) {
        for (unsigned int i = len; i > 0; i--) d[i-1] = s[i-1];
    }
    return dest;
}