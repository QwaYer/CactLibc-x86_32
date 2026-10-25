/*
 * pthread_atfork(): the handler registry fork() (src/unistd.c) consults.
 *
 * Kept in its own translation unit because it depends on nothing else in the
 * libc — the registry, the three hooks fork() calls, and the registration call.
 * POSIX's ordering rule is the whole of the behaviour: the prepare handlers run
 * newest-registration-first before the fork, and the parent/child handlers run
 * in registration order on their own side of it.
 */

#include "pthread.h"
#include "errno.h"

#define ATFORK_MAX 32

static struct {
    void (*prepare)(void);
    void (*parent)(void);
    void (*child)(void);
} g_atfork[ATFORK_MAX];
static int g_atfork_n;

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void)) {
    if (g_atfork_n >= ATFORK_MAX) return ENOMEM;
    g_atfork[g_atfork_n].prepare = prepare;
    g_atfork[g_atfork_n].parent  = parent;
    g_atfork[g_atfork_n].child   = child;
    g_atfork_n++;
    return 0;
}

void __cact_atfork_prepare(void) {
    for (int i = g_atfork_n - 1; i >= 0; i--)
        if (g_atfork[i].prepare) g_atfork[i].prepare();
}

void __cact_atfork_parent(void) {
    for (int i = 0; i < g_atfork_n; i++)
        if (g_atfork[i].parent) g_atfork[i].parent();
}

void __cact_atfork_child(void) {
    for (int i = 0; i < g_atfork_n; i++)
        if (g_atfork[i].child) g_atfork[i].child();
}
