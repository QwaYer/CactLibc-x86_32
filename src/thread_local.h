/* Internal per-thread storage for CactLibc.  Not a public header.
 *
 * The Cact kernel has no TLS facility at all (no set_thread_area/LDT, no
 * per-task %gs/%fs base, no free GDT slot), so a compiler `__thread` is not
 * available yet.  What a thread-local value *can* use is the one thing the
 * libc fully controls: the thread's stack.  Every thread stack is allocated by
 * pthread_create, so its address range identifies the running thread — and
 * reading %esp needs no syscall, unlike GET_TID.
 *
 * The leader has no descriptor in the registry; a stack pointer that matches no
 * registered range gets the static leader block.  That is safe because worker
 * stacks come from the mmap arena and the leader's stack is the kernel-provided
 * one above it, so the ranges cannot overlap. */

#ifndef CACT_THREAD_LOCAL_H
#define CACT_THREAD_LOCAL_H

#include <stddef.h>

/* Per-thread block.  errno and dlerror() live here. */
struct cact_tls {
    int  errno_val;
    char dlerr[128];
    int  dlerr_set;     /* 1 = the text above is a pending error */
};

/* This thread's block, by stack range.  Never NULL. */
struct cact_tls *cact_tlocal_self(void);

/* The per-thread errno slot behind the errno macro in <errno.h>. */
int *__errno_location(void);

/* Register/unregister a worker stack.  Registration happens before the kernel
 * starts the thread, unregistration when it exits (or when it is joined), so a
 * live range is never left pointing at a freed descriptor.  Returns a slot
 * index, or -1 when the table is full (errno then degrades to the leader's
 * block rather than failing the thread creation). */
int  cact_tlocal_register(void *stack_base, size_t stack_size, struct cact_tls *blk);
void cact_tlocal_unregister(struct cact_tls *blk);

#endif /* CACT_THREAD_LOCAL_H */
