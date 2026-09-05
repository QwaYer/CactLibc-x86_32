#include "libgen.h"
#include "string.h"

char *dirname(char *path) {
    if (!path || !*path) return ".";
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') path[--n] = '\0';
    char *slash = 0;
    for (char *p = path; *p; p++)
        if (*p == '/') slash = p;
    if (!slash) return ".";
    if (slash == path) {
        slash[1] = '\0';
        return path;
    }
    *slash = '\0';
    return path;
}

char *basename(char *path) {
    if (!path || !*path) return ".";
    char *end = path + strlen(path);
    while (end > path && end[-1] == '/') end--;
    if (end == path) return "/";
    *end = '\0';
    char *base = end;
    while (base > path && base[-1] != '/') base--;
    return base;
}
