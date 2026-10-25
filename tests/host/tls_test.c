/* tls host test: the static-TLS block plan and fill (src/tls_static.c).
 *
 * The numbers here are the same arithmetic ld.so and the libc must agree on —
 * ld.so assigns the offsets and resolves R_386_TLS_TPOFF from them, the libc
 * builds each thread's block from the same layout — so getting this right is
 * what makes the two halves meet.
 *
 * A module's TLS image is described by a `uintptr_t vaddr`, which is a host
 * pointer here and a target address there, so this test can hand it real
 * buffers. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* The real header, by path: the test main is compiled with the *system*
 * headers (so glibc's sizeof/size_t are used), while the unit under test gets
 * the CactOS ones — sharing the header keeps the struct definition single-
 * sourced and lets each side instantiate it for its own ABI. */
#include "../../include/tls_layout.h"

uint32_t cact_tls_plan(cact_tls_layout_t *lay, uint32_t nmods);
void    *cact_tls_fill(void *block, const cact_tls_layout_t *lay);
void     __cact_tls_start(void);

extern unsigned int cact_host_tls_calls;
extern unsigned int cact_host_tls_base;

static int checks, fails;

static void cku32(const char *lbl, uint32_t got, uint32_t want) {
    checks++;
    if (got != want) { fails++; printf("FAIL %-28s got %u want %u\n", lbl, got, want); }
}

static void ckint(const char *lbl, int got, int want) {
    checks++;
    if (got != want) { fails++; printf("FAIL %-28s got %d want %d\n", lbl, got, want); }
}

static cact_tls_module_t *mod(cact_tls_layout_t *l, int i, unsigned long vaddr,
                              uint32_t filesz, uint32_t memsz, uint32_t align) {
    l->mods[i].off = 0;
    l->mods[i].vaddr = vaddr;
    l->mods[i].filesz = filesz;
    l->mods[i].memsz = memsz;
    l->mods[i].align = align;
    return &l->mods[i];
}

