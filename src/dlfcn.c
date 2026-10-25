/*
 * dlopen()/dlsym()/dlclose()/dlerror(): a runtime loader in userspace.
 *
 * ld.so maps the program and its DT_NEEDED libraries before _start runs and
 * hands this file the list of what it loaded (cact_dl_table) — clibc.so above
 * all, since every plugin names it in its DT_NEEDED.  dlopen() then maps a new
 * object from the filesystem, relocates it against that list, registers it and
 * runs its initialisers.
 *
 * CactOS policy is that every image is mapped at its **link-time address**, so
 * there is never a load bias and a symbol value or relocation addend is already
 * absolute.  A plugin must therefore be linked at an address no other image
 * uses; because a collision would silently replace live mappings, dlopen()
 * checks the new object's span against every known image and refuses with
 * dlerror() instead of mapping over one.
 *
 * A plugin may carry its own `__thread`, in either TLS model a shared object can
 * use.  Initial-exec (gcc/clang -ftls-model=initial-exec) resolves to
 * R_386_TLS_TPOFF against a GOT slot; general-dynamic, the default, resolves the
 * linker's { module id, offset } pair and calls ___tls_get_addr().  Either way
 * the module goes into the spare room the static TLS block reserved for exactly
 * this, its image is copied into every thread's block, and the relocations are
 * filled from its layout entry.  RTLD_LAZY and RTLD_NOW both relocate eagerly.
 */

#include "dlfcn.h"
#include "dl_elf.h"
#include "dl_objects.h"
#include "link.h"
#include "tls_layout.h"
#include "thread_local.h"
#include "string.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "mman.h"
#include "errno.h"
#include <stdint.h>

cact_dl_table_t cact_dl_table;

#define DL_MAX_SEGS 6

struct dl_loaded {
    char           path[256];
    uint32_t       seg_va[DL_MAX_SEGS];       /* the PT_LOAD mappings, to munmap */
    uint32_t       seg_size[DL_MAX_SEGS];
    int            nseg;
    uint32_t       deps[CACT_DL_MAX_OBJECTS]; /* the object's DT_NEEDED, by index */
    int            ndeps;
};

/* Indexed the same way as cact_dl_table.objs */
static struct dl_loaded dl_loaded[CACT_DL_MAX_OBJECTS];

/* Each object's program headers, kept because its ELF header is not mapped —
 * dl_iterate_phdr has to hand the callback a real table. */
static Elf32_Phdr dl_phdrs[CACT_DL_MAX_OBJECTS][CACT_DL_MAX_PHDRS];

/* ── error text (per thread, "pending until read") ───────────────────────── */

static void dl_err(const char *msg) {
    struct cact_tls *t = cact_tlocal_self();
    size_t i = 0;
    t->dlerr_set = 1;
    if (!msg) msg = "unknown error";
    while (msg[i] && i < sizeof t->dlerr - 1) { t->dlerr[i] = msg[i]; i++; }
    t->dlerr[i] = '\0';
}

static void dl_errf(const char *a, const char *b) {
    struct cact_tls *t = cact_tlocal_self();
    size_t i = 0;
    t->dlerr_set = 1;
    while (*a && i < sizeof t->dlerr - 1) t->dlerr[i++] = *a++;
    while (*b && i < sizeof t->dlerr - 1) t->dlerr[i++] = *b++;
    t->dlerr[i] = '\0';
}

char *dlerror(void) {
    struct cact_tls *t = cact_tlocal_self();
    if (!t->dlerr_set) return 0;
    t->dlerr_set = 0;                       /* reported once, as POSIX requires */
    return t->dlerr;
}

/* ── small helpers ──────────────────────────────────────────────────────── */

static void *at(uint32_t va) { return (void *)(uintptr_t)va; }

static int overlaps(unsigned long a, unsigned long asz,
                    unsigned long b, unsigned long bsz) {
    unsigned long long ae = (unsigned long long)a + asz;
    unsigned long long be = (unsigned long long)b + bsz;
    return (unsigned long long)a < be && (unsigned long long)b < ae;
}

/* Find a loaded object by the name it is known by, within one namespace: a name
 * loaded in another namespace is a different instance of it, not something to
 * reuse (and it cannot be loaded again either — same address; see dlmopen() in
 * <dlfcn.h>). */
static cact_dl_object_t *dl_find_in(const char *name, uint32_t ns) {
    if (!name) return 0;
    for (uint32_t i = 0; i < cact_dl_table.count; i++)
        if (cact_dl_table.objs[i].name && cact_dl_table.objs[i].ns == ns &&
            strcmp(cact_dl_table.objs[i].name, name) == 0)
            return &cact_dl_table.objs[i];
    return 0;
}

/* A pinned object by name: the program and whatever ld.so loaded are shared by
 * every namespace, so a namespace's dependency on one of them resolves to the
 * single live copy rather than a second mapping (which the fixed address could
 * not hold anyway). */
static cact_dl_object_t *dl_find_pinned(const char *name) {
    if (!name) return 0;
    for (uint32_t i = 0; i < cact_dl_table.count; i++) {
        cact_dl_object_t *o = &cact_dl_table.objs[i];
        if (o->name && (o->flags & CACT_DL_PINNED) && strcmp(o->name, name) == 0)
            return o;
    }
    return 0;
}

