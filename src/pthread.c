#include "pthread.h"
#include "nodeio.h"
#include "ioctl_abi.h"
#include "unistd.h"
#include "errno.h"
#include "time.h"
#include "mman.h"
#include "stdlib.h"
#include "string.h"
#include "fcntl.h"

#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif

/*
 * POSIX threads on the Cact kernel's user threads.
 *
 * pthread_create() builds the new thread's initial stack and asks the kernel
 * (CACT_PROCCTL_THREAD_CREATE) to start `start_routine` on it.  The kernel
 * points a per-thread "join word" at the descriptor; it zeroes that word and
 * futex-wakes it when the thread exits, which is what makes pthread_join() safe
 * to tear the stack down (the exiting thread never touches it again).
 *
 * Mutexes and condition variables are the usual futex algorithms over
 * CACT_PROCCTL_FUTEX: a lock word in user memory and a kernel wait/wake keyed
 * by (address space, address).
 */

/* ── futex wrappers ─────────────────────────────────────────────────────── */

/* One long-lived /proc/self/ctl fd for every thread.  Opening it per futex
 * operation would race on the shared fd table and churn descriptors. */
static int ctl_fd = -1;

static int ctl(unsigned long cmd, void *arg) {
    int fd = ctl_fd;
    if (fd < 0) {
        fd = nio_open("/proc/self/ctl", O_RDWR);
        if (fd < 0) return -1;
        ctl_fd = fd;
    }
    return nio_ioctl(fd, cmd, arg);
}

static int futex_wait(volatile int *uaddr, int expected, int timeout_ms) {
    cact_futex_arg_t a;
    a.uaddr      = (uint32_t)(uintptr_t)uaddr;
    a.op         = CACT_FUTEX_WAIT;
    a.val        = expected;
    a.timeout_ms = timeout_ms;
    return ctl(CACT_PROCCTL_FUTEX, &a);   /* 0, -EAGAIN, -ETIMEDOUT, -EINTR */
}

static int futex_wake(volatile int *uaddr, int count) {
    cact_futex_arg_t a;
    a.uaddr      = (uint32_t)(uintptr_t)uaddr;
    a.op         = CACT_FUTEX_WAKE;
    a.val        = count;
    a.timeout_ms = 0;
    return ctl(CACT_PROCCTL_FUTEX, &a);
}

/* The calling thread's tid (kernel task id), via the cached ctl fd. */
static uint32_t self_tid(void) {
    int t = ctl(CACT_PROCCTL_GET_TID, 0);
    return (t > 0) ? (uint32_t)t : (uint32_t)gettid();
}

/* ── thread descriptors ─────────────────────────────────────────────────── */

typedef struct cact_thread {
    struct cact_thread *self;
    uint32_t            id;        /* = pthread_t (kernel tid) */
    uint32_t            clr;       /* join word: kernel zeroes it on exit */
    int                 detached;
    int                 has_result;
    void               *retval;
    void               *(*start)(void *);
    void               *arg;
    void               *stack;
    size_t              stack_size;
    void               *keys[PTHREAD_KEYS_MAX];
    struct cact_thread *next;
} cact_thread_t;

/* Registry + key table, guarded by g_lock. */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static cact_thread_t  *g_threads = 0;
static void          (*g_key_destructors[PTHREAD_KEYS_MAX])(void *) = { 0 };
static unsigned char   g_key_used[PTHREAD_KEYS_MAX] = { 0 };

/* The running thread's descriptor, created on first use (the leader has no
 * descriptor until then). */
static cact_thread_t *thread_self_desc(void) {
    uint32_t me = self_tid();

    pthread_mutex_lock(&g_lock);
    for (cact_thread_t *t = g_threads; t; t = t->next) {
        if (t->id == me) {
            pthread_mutex_unlock(&g_lock);
            return t;
        }
    }
    cact_thread_t *td = (cact_thread_t *)calloc(1, sizeof(*td));
    if (td) {
        td->self  = td;
        td->id    = me;
        td->clr   = me;
        td->next  = g_threads;
        g_threads = td;
    }
    pthread_mutex_unlock(&g_lock);
    return td;
}

