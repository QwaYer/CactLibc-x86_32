#ifndef STRING_H
#define STRING_H

#include <stddef.h>

int strcmp(const char* s1, const char* s2);
int compare_string(const char* s1, const char* s2);
int strlen(const char* s);
char* strcat(char* dest, const char* src);
char* strcpy(char* dest, const char* src);
char* copy_string(char* dest, const char* src);

void* memory_set(void* dest, int val, int len);
void* memory_copy(void* dest, const void* src, int len);
int memory_compare(const void* s1, const void* s2, int n);

void* memset(void* dest, int val, unsigned int len);
void* memcpy(void* dest, const void* src, unsigned int len);
void* memmove(void* dest, const void* src, unsigned int len);
int   memcmp(const void* s1, const void* s2, unsigned int n);
void* memchr(const void* s, int c, size_t n);
void* memmem(const void* haystack, size_t haystacklen,
             const void* needle, size_t needlelen);

int  strncmp(const char* a, const char* b, unsigned int n);
void strncpy(char* dst, const char* src, int n);
char* strncat(char* dest, const char* src, size_t n);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strchrnul(const char* s, int c);
size_t strspn(const char* s, const char* accept);
size_t strcspn(const char* s, const char* reject);
char* strstr(const char* haystack, const char* needle);
char* strcasestr(const char* haystack, const char* needle);
char* strpbrk(const char* s, const char* accept);
char* strtok(char* str, const char* delim);
char* strtok_r(char* str, const char* delim, char** saveptr);
char* strsep(char** stringp, const char* delim);
int strcasecmp(const char* s1, const char* s2);
int strncasecmp(const char* s1, const char* s2, size_t n);
size_t strnlen(const char* s, size_t maxlen);
int strcoll(const char* s1, const char* s2);
size_t strxfrm(char* dest, const char* src, size_t n);
char* strdup(const char* s);
char* strndup(const char* s, size_t n);
char* stpcpy(char* dest, const char* src);

int  buf_append(char* buf, int pos, int max, const char* s);
int  buf_append_int(char* buf, int pos, int max, int n);

#endif