static uint32_t dl_symcount(uint32_t hash, uint32_t gnu_hash, uint32_t symtab) {
    if (!symtab) return 0;
    if (hash) return ((uint32_t *)at(hash))[1];             /* nchain */

    if (gnu_hash) {                                         /* walk the chains */
        uint32_t *gh = (uint32_t *)at(gnu_hash);
        uint32_t nbuckets = gh[0], symoffset = gh[1], bloom_size = gh[2];
        uint32_t *buckets, *chains, max_b = 0, i, ci, guard;
        if (bloom_size > 1024) bloom_size = 1024;
        if (nbuckets == 0) return 0;
        buckets = gh + 4 + bloom_size;
        chains  = buckets + nbuckets;
        for (i = 0; i < nbuckets; i++) if (buckets[i] > max_b) max_b = buckets[i];
        if (max_b < symoffset) return symoffset;
        ci = max_b - symoffset;
        for (guard = 0; guard < 8192; guard++, ci++)
            if (chains[ci] & 1u) return symoffset + ci + 1;
        return symoffset + 8192;
    }
    return 4096;                                            /* same fallback as ld.so */
}

/* One symbol, by name, in one object.  0 when it is not there. */
static uint32_t dl_lookup_in(cact_dl_object_t *o, const char *name) {
    if (!o || !o->name || !o->symtab || !o->strtab) return 0;
    for (uint32_t j = 1; j < o->symcount; j++) {
        Elf32_Sym *sym = &((Elf32_Sym *)at(o->symtab))[j];
        uint32_t bind;
        const char *sn;
        if (sym->st_shndx == SHN_UNDEF) continue;
        bind = ELF32_ST_BIND(sym->st_info);
        if (bind != STB_GLOBAL && bind != STB_WEAK) continue;
        if (sym->st_value == 0) continue;                   /* the same rule ld.so uses */
        if (sym->st_name >= o->strsz) continue;
        sn = (const char *)at(o->strtab) + sym->st_name;
        if (strcmp(sn, name) == 0) return sym->st_value;
    }
    return 0;
}

/* Search order for a relocation inside object `self`: the object itself, then
 * its DT_NEEDED depth first, then everything the loader pinned (the program and
 * clibc.so) and anything opened with RTLD_GLOBAL.  `owner`, when not NULL,
 * receives the index of the object the symbol was found in — R_386_TLS_DTPMOD32
 * needs that object's TLS module id, not just the value. */
static uint32_t dl_resolve_depth(int self, const char *name, int *owner, int depth) {
    uint32_t v;
    if (depth > 4) return 0;

    if (self >= 0 && cact_dl_table.objs[self].name) {
        v = dl_lookup_in(&cact_dl_table.objs[self], name);
        if (v) { if (owner) *owner = self; return v; }
    }
    if (self >= 0) {
        for (int d = 0; d < dl_loaded[self].ndeps; d++) {
            int dep = (int)dl_loaded[self].deps[d];
            if (dep == self) continue;
            v = dl_resolve_depth(dep, name, owner, depth + 1);
            if (v) return v;
        }
    }
    {
        uint32_t self_ns = (self >= 0) ? cact_dl_table.objs[self].ns : LM_ID_BASE;
        for (uint32_t i = 0; i < cact_dl_table.count; i++) {
            cact_dl_object_t *o = &cact_dl_table.objs[i];
            if (!o->name || (int)i == self) continue;
            /* The pinned objects (the program and its libraries) are shared by
             * every namespace; anything else must be in this object's own one. */
            if (!(o->flags & CACT_DL_PINNED) &&
                !((o->flags & CACT_DL_GLOBAL) && o->ns == self_ns)) continue;
            v = dl_lookup_in(o, name);
            if (v) { if (owner) *owner = (int)i; return v; }
        }
    }
    return 0;
}

static uint32_t dl_resolve(int self, const char *name) {
    return dl_resolve_depth(self, name, 0, 0);
}

/* The global scope, which RTLD_DEFAULT and dlopen(NULL) address: everything
 * pinned (the program and its libraries) plus whatever was opened RTLD_GLOBAL
 * *in the base namespace*, together with those objects' own dependencies.  A
 * plugin opened RTLD_LOCAL, and anything belonging to a namespace made by
 * dlmopen(), is deliberately not here — that isolation is the whole point of a
 * namespace. */
static uint32_t dl_lookup_global(const char *name) {
    uint32_t v, i;
    int d;
    uint32_t idx[CACT_DL_MAX_OBJECTS];
    uint32_t nx = 0;

    for (i = 0; i < cact_dl_table.count; i++) {
        cact_dl_object_t *o = &cact_dl_table.objs[i];
        if (!o->name) continue;
        if (!(o->flags & CACT_DL_PINNED) &&
            !((o->flags & CACT_DL_GLOBAL) && o->ns == LM_ID_BASE)) continue;
        idx[nx++] = i;
        v = dl_lookup_in(o, name);
        if (v) return v;
    }
    for (i = 0; i < nx; i++) {
        for (d = 0; d < dl_loaded[idx[i]].ndeps; d++) {
            cact_dl_object_t *dep = &cact_dl_table.objs[dl_loaded[idx[i]].deps[d]];
            if (!dep->name) continue;
            v = dl_lookup_in(dep, name);
            if (v) return v;
        }
    }
    return 0;
}

/* ── relocation ─────────────────────────────────────────────────────────── */

