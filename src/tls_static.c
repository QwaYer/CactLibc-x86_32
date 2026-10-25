/*
 * Static TLS (`__thread`): the runtime half.
 *
 * ld.so fills `cact_tls_layout` from the loaded objects' PT_TLS segments before
 * the program's _start runs; this file turns that layout into one block per
 * thread and installs it as the thread's %gs base.  The layout always keeps
 * CACT_TLS_RESERVE bytes of spare room at the bottom of the block, so an object
 * whose own `__thread` only turns up later — a dlopen()'d plugin — can still be
 * given a place; cact_tls_module_add() is that half of the story.
 *
 * Not to be confused with src/tls.c, which is the TLS 1.3 client.
 */

#include "tls_layout.h"
#include "sys/ldt.h"
#include "stdlib.h"
#include "string.h"

cact_tls_layout_t cact_tls_layout;

/* Every block handed out, so a module added later can be copied into the blocks
 * that already exist: a thread that was running before the dlopen has to see the
 * new object's __thread image too.  A block lives as long as its thread and is
 * never freed, so an entry left behind by an exited thread is harmless.  The
 * block's `total` is kept with it: see cact_tls_module_add(). */
#define CACT_TLS_MAX_BLOCKS 128
struct tls_block { void *tp; uint32_t total; };
static struct tls_block tls_blocks[CACT_TLS_MAX_BLOCKS];
static uint32_t tls_nblocks;

static void tls_block_register(void *tp) {
    if (tls_nblocks < CACT_TLS_MAX_BLOCKS) {
        tls_blocks[tls_nblocks].tp    = tp;
        tls_blocks[tls_nblocks].total = cact_tls_layout.total;
        tls_nblocks++;
    }
}

/* Exposed for the host test: the plan is the very inline one ld.so uses, so
 * there is one body to get right rather than two that can drift. */
uint32_t cact_tls_plan(cact_tls_layout_t *lay, uint32_t nmods) {
    return cact_tls_plan_inline(lay, nmods);
}

/* Build a block: each module's initialised image, zeroed .tbss, and a TCB whose
 * first word is its own address ("%gs:0" is the thread pointer).  Returns the
 * thread pointer (the TCB, i.e. the block's end). */
void *cact_tls_fill(void *block, const cact_tls_layout_t *lay) {
    unsigned char *base = (unsigned char *)block;
    unsigned char *tcb;
    uint32_t i;

    memset(base, 0, lay->total);
    for (i = 0; i < lay->count; i++) {
        const cact_tls_module_t *m = &lay->mods[i];
        if (m->filesz)
            memcpy(base + m->off, (const void *)m->vaddr, m->filesz);
    }

    tcb = base + lay->total;
    *(unsigned long *)tcb = (unsigned long)tcb;
    return tcb;
}

/* A 16-byte aligned block (the alignment the plan assumes for the TCB).  It
 * lives as long as its thread, so it is never freed. */
static void *cact_tls_alloc(void) {
    unsigned char *p = (unsigned char *)
        malloc((size_t)cact_tls_layout.total + CACT_TLS_TCB_SIZE + 16);
    if (!p) return 0;
    return (unsigned char *)(((unsigned long)p + 15) & ~(unsigned long)15);
}

void __cact_tls_start(void) {
    struct user_desc ud;
    void *block, *tcb;

    if (cact_tls_layout.total == 0) return;      /* no static TLS in this program */
    block = cact_tls_alloc();
    if (!block) return;                          /* no memory: %gs stays unset */
    tcb = cact_tls_fill(block, &cact_tls_layout);
    tls_block_register(tcb);

    memset(&ud, 0, sizeof ud);
    ud.entry_number = (unsigned)-1;              /* "pick one" — no LDT here */
    ud.base_addr    = (unsigned)(uintptr_t)tcb;
    set_thread_area(&ud);
}

/* A new thread's block, built the same way; the caller keeps it alive.  Returns
 * the thread pointer to hand to CACT_PROCCTL_THREAD_CREATE, or 0 for an empty
 * layout (which the kernel reads as "leave %gs alone"); a layout ld.so built
 * always carries the reserve, so this only ever returns 0 before one is
 * published at all. */
