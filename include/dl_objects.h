#ifndef CACT_DL_OBJECTS_H
#define CACT_DL_OBJECTS_H

/* The table of objects that are already mapped, so dlopen() can reuse them
 * instead of mapping a second copy — in particular clibc.so itself, which every
 * plugin names in its DT_NEEDED.
 *
 * ld.so fills the first `count` entries before the program's _start runs and
 * marks them pinned; the libc appends whatever it dlopens.  Addresses are the
 * fixed link-time ones (CactOS maps every image at its link address). */

#include <stdint.h>

#define CACT_DL_MAX_OBJECTS 8
/* Program headers kept per object for dl_iterate_phdr (an image's ELF header is
 * not mapped, so the loaders copy the table out as they load it). */
#define CACT_DL_MAX_PHDRS   16

/* Part of the initial process image: dlclose() never unloads it. */
#define CACT_DL_PINNED  0x1u
/* Loaded with RTLD_GLOBAL: its symbols are visible to later dlopen()s. */
#define CACT_DL_GLOBAL  0x2u
/* No static TLS: the object carries no PT_TLS (see tls_off below). */
#define CACT_DL_NO_TLS  0xffffffffu

typedef struct {
    const char *name;        /* soname, or "<main>" for the executable */
    uint32_t    dyn;         /* PT_DYNAMIC address (0 = the object has none) */
    uint32_t    dynsz;
    uint32_t    symtab;      /* .dynsym address */
    uint32_t    strtab;      /* .dynstr address */
    uint32_t    symcount;    /* entries in .dynsym */
    uint32_t    strsz;
    /* This object's program headers, kept by whichever loader mapped it (the
     * ELF header of a CactOS image is not mapped).  `unsigned long` rather than
     * uint32_t for the same reason tls_layout.h's `vaddr` is: the host test
     * harness compiles one side of this struct on a 64-bit machine, where the
     * loader's static copy does not fit in 32 bits. */
    unsigned long phdr;
    uint32_t    phnum;
    /* The span the PT_LOAD segments cover, used to refuse a colliding mapping
     * and to tell which object an address belongs to.  `unsigned long` for the
     * same reason as `phdr`: on the host the test harness stands in for the
     * program, and its address does not fit in 32 bits. */
    unsigned long base;
    unsigned long size;
    uint32_t    flags;       /* CACT_DL_PINNED / CACT_DL_GLOBAL */
    uint32_t    refs;        /* dlopen() references */
    /* The namespace this object was loaded into (LM_ID_BASE for the program,
     * ld.so's libraries and anything plain dlopen() loaded).  A symbol lookup
     * started from an object only sees its own namespace plus the pinned
     * objects; see dlmopen() in <dlfcn.h> for what a namespace is here. */
    uint32_t    ns;
    /* Where this object's own __thread lives in every thread's static TLS block
     * (see include/tls_layout.h), or CACT_DL_NO_TLS when it carries none.  The
     * static-TLS relocations in the object resolve against it, and dlclose()
     * needs it to unhook the object's image from the layout. */
    uint32_t    tls_off;
    /* This object's TLS module id (see include/tls_layout.h) — what a
     * general-dynamic relocation names so ___tls_get_addr() can find the
     * module.  0 when the object has no TLS (tls_off is CACT_DL_NO_TLS). */
    uint32_t    tls_modid;
} cact_dl_object_t;

typedef struct {
    uint32_t         count;
    cact_dl_object_t objs[CACT_DL_MAX_OBJECTS];
} cact_dl_table_t;

/* The libc's table; ld.so fills it in before _start runs. */
extern cact_dl_table_t cact_dl_table;

#endif