/* One relocation table.  `use_addend` selects RELA semantics (.rela.dyn). */
static int dl_relocs(cact_dl_object_t *o, int self,
                     uint32_t addr, uint32_t size, uint32_t ent, int use_addend)
{
    uint32_t i, n;
    if (!addr || !size || !ent) return 0;
    n = size / ent;

    for (i = 0; i < n; i++) {
        uint32_t off = addr + i * ent;
        uint32_t r_offset = ((uint32_t *)at(off))[0];
        uint32_t r_info   = ((uint32_t *)at(off))[1];
        int32_t  r_addend = use_addend ? ((int32_t *)at(off))[2] : 0;
        uint32_t type = ELF32_R_TYPE(r_info);
        uint32_t si   = ELF32_R_SYM(r_info);
        uint32_t *target = (uint32_t *)at(r_offset);
        uint32_t S = 0, A;
        int owner = -1;                             /* which object defined it */

        if (si && si < o->symcount) {
            Elf32_Sym *sym = &((Elf32_Sym *)at(o->symtab))[si];
            if (sym->st_shndx == SHN_UNDEF) {
                if (sym->st_name < o->strsz) {
                    const char *sname = (const char *)at(o->strtab) + sym->st_name;
                    S = dl_resolve_depth(self, sname, &owner, 0);
                    /* A weak undefined symbol is allowed to come out as 0. */
                    if (!S && ELF32_ST_BIND(sym->st_info) != STB_WEAK) {
                        dl_errf("undefined symbol: ", sname);
                        return -1;
                    }
                }
            } else {
                S = sym->st_value;
                owner = self;
            }
        }

        switch (type) {
        case R_386_NONE:
            break;
        case R_386_32:                              /* S + A */
            A = use_addend ? (uint32_t)r_addend : *target;
            *target = S + A;
            break;
        case R_386_PC32:                            /* S + A - P */
            A = use_addend ? (uint32_t)r_addend : *target;
            *target = S + A - r_offset;
            break;
        case R_386_GLOB_DAT:
        case R_386_JMP_SLOT:                        /* S */
            *target = S;
            break;
        case R_386_RELATIVE:                        /* B + A, B = 0 */
            if (use_addend) *target = (uint32_t)r_addend;
            break;
        case R_386_COPY:
            if (S && si < o->symcount) {
                Elf32_Sym *sym = &((Elf32_Sym *)at(o->symtab))[si];
                if (sym->st_size && sym->st_size <= 65536)
                    memcpy(target, at(S), sym->st_size);
            }
            break;
        case R_386_TLS_TPOFF:
        case R_386_TLS_TPOFF32:
            /* Static TLS: the displacement from the thread pointer down to the
             * variable, which GNU ld asks for both from a local-exec access (a
             * code displacement) and from the GOT slot an initial-exec access
             * loads and adds to %gs:0.  The symbol's st_value is the variable's
             * offset inside this object's own TLS image, so the answer is
             * `block_offset + st_value - total`.  A variable defined in
             * *another* object would need that object's block offset instead;
             * nothing a sane toolchain produces needs that, so it is refused
             * rather than resolved wrongly. */
            if (!si || si >= o->symcount || o->tls_off == CACT_DL_NO_TLS) {
                dl_err("unsupported TLS relocation in this object");
                return -1;
            } else {
                Elf32_Sym *sym = &((Elf32_Sym *)at(o->symtab))[si];
                if (sym->st_shndx == SHN_UNDEF ||
                    ELF32_ST_TYPE(sym->st_info) != STT_TLS) {
                    dl_err("unsupported TLS relocation in this object");
                    return -1;
                }
            }
            A = use_addend ? (uint32_t)r_addend : *target;
            *target = o->tls_off + S + A - cact_tls_layout.total;
            break;
        case R_386_TLS_DTPMOD32:
            /* General-dynamic: the two GOT words the linker emits next to each
             * other — this one takes the defining object's TLS module id, and
             * R_386_TLS_DTPOFF32 the offset inside that module.  ___
             * tls_get_addr() then finds the module by id in the layout.  The
             * symbol has to live in an object that actually carries TLS. */
            if (owner < 0 ||
                cact_dl_table.objs[owner].tls_off == CACT_DL_NO_TLS) {
                dl_err("unsupported TLS relocation in this object");
                return -1;
            }
            A = use_addend ? (uint32_t)r_addend : *target;
            *target = cact_dl_table.objs[owner].tls_modid + A;
            break;
        case R_386_TLS_DTPOFF32:
            A = use_addend ? (uint32_t)r_addend : *target;
            *target = S + A;
            break;
        default:
            if (type >= R_386_TLS_TPOFF && type <= R_386_TLS_TPOFF32) {
                dl_err("unsupported TLS relocation in this object");
                return -1;
            }
            break;                                  /* anything else: ignore, as ld.so does */
        }
    }
    return 0;
}

/* ── mapping ────────────────────────────────────────────────────────────── */

static unsigned char *dl_read_file(const char *path, uint32_t *len) {
    int fd = open(path, O_RDONLY);
    uint32_t cap = 64 * 1024, n = 0;
    unsigned char *buf;

    if (fd < 0) return 0;
    buf = (unsigned char *)malloc(cap);
    if (!buf) { close(fd); return 0; }

    for (;;) {
        ssize_t r;
        if (n == cap) {
            unsigned char *nb = (unsigned char *)realloc(buf, cap * 2);
            if (!nb) { free(buf); close(fd); return 0; }
            buf = nb;
            cap *= 2;
        }
        r = read(fd, buf + n, cap - n);
        if (r < 0) {
            if (errno == EINTR) continue;
            free(buf);
            close(fd);
            return 0;
        }
        if (r == 0) break;
        n += (uint32_t)r;
    }
    close(fd);
    *len = n;
    return buf;
}

