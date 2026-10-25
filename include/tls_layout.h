#ifndef CACT_TLS_LAYOUT_H
#define CACT_TLS_LAYOUT_H

#include <stdint.h>

/*
 * Static TLS layout, shared by ld.so and the libc.
 *
 * ld.so builds it from the loaded objects' PT_TLS segments and stores it in the
 * libc's `cact_tls_layout` before the program's _start runs; the libc builds
 * every thread's block from it (__cact_tls_start for the main thread,
 * pthread_create for the rest).
 *
 * i386 variant II: the block grows upward and the TCB (thread control block)
 * sits immediately after its end.  `%gs` names the TCB, `%gs:0` holds the TCB's
 * own address, and a variable at TLS offset v of a module whose block offset is
 * o is at `TCB + o + v - total` — which is what R_386_TLS_TPOFF resolves to.
 *
 * The bottom of the block is spare room (CACT_TLS_RESERVE bytes, the same for
 * every thread): the block is laid out once, before _start, but an object
 * dlopen()'d later may carry its own __thread, so ld.so keeps a slice free and
 * cact_tls_module_add() hands it out as those objects arrive (see
 * src/tls_static.c).  It also means a program with no __thread of its own still
 * gets a block, so %gs is usable the moment it loads one.
 *
 * Two TLS models can reach a shared object.  GNU ld reduces static TLS — what
 * -ftls-model=initial-exec emits for a library, and what a program's own code
 * uses (local-exec) — to R_386_TLS_TPOFF, so both resolve against this layout.
 * General-dynamic (a shared object's default) instead reaches the variable
 * through ___tls_get_addr() with the linker's { module id, offset } pair: the
 * loaders fill that pair from the same layout (each module's `id`, and the
 * symbol's offset), and cact_tls_addr() turns it into the address.  The module
 * is looked up by `id`, never by table position, because the table is compacted
 * when a dlclose() forgets a module.
 *
 * `vaddr` and the TCB's self pointer are written as `unsigned long` rather than
 * uintptr_t: this libc's <stdint.h> pins uintptr_t to 32 bits, and the host test
 * harness compiles one side of this struct for a 64-bit machine, so a builtin
 * type keeps the two sides agreeing on the width they each need.
 */

#define CACT_TLS_MAX_MODULES 8
/* The TCB: the self pointer the ABI's `%gs:0` read needs, plus one spare word. */
#define CACT_TLS_TCB_SIZE    (2 * sizeof(void *))
/* Spare room at the bottom of every thread's block, for the objects that arrive
 * after the layout was made.  It is a per-thread cost, so it is deliberately
 * modest; a module that does not fit is refused with "cannot allocate memory in
 * static TLS block", which is what glibc says in the same situation. */
#define CACT_TLS_RESERVE     1024

typedef struct {
    uint32_t      id;      /* TLS module id (stable, never reused) */
    uint32_t      off;     /* offset of this module's TLS in the block (assigned) */
    unsigned long vaddr;   /* where the module's initialised TLS image is mapped */
    uint32_t      filesz;  /* .tdata bytes to copy out of that image */
    uint32_t      memsz;   /* .tdata + .tbss bytes to reserve */
    uint32_t      align;   /* the module's PT_TLS p_align */
} cact_tls_module_t;

typedef struct {
    uint32_t          total;      /* the whole block; the TCB starts here */
    uint32_t          count;
    uint32_t          spare;      /* the room kept at the bottom */
    uint32_t          spare_used; /* how much of it has been handed out */
    uint32_t          next_id;    /* the next TLS module id to hand out */
    cact_tls_module_t mods[CACT_TLS_MAX_MODULES];
} cact_tls_layout_t;

/* Assign each module's `off` and the block's `total`.  `mods[0..nmods)` must
 * already carry vaddr/filesz/memsz/align, with mods[0] being the program itself.
 * Returns `total` — 0 when there is nothing to place.
 *
 * The program's module is placed at the TOP, so its block ends exactly at the
 * thread pointer.  That is not a style choice: gcc/clang with -fPIE emit
 * local-exec accesses (`mov %gs:0,reg` then a displacement) and the *linker*
 * resolves the displacement to `offset_of_variable - memsz_of_the_program's
 * TLSeGMENT`, which only lands on the variable if the program's block ends at
 * the thread pointer.  Anything else (a library's own __thread) would use
 * initial-exec/general-dynamic and pick its offset dynamically, which is why
 * those models can be laid out freely below. */