static cact_thread_t *find_by_id_locked(uint32_t id) {
    for (cact_thread_t *t = g_threads; t; t = t->next)
        if (t->id == id) return t;
    return 0;
}

/* ── mutex ──────────────────────────────────────────────────────────────── */

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    if (!mutex) return EINVAL;
    (void)attr;
    mutex->state = 0;
    mutex->type  = PTHREAD_MUTEX_NORMAL;
    mutex->count = 0;
    mutex->owner = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;
    if (__atomic_load_n((int *)&mutex->state, __ATOMIC_ACQUIRE) != 0) return EBUSY;
    return 0;
}

static int mutex_lock_slow(pthread_mutex_t *m) {
    for (;;) {
        /* Acquire *with* the contended marker (0 -> 2) so a later unlock will
         * wake the next waiter.  Acquiring as plain 1 here loses wake-ups:
         * a woken thread would take the lock as 1, then unlock seeing old==1
         * and never wake the remaining waiters. */
        int c = 0;
        if (__atomic_compare_exchange_n((int *)&m->state, &c, 2, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return 0;
        /* c now holds the observed value (1 or 2).  If it is 1, promote it to
         * 2 so the holder knows there is a waiter to wake. */
        if (c == 1) {
            __atomic_compare_exchange_n((int *)&m->state, &c, 2, 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED);
        }
        futex_wait(&m->state, 2, -1);   /* EAGAIN just means "try again" */
    }
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;

    if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        uint32_t me = self_tid();
        if (mutex->owner == (int)me) {
            mutex->count++;
            return 0;
        }
    }

    int c = __atomic_load_n((int *)&mutex->state, __ATOMIC_RELAXED);
    if (c == 0 &&
        __atomic_compare_exchange_n((int *)&mutex->state, &c, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
            mutex->owner = (int)self_tid();
            mutex->count = 1;
        }
        return 0;
    }
    if (mutex->type == PTHREAD_MUTEX_ERRORCHECK && mutex->owner == (int)self_tid())
        return EDEADLK;

    int r = mutex_lock_slow(mutex);
    if (r == 0 && mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        mutex->owner = (int)self_tid();
        mutex->count = 1;
    }
    return r;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;

    if (mutex->type == PTHREAD_MUTEX_RECURSIVE &&
        mutex->owner == (int)self_tid()) {
        mutex->count++;
        return 0;
    }

    int c = 0;
    if (__atomic_compare_exchange_n((int *)&mutex->state, &c, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
            mutex->owner = (int)self_tid();
            mutex->count = 1;
        }
        return 0;
    }
    return EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (!mutex) return EINVAL;

    if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        if (mutex->owner != (int)self_tid()) return EPERM;
        if (mutex->count > 1) { mutex->count--; return 0; }
        mutex->count = 0;
        mutex->owner = 0;
    }

    int old = __atomic_exchange_n((int *)&mutex->state, 0, __ATOMIC_RELEASE);
    if (old == 2) futex_wake(&mutex->state, 1);
    return 0;
}

/* ── condition variable ─────────────────────────────────────────────────── */

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    if (!cond) return EINVAL;
    (void)attr;
    cond->seq = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    if (!cond || !mutex) return EINVAL;
    int seq = __atomic_load_n((int *)&cond->seq, __ATOMIC_RELAXED);
    pthread_mutex_unlock(mutex);
    /* If a signal lands between the load and the wait, seq changed and the
     * wait returns -EAGAIN immediately. */
    int r = futex_wait(&cond->seq, seq, -1);
    pthread_mutex_lock(mutex);
    if (r == -EINTR) return EINTR;
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime) {
    if (!cond || !mutex || !abstime) return EINVAL;

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    long ms = (abstime->tv_sec - now.tv_sec) * 1000L +
              (abstime->tv_nsec - now.tv_nsec) / 1000000L;
    int timeout = (ms <= 0) ? 0 : (int)ms;

    int seq = __atomic_load_n((int *)&cond->seq, __ATOMIC_RELAXED);
    pthread_mutex_unlock(mutex);
    int r = futex_wait(&cond->seq, seq, timeout);
    pthread_mutex_lock(mutex);

    if (r == -ETIMEDOUT || (timeout == 0 && r == -EAGAIN))
        return ETIMEDOUT;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
    if (!cond) return EINVAL;
    __atomic_fetch_add((int *)&cond->seq, 1, __ATOMIC_RELEASE);
    futex_wake(&cond->seq, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    if (!cond) return EINVAL;
    __atomic_fetch_add((int *)&cond->seq, 1, __ATOMIC_RELEASE);
    futex_wake(&cond->seq, 0x7fffffff);
    return 0;
}

/* ── once ───────────────────────────────────────────────────────────────── */

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine) return EINVAL;

    if (__atomic_load_n((int *)&once_control->done, __ATOMIC_ACQUIRE) == 2)
        return 0;

    int expected = 0;
    if (__atomic_compare_exchange_n((int *)&once_control->done, &expected, 1, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        init_routine();
        __atomic_store_n((int *)&once_control->done, 2, __ATOMIC_RELEASE);
        futex_wake(&once_control->done, 0x7fffffff);
        return 0;
    }

    while (__atomic_load_n((int *)&once_control->done, __ATOMIC_ACQUIRE) != 2)
        futex_wait(&once_control->done, 1, -1);
    return 0;
}