/* "/usr/lib/<name>" for a bare name, the path itself when it has a '/'. */
static void dl_path_for(const char *name, char *out, size_t cap) {
    size_t i = 0;
    if (!strchr(name, '/')) {
        if (cap < 10) { out[0] = '\0'; return; }
        strcpy(out, "/usr/lib/");
        i = 9;
    }
    while (*name && i + 1 < cap) out[i++] = *name++;
    out[i] = '\0';
}

/* Map, register and relocate one object, into namespace `ns`.  Returns its
 * table index, or -1. */
static int dl_load_one(const char *name, int global, uint32_t ns)
{
    char path[256];
    uint32_t filelen = 0, i;
    unsigned char *file;
    Elf32_Ehdr *eh;
    Elf32_Phdr *ph;
    uint32_t dyn = 0, dynsz = 0;
    uint32_t lo = 0, hi = 0;
    uint32_t symtab = 0, strtab = 0, strsz = 0, hash = 0, gnu_hash = 0;
    uint32_t soname_off = 0xffffffffu;
    uint32_t tls_vaddr = 0, tls_filesz = 0, tls_memsz = 0, tls_align = 4;
    int idx, nseg = 0;

    dl_path_for(name, path, sizeof path);
    file = dl_read_file(path, &filelen);
    if (!file) { dl_errf("cannot read ", path); return -1; }
    if (filelen < sizeof(Elf32_Ehdr)) { dl_errf("not an ELF file: ", path); goto fail; }

    eh = (Elf32_Ehdr *)file;
    if (eh->e_ident[0] != ELF32_EI_MAGIC0 || eh->e_ident[1] != ELF32_EI_MAGIC1 ||
        eh->e_ident[2] != ELF32_EI_MAGIC2 || eh->e_ident[3] != ELF32_EI_MAGIC3) {
        dl_errf("not an ELF file: ", path);
        goto fail;
    }
    if (eh->e_phentsize < sizeof(Elf32_Phdr) || eh->e_phnum == 0 ||
        (uint32_t)eh->e_phoff + (uint32_t)eh->e_phnum * eh->e_phentsize > filelen) {
        dl_errf("bad program headers: ", path);
        goto fail;
    }
    ph = (Elf32_Phdr *)(file + eh->e_phoff);

    if (cact_dl_table.count >= CACT_DL_MAX_OBJECTS) {
        dl_err("too many objects loaded");
        goto fail;
    }
    idx = (int)cact_dl_table.count;
    dl_loaded[idx].nseg = 0;
    dl_loaded[idx].ndeps = 0;

    /* The span first, so a colliding image is refused before anything is mapped. */
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) continue;
        {
            uint32_t b = ph[i].p_vaddr & ~0xFFFu;
            uint32_t e = (ph[i].p_vaddr + ph[i].p_memsz + 0xFFFu) & ~0xFFFu;
            if (!lo || b < lo) lo = b;
            if (e > hi) hi = e;
        }
    }
    if (!lo) { dl_errf("no loadable segments: ", path); goto fail; }

    for (i = 0; i < cact_dl_table.count; i++) {
        cact_dl_object_t *o = &cact_dl_table.objs[i];
        if (o->name && o->size && overlaps(lo, hi - lo, o->base, o->size)) {
            dl_errf("would map over an already loaded image: ", path);
            goto fail;
        }
    }

    /* Map and copy. */
    for (i = 0; i < eh->e_phnum; i++) {
        uint32_t seg_va;
        if (ph[i].p_type == PT_DYNAMIC) { dyn = ph[i].p_vaddr; dynsz = ph[i].p_filesz; continue; }
        if (ph[i].p_type == PT_TLS) {
            /* Not mapped here: the .tdata it describes already sits in one of
             * the PT_LOAD segments above.  All the loader needs is where the
             * initial image is and how big the module's block is. */
            tls_vaddr  = ph[i].p_vaddr;
            tls_filesz = ph[i].p_filesz;
            tls_memsz  = ph[i].p_memsz;
            tls_align  = ph[i].p_align;
            continue;
        }
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) continue;

        seg_va = ph[i].p_vaddr & ~0xFFFu;
        if (nseg >= DL_MAX_SEGS) { dl_errf("too many segments: ", path); goto unmap; }
        if (mmap(at(seg_va), ((ph[i].p_vaddr + ph[i].p_memsz + 0xFFFu) & ~0xFFFu) - seg_va,
                 PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_FIXED | MAP_ANON, -1, 0) == MAP_FAILED) {
            dl_errf("mmap failed: ", path);
            goto unmap;
        }
        dl_loaded[idx].seg_va[nseg] = seg_va;
        dl_loaded[idx].seg_size[nseg] =
            ((ph[i].p_vaddr + ph[i].p_memsz + 0xFFFu) & ~0xFFFu) - seg_va;
        nseg++;

        if (ph[i].p_filesz) {
            if (ph[i].p_offset + ph[i].p_filesz > filelen) {
                dl_errf("truncated segment: ", path);
                goto unmap;
            }
            memcpy(at(ph[i].p_vaddr), file + ph[i].p_offset, ph[i].p_filesz);
        }
    }
    dl_loaded[idx].nseg = nseg;
    if (!dyn) { dl_errf("no PT_DYNAMIC: ", path); goto unmap; }

    /* Register before relocating, so the object can resolve against itself. */
    {
        cact_dl_object_t *o = &cact_dl_table.objs[idx];
        memcpy(dl_loaded[idx].path, path, sizeof dl_loaded[idx].path);
        dl_loaded[idx].path[sizeof dl_loaded[idx].path - 1] = '\0';
        memset(o, 0, sizeof *o);
        o->name  = dl_loaded[idx].path;         /* until DT_SONAME is read */
        o->dyn   = dyn;
        o->dynsz = dynsz;
        o->base  = lo;
        o->size  = hi - lo;
        o->refs  = 1;
        o->flags = global ? CACT_DL_GLOBAL : 0;
        o->ns    = ns;
        o->tls_off = CACT_DL_NO_TLS;
        /* the program headers, copied out of the file image.  The cast goes
         * straight to unsigned long: this libc's uintptr_t is 32 bits, so going
         * through it would truncate on the 64-bit host the tests build for. */
        o->phdr  = (unsigned long)&dl_phdrs[idx][0];
        o->phnum = eh->e_phnum < CACT_DL_MAX_PHDRS ? (uint32_t)eh->e_phnum
                                                  : CACT_DL_MAX_PHDRS;
        for (i = 0; i < o->phnum; i++) dl_phdrs[idx][i] = ph[i];
        cact_dl_table.count++;
    }

    /* Walk the dynamic section, then its dependencies, then relocate. */
    {
        Elf32_Dyn *d = (Elf32_Dyn *)at(dyn);
        uint32_t maxent = dynsz / sizeof(Elf32_Dyn);
        uint32_t needed[16];
        uint32_t rel = 0, relsz = 0, relent = 8;
        uint32_t rela = 0, relasz = 0, relaent = 12;
        uint32_t jmprel = 0, jmprelsz = 0, pltrel = DT_REL;
        uint32_t initfn = 0, initarr = 0, initarrsz = 0;
        int nneed = 0;
        cact_dl_object_t *o = &cact_dl_table.objs[idx];

        for (i = 0; i < maxent; i++) {
            if (d[i].d_tag == DT_NULL) break;
            switch (d[i].d_tag) {
            case DT_SYMTAB:       symtab = d[i].d_un.d_ptr; break;
            case DT_STRTAB:       strtab = d[i].d_un.d_ptr; break;
            case DT_STRSZ:        strsz  = d[i].d_un.d_val; break;
            case DT_HASH:         hash   = d[i].d_un.d_ptr; break;
            case DT_GNU_HASH:     gnu_hash = d[i].d_un.d_ptr; break;
            case DT_SONAME:       soname_off = d[i].d_un.d_val; break;
            case DT_REL:          rel    = d[i].d_un.d_ptr; break;
            case DT_RELSZ:        relsz  = d[i].d_un.d_val; break;
            case DT_RELENT:       relent = d[i].d_un.d_val; break;
            case DT_RELA:         rela   = d[i].d_un.d_ptr; break;
            case DT_RELASZ:       relasz = d[i].d_un.d_val; break;
            case DT_RELAENT:      relaent = d[i].d_un.d_val; break;
            case DT_JMPREL:       jmprel = d[i].d_un.d_ptr; break;
            case DT_PLTRELSZ:     jmprelsz = d[i].d_un.d_val; break;
            case DT_PLTREL:       pltrel = d[i].d_un.d_val; break;
            case DT_INIT:         initfn = d[i].d_un.d_ptr; break;
            case DT_INIT_ARRAY:   initarr = d[i].d_un.d_ptr; break;
            case DT_INIT_ARRAYSZ: initarrsz = d[i].d_un.d_val; break;
            case DT_NEEDED:
                if (nneed < 16) needed[nneed++] = d[i].d_un.d_val;
                break;
            default: break;
            }
        }

        o->symtab   = symtab;
        o->strtab   = strtab;
        o->strsz    = strsz;
        o->symcount = dl_symcount(hash, gnu_hash, symtab);

        if (soname_off != 0xffffffffu && strtab && soname_off < strsz)
            o->name = (const char *)at(strtab) + soname_off;

        /* Dependencies first: their symbols have to exist before ours resolve. */
        for (i = 0; i < (uint32_t)nneed; i++) {
            const char *depname;
            cact_dl_object_t *dep;
            if (!strtab || needed[i] >= strsz) continue;
            depname = (const char *)at(strtab) + needed[i];
            dep = dl_find_in(depname, ns);      /* a dependency joins this namespace */
            if (!dep) dep = dl_find_pinned(depname);   /* …unless it is a shared one */
            if (!dep) {
                int didx = dl_load_one(depname, 0, ns);
                if (didx < 0) goto unload;
                dep = &cact_dl_table.objs[didx];
            }
            if (dl_loaded[idx].ndeps < CACT_DL_MAX_OBJECTS)
                dl_loaded[idx].deps[dl_loaded[idx].ndeps++] = (uint32_t)(dep - cact_dl_table.objs);
        }

        /* The object's own `__thread`, if it has one: take a slice of the spare
         * room the static TLS block reserved, and copy the initial image into
         * every thread block that already exists.  This has to happen before
         * the relocations below: an initial-exec access resolves against the
         * block offset it hands back, and a general-dynamic one names the TLS
         * module id it assigns. */
        if (tls_memsz) {
            int off = cact_tls_module_add((unsigned long)tls_vaddr, tls_filesz,
                                          tls_memsz, tls_align);
            if (off < 0) {
                dl_errf("cannot allocate memory in static TLS block: ", path);
                goto unload;
            }
            o->tls_off   = (uint32_t)off;
            o->tls_modid = cact_tls_layout.mods[cact_tls_layout.count - 1].id;
        }

        if (dl_relocs(o, idx, rel, relsz, relent, 0) != 0) goto unload;
        if (dl_relocs(o, idx, rela, relasz, relaent, 1) != 0) goto unload;
        if (jmprel && jmprelsz) {
            if (pltrel == DT_RELA) {
                if (dl_relocs(o, idx, jmprel, jmprelsz, sizeof(Elf32_Rela), 1) != 0) goto unload;
            } else {
                if (dl_relocs(o, idx, jmprel, jmprelsz, sizeof(Elf32_Rel), 0) != 0) goto unload;
            }
        }

        /* dlopen() runs the object's initialisers, as POSIX requires. */
        if (initfn) ((void (*)(void))at(initfn))();
        for (i = 0; initarr && i + 4 <= initarrsz; i += 4) {
            uint32_t fp = ((uint32_t *)at(initarr))[i / 4];
            if (fp) ((void (*)(void))at(fp))();
        }
    }

    free(file);
    return idx;

