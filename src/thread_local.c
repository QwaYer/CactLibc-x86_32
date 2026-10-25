#include "thread_local.h"
#include "sys/ldt.h"
#include "ioctl_abi.h"
#include "nodeio.h"
#include "errno.h"
#include <stdint.h>

/* Sized for the threads a CactOS process plausibly runs; a full table degrades
 * to the leader's block instead of failing thread creation. */
#define CACT_TLS_MAX_THREADS 128

struct tls_slot {
    uintptr_t        lo;    /* [lo, hi) stack range; 0 when the slot is free */
    uintptr_t        hi;
    struct cact_tls *blk;
};

static struct tls_slot slots[CACT_TLS_MAX_THREADS];

/* A hint only: it is what makes repeated errno access by one thread a single
 * comparison.  A stale or torn value just costs a rescan. */
static volatile int tls_hint = -1;

/* The process leader: a plain static, used whenever the current stack pointer
 * matches no registered worker stack. */
static struct cact_tls leader_blk;

static uintptr_t current_sp(void) {
#if defined(__i386__)
    uintptr_t sp;
    __asm__ volatile ("movl %%esp, %0" : "=r"(sp));
    return sp;
#else
    /* Host builds (the errno unit check) only: any address inside the current
     * frame identifies the stack just as well as the stack pointer. */
    return (uintptr_t)__builtin_frame_address(0);
#endif
}

int cact_tlocal_register(void *stack_base, size_t stack_size, struct cact_tls *blk) {
    if (!blk || !stack_base || stack_size == 0) return -1;

    for (int i = 0; i < CACT_TLS_MAX_THREADS; i++) {
        if (__atomic_load_n(&slots[i].blk, __ATOMIC_ACQUIRE) == 0) {
            slots[i].lo = (uintptr_t)stack_base;
            slots[i].hi = (uintptr_t)stack_base + stack_size;
            __atomic_store_n(&slots[i].blk, blk, __ATOMIC_RELEASE);
            return i;
        }
    }
    return -1;
}

void cact_tlocal_unregister(struct cact_tls *blk) {
    if (!blk) return;
    for (int i = 0; i < CACT_TLS_MAX_THREADS; i++) {
        if (__atomic_load_n(&slots[i].blk, __ATOMIC_ACQUIRE) == blk) {
            __atomic_store_n(&slots[i].blk, (struct cact_tls *)0, __ATOMIC_RELEASE);
            slots[i].lo = 0;
            slots[i].hi = 0;
            if (tls_hint == i) tls_hint = -1;
            return;
        }
    }
}

struct cact_tls *cact_tlocal_self(void) {
    uintptr_t sp = current_sp();
    int h = tls_hint;

    if (h >= 0 && h < CACT_TLS_MAX_THREADS) {
        struct cact_tls *b = __atomic_load_n(&slots[h].blk, __ATOMIC_ACQUIRE);
        if (b && sp >= slots[h].lo && sp < slots[h].hi) return b;
    }
    for (int i = 0; i < CACT_TLS_MAX_THREADS; i++) {
        struct cact_tls *b = __atomic_load_n(&slots[i].blk, __ATOMIC_ACQUIRE);
        if (b && sp >= slots[i].lo && sp < slots[i].hi) {
            tls_hint = i;
            return b;
        }
    }
    return &leader_blk;
}

int *__errno_location(void) {
    return &cact_tlocal_self()->errno_val;
}

/* The kernel's per-task segment base — the facility the registry above stands
 * in for.  CactOS has no LDT, so `base_addr` is installed as the task's %gs base
 * outright and the rest of `struct user_desc` is ignored (see <sys/ldt.h>). */
int set_thread_area(struct user_desc *u_info) {
    if (!u_info) { errno = EFAULT; return -1; }
    uint32_t base = (uint32_t)u_info->base_addr;
    return nio_map(nio_ctl(CACT_PROCCTL_SET_TLS, &base));
}