void *cact_tls_block_new(void) {
    void *block, *tcb;
    if (cact_tls_layout.total == 0) return 0;
    block = cact_tls_alloc();
    if (!block) return 0;
    tcb = cact_tls_fill(block, &cact_tls_layout);
    tls_block_register(tcb);
    return tcb;
}

int cact_tls_module_add(unsigned long vaddr, uint32_t filesz, uint32_t memsz,
                        uint32_t align) {
    cact_tls_module_t *m;
    uint32_t i;
    int off;

    if (cact_tls_layout.count >= CACT_TLS_MAX_MODULES) return -1;
    off = cact_tls_take(&cact_tls_layout, memsz, align);
    if (off < 0) return -1;

    m = &cact_tls_layout.mods[cact_tls_layout.count++];
    m->id     = cact_tls_layout.next_id++;
    m->off    = (uint32_t)off;
    m->vaddr  = vaddr;
    m->filesz = filesz;
    m->memsz  = memsz;
    m->align  = align;

    /* The image has to reach the threads that already exist as well; threads
     * created from here on copy it out of the layout themselves.  A block laid
     * out under a different `total` cannot host this module at all — the offset
     * would not mean the same thing there — so those are left alone (in a real
     * process the layout is fixed before the first block exists, so this only
     * ever skips nothing). */
    for (i = 0; i < tls_nblocks; i++) {
        unsigned char *slot;
        if (tls_blocks[i].total != cact_tls_layout.total) continue;
        slot = (unsigned char *)tls_blocks[i].tp
             - cact_tls_layout.total + (uint32_t)off;
        if (filesz) memcpy(slot, (const void *)vaddr, filesz);
        if (memsz > filesz) memset(slot + filesz, 0, memsz - filesz);
    }
    return off;
}

void cact_tls_module_drop(uint32_t off) {
    uint32_t i, j;

    for (i = 0; i < cact_tls_layout.count; i++) {
        if (cact_tls_layout.mods[i].off != off || !cact_tls_layout.mods[i].memsz)
            continue;
        /* Shift the tail down and forget the module: it must stop copying out
         * of an image the loader is about to unmap, and its table slot has to
         * come back (nothing indexes mods[] by position once the plan has been
         * made — the layout is a set, not a list).  The block offset itself is
         * not reclaimed; glibc does not reclaim its static TLS surplus on
         * dlclose() either. */
        for (j = i + 1; j < cact_tls_layout.count; j++)
            cact_tls_layout.mods[j - 1] = cact_tls_layout.mods[j];
        cact_tls_layout.count--;
        cact_tls_layout.mods[cact_tls_layout.count].filesz = 0;
        cact_tls_layout.mods[cact_tls_layout.count].memsz  = 0;
        return;
    }
}

/* i386's default TLS model reaches a shared object's `__thread` through this
 * function: the compiler passes a pointer to a two-word { ti_module, ti_offset }
 * pair that the linker put in .got, and the loaders filled in the defining
 * object's TLS module id and the variable's offset inside that module.  Every
 * module — the program's, a library's, and one that arrived with dlopen() — has
 * a slice of the *same* per-thread static block, so the address is just that
 * slice's offset added to the thread pointer (%gs, whose first word holds the
 * TCB's own address).  There is no per-module DTV to allocate, which is the
 * whole reason this is small. */
void *___tls_get_addr(const void *ti_arg) {
    const uint32_t *ti = (const uint32_t *)ti_arg;
    unsigned int tp32;

    if (!ti || cact_tls_layout.total == 0) return 0;
    /* A 32-bit register: the TCB address fits in 32 bits on the target, and
     * `movl %gs:0` also assembles in the host build (where this file is
     * compiled too, but never called). */
    __asm__ volatile ("movl %%gs:0, %0" : "=r"(tp32));
    if (!tp32) return 0;                        /* no block installed */
    return cact_tls_addr(&cact_tls_layout, ti[0], ti[1], (unsigned long)tp32);
}