unmap:
    for (i = 0; i < (uint32_t)nseg; i++)
        munmap(at(dl_loaded[idx].seg_va[i]), dl_loaded[idx].seg_size[i]);
    dl_loaded[idx].nseg = 0;
    goto fail;

unload:
    if (cact_dl_table.objs[idx].tls_off != CACT_DL_NO_TLS)
        cact_tls_module_drop(cact_dl_table.objs[idx].tls_off);
    for (i = 0; i < (uint32_t)nseg; i++)
        munmap(at(dl_loaded[idx].seg_va[i]), dl_loaded[idx].seg_size[i]);
    dl_loaded[idx].nseg = 0;
    cact_dl_table.objs[idx].name = 0;
    cact_dl_table.count--;
fail:
    free(file);
    return -1;
}

/* ── namespaces ─────────────────────────────────────────────────────────────
 *
 * A namespace is a symbol scope, not a second address space (see dlmopen() in
 * <dlfcn.h>).  Namespace 0 is LM_ID_BASE — the program, whatever ld.so loaded,
 * and everything plain dlopen() opens.  dlmopen(LM_ID_NEWLM, …) hands out an id
 * from here, and it is given back once the last object of that namespace is
 * unmapped, so a stale id cannot name a live scope. */
#define DL_MAX_NS 16