/* ── thread-specific data ───────────────────────────────────────────────── */

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    if (!key) return EINVAL;

    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < PTHREAD_KEYS_MAX; i++) {
        if (!g_key_used[i]) {
            g_key_used[i] = 1;
            g_key_destructors[i] = destructor;
            *key = (pthread_key_t)i;
            pthread_mutex_unlock(&g_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX) return EINVAL;
    pthread_mutex_lock(&g_lock);
    if (!g_key_used[key]) { pthread_mutex_unlock(&g_lock); return EINVAL; }
    g_key_used[key] = 0;
    g_key_destructors[key] = 0;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX) return 0;
    cact_thread_t *td = thread_self_desc();
    if (!td) return 0;
    return td->keys[key];
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key >= PTHREAD_KEYS_MAX) return EINVAL;
    if (!g_key_used[key]) return EINVAL;
    cact_thread_t *td = thread_self_desc();
    if (!td) return ENOMEM;
    td->keys[key] = (void *)value;
    return 0;
}

/* ── thread identity ────────────────────────────────────────────────────── */

pthread_t pthread_self(void) {
    return (pthread_t)self_tid();
}

int pthread_equal(pthread_t t1, pthread_t t2) {
    return t1 == t2;
}

/* ── lifecycle ──────────────────────────────────────────────────────────── */

int pthread_attr_init(pthread_attr_t *attr) {
    if (!attr) return EINVAL;
    attr->__x = 0;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
    (void)attr;
    return 0;
}

#define DEFAULT_STACK_SIZE (1024u * 1024u)

/* Jumped to when a thread's start routine returns; EAX holds its return value
 * and the stack is unspecified.  Defined in thread_start.S. */
extern void __cact_thread_exit_stub(void);

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    if (!thread || !start_routine) return EINVAL;
    (void)attr;

    /* Seed the process pid from the leader before any worker exists. */
    __cact_pid_init();

    void *stack = mmap(0, DEFAULT_STACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) return EAGAIN;

    cact_thread_t *td = (cact_thread_t *)calloc(1, sizeof(*td));
    if (!td) { munmap(stack, DEFAULT_STACK_SIZE); return EAGAIN; }
    td->self       = td;
    td->start      = start_routine;
    td->arg        = arg;
    td->stack      = stack;
    td->stack_size = DEFAULT_STACK_SIZE;

    /* Initial frame: [return address = exit stub][arg].  The kernel enters
     * start_routine as if it had been called. */
    uintptr_t top = (uintptr_t)stack + DEFAULT_STACK_SIZE;
    top &= ~(uintptr_t)15;
    uintptr_t *sp = (uintptr_t *)top;
    *--sp = (uintptr_t)arg;
    *--sp = (uintptr_t)__cact_thread_exit_stub;

    cact_thread_create_arg_t a;
    a.entry           = (void *)start_routine;
    a.user_esp        = (uint32_t)(uintptr_t)sp;
    a.flags           = 0;
    a.tls             = 0;
    a.set_child_tid   = (uint32_t)(uintptr_t)&td->id;   /* kernel writes the tid */
    a.clear_child_tid = (uint32_t)(uintptr_t)&td->clr;  /* kernel zeroes on exit */

    /* Register before the kernel starts the thread, so the new thread can find
     * its own descriptor by tid as soon as it runs. */
    pthread_mutex_lock(&g_lock);
    td->next  = g_threads;
    g_threads = td;
    pthread_mutex_unlock(&g_lock);

    int tid = ctl(CACT_PROCCTL_THREAD_CREATE, &a);
    if (tid <= 0) {
        pthread_mutex_lock(&g_lock);
        cact_thread_t **pp = &g_threads;
        while (*pp && *pp != td) pp = &(*pp)->next;
        if (*pp) *pp = td->next;
        pthread_mutex_unlock(&g_lock);
        munmap(stack, DEFAULT_STACK_SIZE);
        free(td);
        return EAGAIN;
    }

    /* The kernel wrote `tid` into td->id and td->clr before the thread ran. */
    *thread = (pthread_t)td->id;
    return 0;
}

