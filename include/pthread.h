#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <stdint.h>
#include <time.h>

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

typedef struct { uint32_t __x; } pthread_attr_t;
typedef struct { uint32_t __x; } pthread_mutexattr_t;
typedef struct { uint32_t __x; } pthread_condattr_t;

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

#endif /* _PTHREAD_H */