static unsigned char g_ns_used[DL_MAX_NS];      /* [0] is never handed out */

static uint32_t dl_ns_new(void) {
    for (uint32_t n = 1; n < DL_MAX_NS; n++) {
        if (!g_ns_used[n]) { g_ns_used[n] = 1; return n; }
    }
    return 0;
}

static int dl_ns_known(uint32_t ns) {
    return ns == LM_ID_BASE || (ns < DL_MAX_NS && g_ns_used[ns]);
}

static void dl_ns_release_if_empty(uint32_t ns) {
    if (ns == LM_ID_BASE || ns >= DL_MAX_NS) return;
    for (uint32_t i = 0; i < cact_dl_table.count; i++)
        if (cact_dl_table.objs[i].name && cact_dl_table.objs[i].ns == ns) return;
    g_ns_used[ns] = 0;
}

/* ── public entry points ────────────────────────────────────────────────── */

/* Load `filename` into namespace `ns`, reusing it if it is already there. */
static void *dl_open_into(const char *filename, int flags, uint32_t ns) {
    char path[256];
    cact_dl_object_t *o;
    int idx;

    if (!filename) return (void *)1;            /* a handle for the global scope */

    o = dl_find_in(filename, ns);               /* already in *this* namespace */
    if (!o) {
        dl_path_for(filename, path, sizeof path);
        o = dl_find_in(path, ns);
    }
    /* A shared (pinned) library is the same object in every namespace: naming it
     * explicitly resolves to the live copy, not a second mapping. */
    if (!o) o = dl_find_pinned(filename);
    if (o) { o->refs++; return (void *)o; }

    idx = dl_load_one(filename, (flags & RTLD_GLOBAL) != 0, ns);
    if (idx < 0) return 0;
    return (void *)&cact_dl_table.objs[idx];
}

void *dlopen(const char *filename, int flags) {
    return dl_open_into(filename, flags, LM_ID_BASE);
}

/* Which loaded object an address belongs to, or -1.  RTLD_NEXT needs it for
 * the caller's return address: "next" means "later in the load order than the
 * object that asked". */
static int dl_object_of(unsigned long a) {
    uint32_t i;
    for (i = 0; i < cact_dl_table.count; i++) {
        cact_dl_object_t *o = &cact_dl_table.objs[i];
        if (!o->name || !o->size) continue;
        if (a >= o->base && a < o->base + o->size)
            return (int)i;
    }
    return -1;
}

