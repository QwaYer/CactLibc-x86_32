#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <stdint.h>
#include <time.h>

/*
 * Minimal POSIX threads for CactOS.
 *
 * The Cact kernel has no threads/clone/futex, so every process is
 * single-threaded.  The subset below (mutexes, condition variables, once,
 * TLS keys) satisfies the pthread usage of single-threaded consumers such as
 * libwayland.  pthread_create() is provided but fails with ENOSYS.
 */

typedef uint32_t pthread_t;

typedef struct {
    volatile int state;   /* 0 = unlocked, 1 = locked */
} pthread_mutex_t;

typedef struct {
    volatile int seq;     /* unused in single-threaded mode */
} pthread_cond_t;

typedef struct {
    volatile int done;
} pthread_once_t;

typedef uint32_t pthread_key_t;

typedef struct { uint32_t __x; } pthread_attr_t;
typedef struct { uint32_t __x; } pthread_mutexattr_t;
typedef struct { uint32_t __x; } pthread_condattr_t;

#define PTHREAD_MUTEX_INITIALIZER  { 0 }
#define PTHREAD_COND_INITIALIZER   { 0 }
#define PTHREAD_ONCE_INIT          { 0 }

#define PTHREAD_MUTEX_NORMAL    0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT   PTHREAD_MUTEX_NORMAL

#define PTHREAD_KEYS_MAX 64
#define PTHREAD_DESTRUCTOR_ITERATIONS 4

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
void pthread_exit(void *retval);
int pthread_join(pthread_t thread, void **retval);

#endif /* _PTHREAD_H */
