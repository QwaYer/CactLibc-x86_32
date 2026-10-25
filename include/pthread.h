#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <stdint.h>
#include <time.h>
#include "sched.h"           /* struct sched_param, SCHED_OTHER */

/*
 * POSIX threads for CactOS.
 *
 * The Cact kernel now provides user threads (CACT_PROCCTL_THREAD_CREATE),
 * futex-style wait/wake (CACT_PROCCTL_FUTEX) and a kernel-cleared join word
 * (clear_child_tid).  On top of those this header offers the subset libc
 * consumers need: mutexes, condition variables, once, per-thread keys, and
 * pthread_create/join/exit.
 */

typedef uint32_t pthread_t;

/* `state` is the futex word: 0 = unlocked, 1 = locked (no waiters),
 * 2 = locked with at least one waiter. */
typedef struct {
    volatile int state;
    int          type;
    int          count;   /* recursion depth (recursive mutexes) */
    int          owner;   /* owning tid (recursive/errorcheck) */
} pthread_mutex_t;

/* `seq` is the futex word: bumped by signal/broadcast, waited on by waiters. */
typedef struct {
    volatile int seq;
} pthread_cond_t;

typedef struct {
    volatile int done;    /* 0 = unstarted, 1 = running, 2 = done */
} pthread_once_t;

typedef uint32_t pthread_key_t;

typedef struct { uint32_t __x; } pthread_mutexattr_t;
typedef struct { uint32_t __x; } pthread_condattr_t;

/* The scheduling half of a thread attribute.  POSIX starts a new thread with
 * INHERIT_SCHED, so policy/param below only take effect once
 * pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED) is called — and
 * then only SCHED_OTHER (priority 0) can be honoured, because that is the one
 * class the MLFQ has; pthread_create() reports EINVAL for anything else rather
 * than silently starting the thread in the wrong class (see <sched.h>). */
typedef struct {
    int                policy;
    int                inheritsched;
    struct sched_param param;
} pthread_attr_t;

#define PTHREAD_MUTEX_INITIALIZER  { 0, 0, 0, 0 }
#define PTHREAD_COND_INITIALIZER   { 0 }
#define PTHREAD_ONCE_INIT          { 0 }

#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT    PTHREAD_MUTEX_NORMAL

#define PTHREAD_KEYS_MAX 64
#define PTHREAD_DESTRUCTOR_ITERATIONS 4

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

#define PTHREAD_INHERIT_SCHED   0
#define PTHREAD_EXPLICIT_SCHED  1

#define PTHREAD_SCOPE_SYSTEM    0   /* kernel threads: the only scope there is */
#define PTHREAD_SCOPE_PROCESS   1

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);

pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);
void pthread_exit(void *retval) __attribute__((noreturn));
int pthread_join(pthread_t thread, void **retval);
int pthread_detach(pthread_t thread);

/* The scheduling attributes (see the comment on pthread_attr_t). */
int pthread_attr_setinheritsched(pthread_attr_t *attr, int inheritsched);
int pthread_attr_getinheritsched(const pthread_attr_t *attr, int *inheritsched);
int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy);
int pthread_attr_getschedpolicy(const pthread_attr_t *attr, int *policy);
int pthread_attr_setschedparam(pthread_attr_t *attr, const struct sched_param *param);
int pthread_attr_getschedparam(const pthread_attr_t *attr, struct sched_param *param);
int pthread_attr_setscope(pthread_attr_t *attr, int scope);
int pthread_attr_getscope(const pthread_attr_t *attr, int *scope);

int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param *param);
int pthread_getschedparam(pthread_t thread, int *policy, struct sched_param *param);
int pthread_setschedprio(pthread_t thread, int prio);

/* fork() handlers.  Called in POSIX's order: every prepare in reverse
 * registration order, then every parent/child in order. */
int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void));

/* Internal: the hooks fork() (src/unistd.c) calls around the trap. */
void __cact_atfork_prepare(void);
void __cact_atfork_parent(void);
void __cact_atfork_child(void);

#endif /* _PTHREAD_H */