static inline uint32_t cact_tls_plan_inline(cact_tls_layout_t *lay, uint32_t nmods)
{
    uint32_t n = nmods < CACT_TLS_MAX_MODULES ? nmods : CACT_TLS_MAX_MODULES;
    uint32_t off = 0;
    int i;

    for (i = (int)n - 1; i >= 1; i--) {
        uint32_t a = lay->mods[i].align;
        if (a < 4 || (a & (a - 1))) a = 4;
        off = (off + a - 1) & ~(a - 1);
        lay->mods[i].off = off;
        off += lay->mods[i].memsz;
    }
    if (n > 0) {
        uint32_t a = lay->mods[0].align;
        if (a < 4 || (a & (a - 1))) a = 4;
        off = (off + a - 1) & ~(a - 1);
        lay->mods[0].off = off;
        off += lay->mods[0].memsz;
    }

    lay->count = n;
    lay->total = off;                            /* == the thread pointer */
    for (i = 0; i < (int)n; i++) lay->mods[i].id = (uint32_t)(i + 1);
    lay->next_id = (uint32_t)n + 1;
    return lay->total;
}

/* Set aside `bytes` of spare room below every planned module.  Every offset
 * moves up by the same rounded-up amount, so module 0 still ends at the thread
 * pointer — the invariant the linker's own displacements depend on.  The
 * rounding keeps each module's alignment (the largest PT_TLS p_align in
 * practice is much smaller).  Returns `total`. */
static inline uint32_t cact_tls_reserve(cact_tls_layout_t *lay, uint32_t bytes)
{
    uint32_t i;

    if (!bytes) return lay->total;
    bytes = (bytes + 63u) & ~63u;
    for (i = 0; i < lay->count; i++) lay->mods[i].off += bytes;
    lay->spare = bytes;
    lay->spare_used = 0;
    lay->total += bytes;
    return lay->total;
}

/* Lay the block out and set aside the spare room in one step: this is what
 * ld.so does, before any thread exists, and the two must not get out of order
 * (a reserve applied to an unplanned layout leaves every module at offset 0).
 * Returns `total`. */
static inline uint32_t cact_tls_plan_reserved(cact_tls_layout_t *lay, uint32_t nmods,
                                              uint32_t bytes)
{
    cact_tls_plan_inline(lay, nmods);
    return cact_tls_reserve(lay, bytes);
}

/* Hand `memsz` bytes (aligned to `align`) out of the spare room to a module that
 * arrived after the plan was made.  Returns its offset in the block, or -1 when
 * the room is used up. */
static inline int cact_tls_take(cact_tls_layout_t *lay, uint32_t memsz, uint32_t align)
{
    uint32_t a = align, off;

    if (a < 4 || (a & (a - 1))) a = 4;
    off = (lay->spare_used + a - 1) & ~(a - 1);
    if (off > lay->spare || memsz > lay->spare - off) return -1;
    lay->spare_used = off + memsz;
    return (int)off;
}

/* ld.so publishes the layout here; the libc reads it. */
extern cact_tls_layout_t cact_tls_layout;

/* The general-dynamic access path: where a variable at `offset` in the module
 * with TLS `id` lives, given the thread pointer `tp` (the TCB).  The module is
 * found by id — never by table position, since the table is compacted on
 * dlclose() — and the answer is `tp + off - total + offset`, the same
 * arithmetic R_386_TLS_TPOFF bakes into a static access.  Returns 0 when there
 * is no such module (which is a valid address in no program, so it doubles as
 * the error value). */
static inline void *cact_tls_addr(const cact_tls_layout_t *lay, uint32_t id,
                                  uint32_t offset, unsigned long tp)
{
    uint32_t i;
    for (i = 0; i < lay->count; i++)
        if (lay->mods[i].id == id)
            return (void *)((unsigned char *)tp + lay->mods[i].off + offset
                            - lay->total);
    return 0;
}

/* The libc's entry points into this (declared here so start.S and pthread.c
 * share one contract). */
void  __cact_tls_start(void);        /* install the main thread's block */
void *cact_tls_block_new(void);      /* a fresh block for a new thread, or 0 */
uint32_t cact_tls_plan(cact_tls_layout_t *lay, uint32_t nmods);
void *cact_tls_fill(void *block, const cact_tls_layout_t *lay);

/* Add a module that arrived after the layout was made (a dlopen()'d object's own
 * __thread): take a slice of the spare room, append it to the layout so threads
 * created later get its image, and copy that image into the blocks that already
 * exist.  Returns the module's offset in the block, or -1.  The module is given
 * the layout's next id (cact_tls_layout.mods[count-1].id is the id the loaders
 * record for a general-dynamic relocation). */
int  cact_tls_module_add(unsigned long vaddr, uint32_t filesz, uint32_t memsz,
                         uint32_t align);

/* Forget a module whose object is being unloaded, so nothing points at that
 * image any more.  The block offset is not reclaimed — glibc does not reclaim
 * its static TLS surplus on dlclose() either. */
void cact_tls_module_drop(uint32_t off);

#endif
