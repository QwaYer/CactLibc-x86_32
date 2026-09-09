#include "pthread.h"
#include "unistd.h"
#include "errno.h"
#include "time.h"

/*
 * Single-threaded pthread shim: the Cact kernel has no threads/clone/futex,
 * so locking primitives are trivially satisfied and pthread_create() cannot
 * work.  Kept enough for single-threaded consumers like libwayland.
 */

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    (void)attr;
    if (!mutex) return EINVAL;
    mutex->state = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    mutex->state = 1;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    if (mutex->state) return EBUSY;
    mutex->state = 1;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    mutex->state = 0;
    return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    (void)attr;
    if (!cond) return EINVAL;
    cond->seq = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    /* Single thread: no peer can signal.  Unlock, yield a little so a
     * signal handler / other context can make progress, relock, and return
     * spuriously (POSIX allows spurious wakeups). */
    (void)cond;
    if (mutex) pthread_mutex_unlock(mutex);
    usleep(1000);
    if (mutex) pthread_mutex_lock(mutex);
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime) {
    (void)cond;
    struct timespec now;
    if (mutex) pthread_mutex_unlock(mutex);
    clock_gettime(CLOCK_REALTIME, &now);
    if (abstime &&
        (abstime->tv_sec > now.tv_sec ||
         (abstime->tv_sec == now.tv_sec && abstime->tv_nsec > now.tv_nsec))) {
        long ms = (abstime->tv_sec - now.tv_sec) * 1000L +
                  (abstime->tv_nsec - now.tv_nsec) / 1000000L;
        if (ms > 0) usleep((unsigned int)ms);
    }
    if (mutex) pthread_mutex_lock(mutex);
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine) return EINVAL;
    if (!once_control->done) {
        once_control->done = 1;
        init_routine();
    }
    return 0;
}

static void (*key_destructors[PTHREAD_KEYS_MAX])(void *) = { 0 };
static void *key_values[PTHREAD_KEYS_MAX] = { 0 };
static unsigned char key_used[PTHREAD_KEYS_MAX] = { 0 };

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    if (!key) return EINVAL;
    for (int i = 0; i < PTHREAD_KEYS_MAX; i++) {
        if (!key_used[i]) {
            key_used[i] = 1;
            key_destructors[i] = destructor;
            key_values[i] = 0;
            *key = (pthread_key_t)i;
            return 0;
        }
    }
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX || !key_used[key]) return EINVAL;
    key_used[key] = 0;
    key_destructors[key] = 0;
    key_values[key] = 0;
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX || !key_used[key]) return 0;
    return key_values[key];
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key >= PTHREAD_KEYS_MAX || !key_used[key]) return EINVAL;
    key_values[key] = (void *)value;
    return 0;
}

pthread_t pthread_self(void) {
    return 1;
}

int pthread_equal(pthread_t t1, pthread_t t2) {
    return t1 == t2;
}

int pthread_attr_init(pthread_attr_t *attr) {
    if (!attr) return EINVAL;
    attr->__x = 0;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
    (void)attr;
    return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    (void)thread;
    (void)attr;
    (void)start_routine;
    (void)arg;
    return ENOSYS;
}

void pthread_exit(void *retval) {
    (void)retval;
    _exit(0);
}

int pthread_join(pthread_t thread, void **retval) {
    (void)thread;
    (void)retval;
    return EINVAL;
}