__attribute__((noreturn, visibility("hidden")))
void __cact_thread_exit_body(void *retval);

void pthread_exit(void *retval) {
    __cact_thread_exit_body(retval);
}

/*
 * The real thread-exit routine.  It has hidden visibility so the asm stub in
 * thread_start.S can call it with a plain PC-relative `call` (no PLT): a new
 * thread enters with %ebx unspecified, and a PLT entry would use it as the GOT
 * pointer.  A direct call lets this function establish its own GOT on entry.
 */
__attribute__((noreturn, visibility("hidden")))
void __cact_thread_exit_body(void *retval) {
    cact_thread_t *td = thread_self_desc();
    if (td) {
        td->retval     = retval;
        td->has_result = 1;

        /* Run key destructors (a few times, per POSIX). */
        for (int iter = 0; iter < PTHREAD_DESTRUCTOR_ITERATIONS; iter++) {
            int ran = 0;
            for (int k = 0; k < PTHREAD_KEYS_MAX; k++) {
                void (*dtor)(void *) = g_key_destructors[k];
                if (dtor && td->keys[k]) {
                    void *v = td->keys[k];
                    td->keys[k] = 0;
                    dtor(v);
                    ran = 1;
                }
            }
            if (!ran) break;
        }
    }

    /* The kernel zeroes &td->clr and futex-wakes anyone joined on it, then
     * never lets this thread run again. */
    uint32_t code = 0;
    ctl(CACT_PROCCTL_THREAD_EXIT, &code);

    /* Not reached; if it somehow is, end the process rather than spin. */
    _exit(0);
}

int pthread_join(pthread_t thread, void **retval) {
    if (thread == 0) return ESRCH;

    pthread_mutex_lock(&g_lock);
    cact_thread_t *td = find_by_id_locked((uint32_t)thread);
    pthread_mutex_unlock(&g_lock);
    if (!td) return ESRCH;

    /* Wait until the kernel clears the join word (thread really gone). */
    while (__atomic_load_n((int *)&td->clr, __ATOMIC_ACQUIRE) != 0) {
        int cur = (int)__atomic_load_n((int *)&td->clr, __ATOMIC_RELAXED);
        if (cur == 0) break;
        int r = futex_wait((volatile int *)&td->clr, cur, -1);
        if (r == -EINTR) return EINTR;   /* let a pending signal terminate */
    }

    if (retval) *retval = td->retval;

    pthread_mutex_lock(&g_lock);
    cact_thread_t **pp = &g_threads;
    while (*pp && *pp != td) pp = &(*pp)->next;
    if (*pp) *pp = td->next;
    pthread_mutex_unlock(&g_lock);

    void *stack = td->stack;
    size_t ssize = td->stack_size;
    free(td);
    if (stack) munmap(stack, ssize);
    return 0;
}

int pthread_detach(pthread_t thread) {
    pthread_mutex_lock(&g_lock);
    cact_thread_t *td = find_by_id_locked((uint32_t)thread);
    if (td) td->detached = 1;
    pthread_mutex_unlock(&g_lock);
    /* A detached thread's stack/descriptor are reclaimed when the process
     * exits; v1 does not free them separately. */
    return td ? 0 : ESRCH;
}