int main(void) {
    /* no modules: nothing to place */
    {
        cact_tls_layout_t l;
        memset(&l, 0, sizeof l);
        cku32("empty total", cact_tls_plan(&l, 0), 0);
        cku32("empty count", l.count, 0);
    }

    /* one 4-byte .tdata + 4-byte .tbss, align 4 */
    {
        cact_tls_layout_t l;
        static int tdata = 0x42424242;
        memset(&l, 0, sizeof l);
        mod(&l, 0, (unsigned long)&tdata, 4, 8, 4);
        uint32_t total = cact_tls_plan(&l, 1);
        cku32("one mod off", l.mods[0].off, 0);
        cku32("one mod total", total, 8);       /* the linker's assumption: end at the TP */
        cku32("one mod count", l.count, 1);

        /* the block: image copied, .tbss and padding zeroed, TCB self-pointing */
        unsigned char blk[64];
        memset(blk, 0xAA, sizeof blk);
        void *tcb = cact_tls_fill(blk, &l);
        checks++;
        if (tcb != blk + total) { fails++; printf("FAIL tcb position %p vs %p\n", tcb, blk + total); }
        checks++;
        if (memcmp(blk, &tdata, 4) != 0) { fails++; printf("FAIL image not copied\n"); }
        for (int i = 4; i < (int)total; i++) {
            if (blk[i] != 0) { fails++; printf("FAIL blk[%d]=%#x not zeroed\n", i, blk[i]); break; }
        }
        checks++;
        if (*(unsigned long *)tcb != (unsigned long)tcb) {
            fails++;
            printf("FAIL tcb self pointer: %#lx vs %#lx\n",
                   *(unsigned long *)tcb, (unsigned long)tcb);
        }
        /* the address the TPOFF formula would produce for a variable at TLS
         * offset v is TCB + (v - total): check it lands inside the image. */
        checks++;
        if ((unsigned char *)tcb + (0 - (long)total) != blk) {
            fails++;
            printf("FAIL TPOFF arithmetic\n");
        }
    }

    /* two modules: the library goes below, the program ends at the TP */
    {
        cact_tls_layout_t l;
        memset(&l, 0, sizeof l);
        mod(&l, 0, 0, 4, 4, 4);                 /* the program */
        mod(&l, 1, 0, 8, 8, 16);                /* a library */
        uint32_t total = cact_tls_plan(&l, 2);
        cku32("two mod1 off", l.mods[1].off, 0);
        cku32("two mod0 off", l.mods[0].off, 8);
        cku32("two total", total, 12);
    }

    /* a non-power-of-two (or zero/one) align must not be honoured */
    {
        cact_tls_layout_t l;
        memset(&l, 0, sizeof l);
        mod(&l, 0, 0, 1, 1, 3);
        mod(&l, 1, 0, 1, 1, 0);
        mod(&l, 2, 0, 1, 1, 1);
        uint32_t total = cact_tls_plan(&l, 3);
        cku32("bogus align offs", l.mods[1].off, 4);   /* 1 + 3 waste = aligned to 4 */
        cku32("bogus align total", total, 9);
    }

    /* more modules than fit: the extra ones are dropped, not written past */
    {
        cact_tls_layout_t l;
        memset(&l, 0, sizeof l);
        for (uint32_t i = 0; i < CACT_TLS_MAX_MODULES; i++)
            mod(&l, i, 0, 4, 4, 4);
        cact_tls_plan(&l, CACT_TLS_MAX_MODULES + 3);
        cku32("capped count", l.count, CACT_TLS_MAX_MODULES);
    }

    /* A thread's block: cact_tls_block_new returns the thread pointer directly,
     * so the whole fill path and the TPOFF arithmetic can be checked here.
     * (__cact_tls_start's base is only checked for "one call, non-zero" below:
     * struct user_desc.base_addr is 32-bit by ABI, so on a 64-bit host the
     * pointer would be truncated before any recorder could see it.) */
    {
        static int image[2] = { 0x11111111, 0x22222222 };
        memset(&cact_tls_layout, 0, sizeof cact_tls_layout);
        cact_tls_layout.mods[0].vaddr  = (unsigned long)image;
        cact_tls_layout.mods[0].filesz = 8;
        cact_tls_layout.mods[0].memsz  = 8;
        cact_tls_layout.mods[0].align  = 4;
        cact_tls_layout.total = cact_tls_plan(&cact_tls_layout, 1);

        void *tcb = cact_tls_block_new();
        checks++;
        if (!tcb || ((unsigned long)tcb & 3)) {      /* 4 is what %gs:0 needs */
            fails++;
            printf("FAIL block_new: tcb %p not 4-aligned\n", tcb);
        } else {
            checks++;
            if (*(unsigned long *)tcb != (unsigned long)tcb) {
                fails++;
                printf("FAIL block_new: TCB does not self-point\n");
            }
            /* where the R_386_TLS_TPOFF value would land a variable at offset v */
            checks++;
            if (memcmp((void *)((unsigned long)tcb - cact_tls_layout.total), image, 8) != 0) {
                fails++;
                printf("FAIL block_new: image not at TCB - total\n");
            }
        }

        cact_host_tls_calls = 0;
        cact_host_tls_base = 0;
        __cact_tls_start();
        checks++;
        if (cact_host_tls_calls != 1 || cact_host_tls_base == 0) {
            fails++;
            printf("FAIL start: calls=%u base=%#x\n", cact_host_tls_calls, cact_host_tls_base);
        }
    }

    /* an empty layout must leave %gs completely alone */
    {
        memset(&cact_tls_layout, 0, sizeof cact_tls_layout);
        cact_host_tls_calls = 0;
        __cact_tls_start();
        cku32("no-TLS: no syscall", cact_host_tls_calls, 0);
        checks++;
        if (cact_tls_block_new() != 0) {
            fails++;
            printf("FAIL no-TLS: block_new should be 0\n");
        }
    }

    /* The spare room: what an object that only turns up later (a dlopen()'d
     * plugin with a __thread of its own) is placed in.  The whole point of
     * keeping it at the BOTTOM is the invariant checked here — module 0 still
     * ends at the thread pointer, which is what the linker's own local-exec
     * displacement was computed against. */
    {
        cact_tls_layout_t l;
        memset(&l, 0, sizeof l);
        mod(&l, 0, 0, 4, 8, 4);                 /* the program */
        mod(&l, 1, 0, 4, 8, 4);                 /* a library */
        cku32("reserve: total before", cact_tls_plan(&l, 2), 16);
        cku32("reserve: total after", cact_tls_reserve(&l, 1024), 1040);
        cku32("reserve: spare", l.spare, 1024);
        cku32("reserve: mod1 moved", l.mods[1].off, 1024);
        cku32("reserve: mod0 moved", l.mods[0].off, 1032);
        cku32("reserve: top invariant", l.mods[0].off + l.mods[0].memsz, l.total);

        /* A size that is not a multiple of the round-up keeps the modules'
         * alignment too, since every offset moves by the rounded amount. */
        memset(&l, 0, sizeof l);
        mod(&l, 0, 0, 4, 4, 16);
        cact_tls_plan(&l, 1);
        cact_tls_reserve(&l, 100);
        cku32("reserve: rounded up", l.spare, 128);

        /* The one-step version ld.so uses.  The invariant it has to keep is the
         * one a missing plan breaks silently: module 0 must still end at the
         * thread pointer, and every module must have been placed at all. */
        memset(&l, 0, sizeof l);
        mod(&l, 0, 0, 4, 8, 4);
        cku32("plan_reserved: total", cact_tls_plan_reserved(&l, 1, 1024), 1032);
        cku32("plan_reserved: count", l.count, 1);
        cku32("plan_reserved: spare", l.spare, 1024);
        cku32("plan_reserved: mod0 off", l.mods[0].off, 1024);
        cku32("plan_reserved: top invariant", l.mods[0].off + l.mods[0].memsz, l.total);
        cku32("plan_reserved: mod0 id", l.mods[0].id, 1);
        cku32("plan_reserved: next_id", l.next_id, 2);

        /* Handing slices out: each one aligned, and a refusal once it is full. */
        memset(&l, 0, sizeof l);
        cact_tls_plan(&l, 0);
        cact_tls_reserve(&l, 64);
        ckint("take 1 (align 16)", cact_tls_take(&l, 8, 16), 0);
        ckint("take 2", cact_tls_take(&l, 8, 4), 8);
        ckint("take 3", cact_tls_take(&l, 8, 4), 16);
        ckint("take too big", cact_tls_take(&l, 64, 4), -1);
        ckint("take 4 (fills it exactly)", cact_tls_take(&l, 40, 4), 24);
        ckint("take exhausted", cact_tls_take(&l, 4, 4), -1);
    }

    /* A module that arrives late (a dlopen()'d object): it has to reach the
     * blocks that already exist, and the ones made after it, and it has to stop
     * being copied once its object is unloaded. */
    {
        static int image[2] = { 0x0BADF00D, 0x0D15EA5E };
        unsigned char *tp, *tp2, *slot;
        int off;

        memset(&cact_tls_layout, 0, sizeof cact_tls_layout);
        cact_tls_plan(&cact_tls_layout, 0);
        cact_tls_reserve(&cact_tls_layout, CACT_TLS_RESERVE);
        tp = (unsigned char *)cact_tls_block_new();

        checks++;
        if (!tp) { fails++; printf("FAIL module_add: no block\n"); return 1; }

        off = cact_tls_module_add((unsigned long)image, 8, 8, 4);
        ckint("module_add offset", off, 0);
        cku32("module_add count", cact_tls_layout.count, 1);
        cku32("module_add id", cact_tls_layout.mods[0].id, 1);
        cku32("module_add next_id", cact_tls_layout.next_id, 2);
        slot = tp - (long)cact_tls_layout.total + (uint32_t)off;
        checks++;
        if (memcmp(slot, image, 8) != 0) {
            fails++;
            printf("FAIL module_add: the image is not in the existing block\n");
        }
        /* The general-dynamic address path: id + offset + tp resolves into the
         * module's slice of the block. */
        checks++;
        if (cact_tls_addr(&cact_tls_layout, cact_tls_layout.mods[0].id, 4,
                          (unsigned long)tp) != (void *)(slot + 4)) {
            fails++;
            printf("FAIL cact_tls_addr: wrong address for a known module\n");
        }
        checks++;
        if (cact_tls_addr(&cact_tls_layout, 99, 0, (unsigned long)tp) != 0) {
            fails++;
            printf("FAIL cact_tls_addr: an unknown module id resolved\n");
        }
        tp2 = (unsigned char *)cact_tls_block_new();
        checks++;
        if (!tp2 || memcmp(tp2 - (long)cact_tls_layout.total + (uint32_t)off, image, 8) != 0) {
            fails++;
            printf("FAIL module_add: a later block did not get the image\n");
        }

        cact_tls_module_drop((uint32_t)off);
        cku32("module_drop memsz", cact_tls_layout.mods[0].memsz, 0);
        cku32("module_drop count", cact_tls_layout.count, 0);
        tp2 = (unsigned char *)cact_tls_block_new();
        checks++;
        if (!tp2 || *(int *)(tp2 - (long)cact_tls_layout.total + (uint32_t)off) != 0) {
            fails++;
            printf("FAIL module_drop: the image is still being copied\n");
        }

        /* A module loaded after one was unloaded gets a *fresh* id: a reused id
         * could make a stale general-dynamic pair land on the wrong module. */
        off = cact_tls_module_add((unsigned long)image, 8, 8, 4);
        cku32("module_add fresh id after drop", cact_tls_layout.mods[0].id, 2);
        cact_tls_module_drop((uint32_t)off);

        /* The room and the module table are both finite. */
        ckint("module_add past the spare", cact_tls_module_add((unsigned long)image, 8,
                                                              CACT_TLS_RESERVE * 2, 4), -1);
        for (int i = 0; i < CACT_TLS_MAX_MODULES; i++)
            cact_tls_module_add((unsigned long)image, 8, 8, 4);
        ckint("module_add past the table", cact_tls_module_add((unsigned long)image, 8, 8, 4), -1);
    }

    printf("tls: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