void *dlsym(void *handle, const char *symbol) {
    uint32_t v;

    if (!symbol) { dl_err("dlsym: no symbol name"); return 0; }

    if (handle == (void *)-1) {                 /* RTLD_NEXT */
        int from = dl_object_of((unsigned long)__builtin_return_address(0));
        if (from < 0) {
            dl_err("RTLD_NEXT: cannot tell which object called dlsym");
            return 0;
        }
        for (uint32_t i = (uint32_t)from + 1; i < cact_dl_table.count; i++) {
            cact_dl_object_t *o = &cact_dl_table.objs[i];
            if (!o->name || !(o->flags & (CACT_DL_PINNED | CACT_DL_GLOBAL))) continue;
            v = dl_lookup_in(o, symbol);
            if (v) return at(v);
        }
        dl_errf("symbol not found after this object: ", symbol);
        return 0;
    }

    if (!handle || handle == (void *)1) {       /* RTLD_DEFAULT / dlopen(NULL) */
        v = dl_lookup_global(symbol);
        if (v) return at(v);
        dl_errf("symbol not found: ", symbol);
        return 0;
    }

    {
        int self = (int)((cact_dl_object_t *)handle - cact_dl_table.objs);
        if (self < 0 || self >= CACT_DL_MAX_OBJECTS || !cact_dl_table.objs[self].name) {
            dl_err("dlsym: invalid handle");
            return 0;
        }
        v = dl_resolve(self, symbol);
        if (v) return at(v);
    }
    dl_errf("symbol not found: ", symbol);
    return 0;
}

int dlclose(void *handle) {
    cact_dl_object_t *o;
    int self, i;

    if (!handle || handle == (void *)1) return 0;
    if (handle == (void *)-1) return 0;
    o = (cact_dl_object_t *)handle;
    self = (int)(o - cact_dl_table.objs);
    if (self < 0 || self >= CACT_DL_MAX_OBJECTS || !o->name) {
        dl_err("dlclose: invalid handle");
        return -1;
    }
    if (o->flags & CACT_DL_PINNED) return 0;    /* part of the process image */
    if (o->refs > 1) { o->refs--; return 0; }

    /* Unhook the object's TLS image first: the layout still names it, and a
     * thread created after this point would otherwise read from the mapping
     * that is about to go away. */
    if (o->tls_off != CACT_DL_NO_TLS) {
        cact_tls_module_drop(o->tls_off);
        o->tls_off = CACT_DL_NO_TLS;
    }
    for (i = 0; i < dl_loaded[self].nseg; i++)
        munmap(at(dl_loaded[self].seg_va[i]), dl_loaded[self].seg_size[i]);
    dl_loaded[self].nseg = 0;
    dl_loaded[self].ndeps = 0;
    o->name = 0;                                /* the slot is reusable now */
    /* An empty namespace goes away with its last object, so a stale Lmid_t can
     * never name a live scope. */
    dl_ns_release_if_empty(o->ns);
    return 0;
}

/* The GNU versioned lookup.  CactOS images carry no version definitions, so the
 * version is accepted and ignored — the same behaviour a program gets from
 * dlsym() on a platform without symbol versioning. */
void *dlvsym(void *handle, const char *symbol, const char *version) {
    (void)version;
    return dlsym(handle, symbol);
}

/* Which object — and which of its symbols — an address belongs to.  Returns 0
 * when the address is in no loaded object, 1 otherwise; dli_sname/dli_saddr
 * stay 0 when no symbol covers the address, as glibc leaves them. */
int dladdr(const void *addr, Dl_info *info) {
    unsigned long a = (unsigned long)addr;
    uint32_t i;

    if (!info) return 0;
    info->dli_fname = 0;
    info->dli_fbase = 0;
    info->dli_sname = 0;
    info->dli_saddr = 0;
    if (a > 0xffffffffu) return 0;              /* no image lives up there */

    for (i = 0; i < cact_dl_table.count; i++) {
        cact_dl_object_t *o = &cact_dl_table.objs[i];
        uint32_t best_va = 0, j;
        const char *best_name = 0;

        if (!o->name || !o->size) continue;
        if (a < o->base || a >= o->base + o->size) continue;

        info->dli_fname = o->name;
        info->dli_fbase = at(o->base);

        if (o->symtab && o->strtab) {
            for (j = 1; j < o->symcount; j++) {
                Elf32_Sym *sym = &((Elf32_Sym *)at(o->symtab))[j];
                if (sym->st_shndx == SHN_UNDEF) continue;
                if (!sym->st_value || sym->st_value > (uint32_t)a) continue;
                if (sym->st_name >= o->strsz) continue;
                if (sym->st_value >= best_va) {     /* the nearest one at or below */
                    best_va = sym->st_value;
                    best_name = (const char *)at(o->strtab) + sym->st_name;
                }
            }
        }
        if (best_name) {
            info->dli_sname = best_name;
            info->dli_saddr = at(best_va);
        }
        return 1;
    }
    return 0;
}

/* Walk the mapped objects, as glibc does: the program first, then whatever was
 * loaded.  `dlpi_addr` is always 0 (no load bias), and the program headers are
 * the loader's own copies.  A callback returning nonzero stops the walk and its
 * value becomes the return value. */
int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data) {
    struct dl_phdr_info info;
    uint32_t i;

    if (!callback) return 0;

    for (i = 0; i < cact_dl_table.count; i++) {
        cact_dl_object_t *o = &cact_dl_table.objs[i];
        int r;

        if (!o->name) continue;
        memset(&info, 0, sizeof info);
        info.dlpi_addr  = 0;
        info.dlpi_name  = o->name;
        info.dlpi_phdr  = (const Elf32_Phdr *)o->phdr;
        info.dlpi_phnum = (uint16_t)o->phnum;

        r = callback(&info, sizeof info, data);
        if (r) return r;
    }
    return 0;
}

/* ── dlinfo ─────────────────────────────────────────────────────────────── */

/* One link map per object, filled in on demand: the table changes with
 * dlopen()/dlclose(), and glibc's link maps are stable pointers, so the array
 * is static and the entries are updated in place rather than rebuilt. */
static struct link_map dl_lms[CACT_DL_MAX_OBJECTS];

/* Chain the objects that currently have a name, in load order (the program
 * first, as glibc's namespace head is). */
static void dl_lms_build(void) {
    int prev = -1;
    uint32_t i;

    for (i = 0; i < cact_dl_table.count; i++) {
        cact_dl_object_t *o = &cact_dl_table.objs[i];
        if (!o->name) continue;
        dl_lms[i].l_addr = 0;                    /* no load bias, ever */
        dl_lms[i].l_name = (char *)o->name;
        dl_lms[i].l_ld   = (Elf32_Dyn *)(uintptr_t)o->dyn;
        dl_lms[i].l_prev = prev < 0 ? 0 : &dl_lms[prev];
        dl_lms[i].l_next = 0;
        if (prev >= 0) dl_lms[prev].l_next = &dl_lms[i];
        prev = (int)i;
    }
}

/* The object's directory: what its path is up to the last '/'.  An object the
 * libc loaded is known by the path it was opened with (which may differ from
 * its soname), a bare name came from the loaders' /usr/lib, and the program's
 * own file name is not something the loaders are told — "." stands for it
 * (glibc hands out a null pointer there and crashes if a caller looks). */
static void dl_origin_of(uint32_t idx, char *out) {
    cact_dl_object_t *o = &cact_dl_table.objs[idx];
    const char *name = o->name, *slash;
    size_t n;

    if (idx < CACT_DL_MAX_OBJECTS && dl_loaded[idx].path[0])
        name = dl_loaded[idx].path;
    if (!name || strcmp(name, "<main>") == 0) { out[0] = '.'; out[1] = '\0'; return; }
    slash = strrchr(name, '/');
    if (!slash) { strcpy(out, "/usr/lib"); return; }
    n = (size_t)(slash - name);
    if (n == 0) n = 1;                           /* "/foo.so" -> "/" */
    memcpy(out, name, n);
    out[n] = '\0';
}

int dlinfo(void *handle, int request, void *arg) {
    cact_dl_object_t *o;
    uint32_t idx;

    if (!arg) { dl_err("dlinfo: no result buffer"); return -1; }

    if (!handle || handle == (void *)1) {        /* RTLD_DEFAULT / dlopen(NULL) */
        for (idx = 0; idx < cact_dl_table.count; idx++)
            if (cact_dl_table.objs[idx].name) break;
        if (idx >= cact_dl_table.count) {
            dl_err("dlinfo: no object loaded");
            return -1;
        }
        o = &cact_dl_table.objs[idx];
    } else if (handle == (void *)-1) {           /* RTLD_NEXT */
        dl_err("dlinfo: RTLD_NEXT names no object");
        return -1;
    } else {
        o   = (cact_dl_object_t *)handle;
        idx = (uint32_t)(o - cact_dl_table.objs);
        if (idx >= CACT_DL_MAX_OBJECTS || !o->name) {
            dl_err("dlinfo: invalid handle");
            return -1;
        }
    }

    switch (request) {
    case RTLD_DI_LMID:
        *(Lmid_t *)arg = (Lmid_t)o->ns;          /* which namespace it was loaded into */
        return 0;
    case RTLD_DI_LINKMAP:
        dl_lms_build();
        *(struct link_map **)arg = &dl_lms[idx];
        return 0;
    case RTLD_DI_ORIGIN:
        dl_origin_of(idx, (char *)arg);
        return 0;
    case RTLD_DI_PHDR:
        *(const Elf32_Phdr **)arg = (const Elf32_Phdr *)o->phdr;
        return (int)o->phnum;   /* glibc returns the count here, not 0 */
    default:
        dl_err("unsupported dlinfo request");
        return -1;
    }
}

/* dlmopen(): load into a namespace.  A namespace here is a symbol scope, not a
 * second address space — every image is still mapped once, at its link-time
 * address, so a library can live in only one namespace (the loader refuses the
 * second copy by address, exactly as it refuses any colliding span).  What a
 * namespace does give is isolation: a lookup started inside one sees that
 * namespace's objects and nothing else, except the pinned objects (the program
 * and clibc.so), which no namespace can do without. */
void *dlmopen(Lmid_t nsid, const char *filename, int flags) {
    if (nsid == LM_ID_NEWLM) {
        uint32_t ns = dl_ns_new();
        if (ns == 0) { dl_err("dlmopen: no namespace left"); return 0; }
        {
            void *h = dl_open_into(filename, flags, ns);
            if (!h) dl_ns_release_if_empty(ns);     /* nothing landed: give it back */
            return h;
        }
    }
    if (nsid < 0 || !dl_ns_known((uint32_t)nsid)) {
        dl_err("dlmopen: unknown namespace");
        return 0;
    }
    return dl_open_into(filename, flags, (uint32_t)nsid);
}
