/* CactOS userspace dynamic linker bootstrap (ld.so).

 * Compiled as a freestanding fixed-address i386 executable, no libc.  It is
 * loaded by the kernel as the PT_INTERP interpreter; the kernel maps the main
 * image and ld.so itself but performs no relocations.  This component:
 *   1. parses auxv (AT_PHDR/AT_ENTRY/...),
 *   2. maps the shared libraries from DT_NEEDED into their fixed addresses,
 *   3. applies R_386_* relocations to libc and the main image (the same pass
 *      the kernel dynlink module used to do), then
 *   4. returns the main entry point to _start, which restores the original
 *      stack pointer and jumps there.
 *
 * CactOS policy: every image is mapped exactly at its link-time address, so
 * symbol values / relocation addends are already absolute and R_386_RELATIVE
 * is a no-op (B + A with bias 0). */

#include "ldso.h"

typedef struct {
    u32 phdr;
    u32 phent;
    u32 phnum;
    u32 pagesz;
    u32 base;
    u32 entry;
} auxv_t;

typedef struct {
    const char* name;      /* soname, "<main>" for the executable */
    u32         dyn;       /* PT_DYNAMIC vaddr (in memory) */
    u32         dynsz;     /* PT_DYNAMIC p_filesz */
    Elf32_Sym*  symtab;
    u32         symcount;
    char*       strtab;
    u32         strsz;
    /* PT_TLS (all zero when the object has no TLS) */
    u32         tls_vaddr, tls_filesz, tls_memsz, tls_align;
    u32         tls_off;   /* its offset in the static block (see tls_publish) */
    u32         tls_modid; /* its TLS module id (for general-dynamic relocations) */
    /* a copy of its program headers (the ELF header is not mapped, so they are
     * not readable after the fact) and the phdr count */
    u32         phdr, phnum;
    /* the span the PT_LOADs cover, so a later dlopen() can refuse to map over it */
    u32         base, size;
    /* The relocation sections, kept so the TLS pass can re-walk them once
     * every object is loaded and the block layout is known. */
    Elf32_Rel*  rel;  u32 rel_sz,  rel_ent;
    Elf32_Rela* rela; u32 rela_sz, rela_ent;
} ldobj_t;

/* Raw syscall shim.  ABI: EAX=num, EBX=a1, ESI=a2, EDI=a3 (SYSENTER). */
__attribute__((noinline))
long ldso_sc(unsigned num, unsigned a1, unsigned a2, unsigned a3)
{
    long ret;
    __asm__ volatile(
        "movl %%esp, %%ecx\n\t"
        "call 1f\n\t"
        "1:\n\t"
        "popl %%edx\n\t"
        "addl $(2f - 1b), %%edx\n\t"
        "sysenter\n\t"
        "2:\n\t"
        : "=a"(ret)
        : "a"(num), "b"(a1), "S"(a2), "D"(a3)
        : "ecx", "edx", "memory");
    return ret;
}

void ldso_die(void)
{
    for (;;) ldso_sc(SYS_EXIT, 1, 0, 0);
}

/* ── minimal memory / string helpers ─────────────────────────────────── */

static void ld_memcpy(void* dst, const void* src, u32 n)
{
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    while (n--) *d++ = *s++;
}

static void ld_memset(void* dst, int v, u32 n)
{
    u8* d = (u8*)dst;
    while (n--) *d++ = (u8)v;
}

static int ld_strlen(const char* s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int ld_strcmp(const char* a, const char* b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ── debug output (fd 1) ─────────────────────────────────────────────── */

static void ld_dbg(const char* s)
{
    int n = ld_strlen(s);
    ldso_sc(SYS_WRITE, 1, (unsigned)s, (unsigned)n);
}

/* ── object table ────────────────────────────────────────────────────── */

static ldobj_t g_objs[LDSO_MAX_OBJS];
static int     g_nobj;

/* Program headers, copied out of the file scratch buffer as each object is
 * loaded: a CactOS image's ELF header is not mapped (its first PT_LOAD starts
 * at file offset 0x1000), so dl_iterate_phdr has nothing else to point at. */
#define DL_MAX_PHDRS CACT_DL_MAX_PHDRS
static Elf32_Phdr g_phdrs[LDSO_MAX_OBJS * DL_MAX_PHDRS];

static ldobj_t* obj_find_by_name(const char* name)
{
    int i;
    for (i = 0; i < g_nobj; i++) {
        if (ld_strcmp(g_objs[i].name, name) == 0) return &g_objs[i];
    }
    return 0;
}

static u32 symcount_from_hash(u32 hash, u32 gnu_hash, Elf32_Sym* symtab)
{
    u32 n = 0;
    if (!symtab) return 0;
    if (hash) {
        n = ((u32*)hash)[1];                     /* SYSV: nchain */
        return n;
    }
    if (gnu_hash) {
        u32* gh = (u32*)gnu_hash;
        u32 nbuckets   = gh[0];
        u32 symoffset  = gh[1];
        u32 bloom_size = gh[2];
        if (bloom_size > 1024) bloom_size = 1024;
        if (nbuckets == 0) return 0;
        {
            u32* buckets = gh + 4 + bloom_size;
            u32* chains  = buckets + nbuckets;
            u32  max_b   = 0, i;
            for (i = 0; i < nbuckets; i++)
                if (buckets[i] > max_b) max_b = buckets[i];
            if (max_b < symoffset) return symoffset;
            {
                u32 ci = max_b - symoffset;
                u32 guard;
                for (guard = 0; guard < 8192; guard++, ci++) {
                    if (chains[ci] & 1u) return symoffset + ci + 1;
                }
            }
            return symoffset + 8192;
        }
    }
    return 4096;                                /* kernel fallback */
}

static int load_library(const char* name);

static u32 resolve_symbol(const char* name)
{
    int o;
    for (o = 0; o < g_nobj; o++) {
        ldobj_t* ob = &g_objs[o];
        u32 j;
        if (!ob->symtab || !ob->strtab) continue;
        for (j = 1; j < ob->symcount; j++) {
            Elf32_Sym* sym = &ob->symtab[j];
            const char* sn;
            int bind;
            if (sym->st_shndx == SHN_UNDEF) continue;
            bind = ELF32_ST_BIND(sym->st_info);
            if (bind != 1 /* STB_GLOBAL */ && bind != STB_WEAK) continue;
            if (sym->st_value == 0) continue;
            if (sym->st_name >= ob->strsz) continue;
            sn = ob->strtab + sym->st_name;
            if (ld_strcmp(sn, name) == 0) return sym->st_value;
        }
    }
    return 0;
}

/* ── relocation application (kernel dynlink_reloc.c logic) ───────────── */

static void apply_rel(ldobj_t* ob, Elf32_Rel* rel)
{
    u32  sym_idx  = ELF32_R_SYM(rel->r_info);
    u8   rel_type = (u8)ELF32_R_TYPE(rel->r_info);
    u32* target   = (u32*)rel->r_offset;
    u32  S = 0, A, P = rel->r_offset;

    if (sym_idx != 0 && ob->symtab && ob->strtab) {
        Elf32_Sym* sym;
        if (sym_idx >= ob->symcount) return;
        sym = &ob->symtab[sym_idx];
        if (sym->st_shndx == SHN_UNDEF) {
            const char* sname;
            if (sym->st_name >= ob->strsz) return;
            sname = ob->strtab + sym->st_name;
            S = resolve_symbol(sname);
        } else {
            S = sym->st_value;
        }
    }

    switch (rel_type) {
    case R_386_NONE:
        break;
    case R_386_32:            /* S + A */
        A = *target;
        *target = S + A;
        break;
    case R_386_PC32:          /* S + A - P */
        A = *target;
        *target = S + A - P;
        break;
    case R_386_GLOB_DAT:
    case R_386_JMP_SLOT:      /* S */
        *target = S;
        break;
    case R_386_RELATIVE:      /* B + A, B = 0 (fixed-address images) */
        break;
    case R_386_COPY:
        if (S && sym_idx < ob->symcount) {
            Elf32_Sym* sym = &ob->symtab[sym_idx];
            if (sym->st_size > 0 && sym->st_size <= 65536)
                ld_memcpy(target, (void*)S, sym->st_size);
        }
        break;
    default:
        break;
    }
}

static void apply_rela(ldobj_t* ob, Elf32_Rela* rela)
{
    u32  sym_idx  = ELF32_R_SYM(rela->r_info);
    u8   rel_type = (u8)ELF32_R_TYPE(rela->r_info);
    u32* target   = (u32*)rela->r_offset;
    u32  S = 0, A, P = rela->r_offset;

    if (sym_idx != 0 && ob->symtab && ob->strtab) {
        Elf32_Sym* sym;
        if (sym_idx >= ob->symcount) return;
        sym = &ob->symtab[sym_idx];
        if (sym->st_shndx == SHN_UNDEF) {
            const char* sname;
            if (sym->st_name >= ob->strsz) return;
            sname = ob->strtab + sym->st_name;
            S = resolve_symbol(sname);
        } else {
            S = sym->st_value;
        }
    }

    A = (u32)rela->r_addend;
    switch (rel_type) {
    case R_386_NONE:
        break;
    case R_386_32:
        *target = S + A;
        break;
    case R_386_PC32:
        *target = S + A - P;
        break;
    case R_386_GLOB_DAT:
    case R_386_JMP_SLOT:
        *target = S;
        break;
    case R_386_RELATIVE:
        break;
    case R_386_COPY:
        if (S && sym_idx < ob->symcount) {
            Elf32_Sym* sym = &ob->symtab[sym_idx];
            if (sym->st_size > 0 && sym->st_size <= 65536)
                ld_memcpy(target, (void*)S, sym->st_size);
        }
        break;
    default:
        break;
    }
}

/* Parse the object's .dynamic (in memory), register it, recursively load its
 * DT_NEEDED deps, then apply its own relocations (kernel ordering). */
static void process_object(ldobj_t* ob)
{
    Elf32_Dyn* dyn       = (Elf32_Dyn*)ob->dyn;
    u32        max_ent   = ob->dynsz / sizeof(Elf32_Dyn);
    Elf32_Sym* symtab    = 0;
    char*      strtab    = 0;
    u32        strsz     = 0;
    u32        hash      = 0;
    u32        gnu_hash  = 0;
    Elf32_Rel* rel       = 0;
    u32        rel_sz    = 0, rel_ent = 8;
    Elf32_Rela* rela     = 0;
    u32        rela_sz   = 0, rela_ent = 12;
    Elf32_Rel* jmprel    = 0;
    u32        jmprel_sz = 0;
    u32        pltrel    = DT_REL;
    u32        needed[LDSO_MAX_NEEDED];
    int        nneed = 0, i;

    for (i = 0; i < (int)max_ent; i++) {
        Elf32_Dyn* d = &dyn[i];
        if (d->d_tag == DT_NULL) break;
        switch (d->d_tag) {
        case DT_SYMTAB:  symtab = (Elf32_Sym*)d->d_un.d_ptr; break;
        case DT_STRTAB:  strtab = (char*)d->d_un.d_ptr;       break;
        case DT_STRSZ:   strsz  = d->d_un.d_val;              break;
        case DT_HASH:    hash   = d->d_un.d_ptr;              break;
        case DT_GNU_HASH: gnu_hash = d->d_un.d_ptr;           break;
        case DT_REL:     rel    = (Elf32_Rel*)d->d_un.d_ptr;  break;
        case DT_RELSZ:   rel_sz = d->d_un.d_val;              break;
        case DT_RELENT:  rel_ent = d->d_un.d_val;             break;
        case DT_RELA:    rela   = (Elf32_Rela*)d->d_un.d_ptr; break;
        case DT_RELASZ:  rela_sz = d->d_un.d_val;             break;
        case DT_RELAENT: rela_ent = d->d_un.d_val;            break;
        case DT_JMPREL:  jmprel = (Elf32_Rel*)d->d_un.d_ptr;  break;
        case DT_PLTRELSZ: jmprel_sz = d->d_un.d_val;          break;
        case DT_PLTREL:  pltrel = d->d_un.d_val;              break;
        case DT_NEEDED:
            if (nneed < LDSO_MAX_NEEDED) needed[nneed++] = d->d_un.d_val;
            break;
        default: break;
        }
    }

    ob->symtab    = symtab;
    ob->strtab    = strtab;
    ob->strsz     = strsz;
    ob->symcount  = symcount_from_hash(hash, gnu_hash, symtab);
    ob->rel       = rel;  ob->rel_sz  = rel_sz;  ob->rel_ent  = rel_ent;
    ob->rela      = rela; ob->rela_sz = rela_sz; ob->rela_ent = rela_ent;

    /* dependencies (their relocations run first, kernel order) */
    for (i = 0; i < nneed; i++) {
        const char* name;
        if (!strtab || needed[i] >= strsz) continue;
        name = strtab + needed[i];
        if (!obj_find_by_name(name)) load_library(name);
    }

    if (rel && rel_sz && rel_ent) {
        u32 n = rel_sz / rel_ent;
        for (i = 0; i < (int)n; i++)
            apply_rel(ob, (Elf32_Rel*)((u8*)rel + (u32)i * rel_ent));
    }
    if (rela && rela_sz && rela_ent) {
        u32 n = rela_sz / rela_ent;
        for (i = 0; i < (int)n; i++)
            apply_rela(ob, (Elf32_Rela*)((u8*)rela + (u32)i * rela_ent));
    }
    if (jmprel && jmprel_sz) {
        if (pltrel == DT_RELA) {
            u32 n = jmprel_sz / sizeof(Elf32_Rela);
            for (i = 0; i < (int)n; i++)
                apply_rela(ob, (Elf32_Rela*)((u8*)jmprel + (u32)i * sizeof(Elf32_Rela)));
        } else {
            u32 n = jmprel_sz / sizeof(Elf32_Rel);
            for (i = 0; i < (int)n; i++)
                apply_rel(ob, (Elf32_Rel*)((u8*)jmprel + (u32)i * sizeof(Elf32_Rel)));
        }
    }
}

/* ── file loading helpers ────────────────────────────────────────────── */

static int read_file_full(const char* path, u8* buf, u32 cap)
{
    int fd = (int)ldso_sc(SYS_OPEN, (unsigned)path, 0, 0);
    u32  off = 0;
    if (fd < 0) return -1;
    for (;;) {
        long n;
        if (off >= cap) break;
        n = ldso_sc(SYS_READ, (unsigned)fd, (unsigned)(buf + off), cap - off);
        if (n <= 0) break;
        off += (u32)n;
    }
    ldso_sc(SYS_CLOSE, (unsigned)fd, 0, 0);
    return (off >= cap) ? -2 : (int)off;
}

static u32 map_segment(u32 vaddr, u32 memsz)
{
    u32 start = vaddr & ~0xFFFu;
    u32 end   = (vaddr + memsz + 0xFFFu) & ~0xFFFu;
    long r;
    if (end <= start) return 0;
    r = ldso_sc(SYS_MMAP, (unsigned)&(u32[]){start, end - start,
                PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_PRIVATE | MAP_FIXED | MAP_ANON, (unsigned)-1, 0}, 0, 0);
    return (r == (long)-1) ? 0 : start;
}

/* Map libc/libfoo.so at its fixed link-time addresses; returns 0 on failure. */
static int load_library(const char* name)
{
    char path[SO_PATH_MAX];
    int  len;
    u8*  buf;
    u32  cap, total;
    u32* hdr;
    u16  phnum;
    u32  phoff, phentsz;
    int  i;
    Elf32_Dyn* dyn = 0;
    u32        dynsz = 0;
    u32        tls_vaddr = 0, tls_filesz = 0, tls_memsz = 0, tls_align = 4;
    u32        span_lo = 0, span_hi = 0;

    if (g_nobj >= LDSO_MAX_OBJS) return -1;

    len = ld_strlen(name);
    if (len + 10 > SO_PATH_MAX) return -1;
    ld_memcpy(path, "/usr/lib/", 9);
    ld_memcpy(path + 9, name, (u32)len + 1);

    cap = LDSO_SCRATCH;
    buf = (u8*)ldso_sc(SYS_MMAP, (unsigned)&(u32[]){0, cap,
                  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON,
                  (unsigned)-1, 0}, 0, 0);
    if (buf == (u8*)-1) return -1;

    total = (u32)read_file_full(path, buf, cap);
    if ((int)total <= 0) {
        ldso_sc(SYS_MUNMAP, (unsigned)buf, cap, 0);
        ld_dbg("[ld.so] cannot read "); ld_dbg(path); ld_dbg("\n");
        return -1;
    }

    hdr = (u32*)buf;
    if (hdr[0] != 0x464C457Fu) {            /* \x7fELF */
        ldso_sc(SYS_MUNMAP, (unsigned)buf, cap, 0);
        ld_dbg("[ld.so] bad magic: "); ld_dbg(path); ld_dbg("\n");
        return -1;
    }
    phnum   = *(u16*)(buf + 0x2C);
    phoff   = *(u32*)(buf + 0x1C);
    phentsz = *(u16*)(buf + 0x2A);

    if (phentsz < sizeof(Elf32_Phdr) || phnum == 0 ||
        phoff + (u32)phnum * phentsz > total) {
        ldso_sc(SYS_MUNMAP, (unsigned)buf, cap, 0);
        return -1;
    }

    /* two passes: map PT_LOAD, remember PT_DYNAMIC */
    for (i = 0; i < phnum; i++) {
        Elf32_Phdr* ph = (Elf32_Phdr*)(buf + phoff + (u32)i * phentsz);
        if (ph->p_type == PT_TLS) {
            tls_vaddr = ph->p_vaddr;
            tls_filesz = ph->p_filesz;
            tls_memsz  = ph->p_memsz;
            tls_align  = ph->p_align;
            continue;
        }
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
        {
            u32 lo = ph->p_vaddr & ~0xFFFu;
            u32 hi = (ph->p_vaddr + ph->p_memsz + 0xFFFu) & ~0xFFFu;
            if (!span_lo || lo < span_lo) span_lo = lo;
            if (hi > span_hi) span_hi = hi;
        }
        if (!map_segment(ph->p_vaddr, ph->p_memsz)) {
            ldso_sc(SYS_MUNMAP, (unsigned)buf, cap, 0);
            ld_dbg("[ld.so] mmap segment failed: "); ld_dbg(path); ld_dbg("\n");
            return -1;
        }
    }
    for (i = 0; i < phnum; i++) {
        Elf32_Phdr* ph = (Elf32_Phdr*)(buf + phoff + (u32)i * phentsz);
        if (ph->p_type == PT_DYNAMIC) { dyn = (Elf32_Dyn*)ph->p_vaddr; dynsz = ph->p_filesz; break; }
    }

    /* copy segment file bytes into the fresh zeroed mappings */
    for (i = 0; i < phnum; i++) {
        Elf32_Phdr* ph = (Elf32_Phdr*)(buf + phoff + (u32)i * phentsz);
        u32 seg_va;
        if (ph->p_type != PT_LOAD || ph->p_filesz == 0) continue;
        if (ph->p_offset + ph->p_filesz > total) continue;
        seg_va = ph->p_vaddr & ~0xFFFu;
        ld_memcpy((void*)(seg_va + (ph->p_vaddr - seg_va)),
                  buf + ph->p_offset, ph->p_filesz);
    }

    /* Keep a copy of the program headers while the scratch buffer is still
     * there: the ELF header of a CactOS image is not mapped (its first PT_LOAD
     * starts at file offset 0x1000), so dl_iterate_phdr has nothing else to
     * point a caller at.  The slot is the index this object is about to take. */
    if (g_nobj < LDSO_MAX_OBJS) {
        u32 n = phnum < DL_MAX_PHDRS ? phnum : DL_MAX_PHDRS;
        u32 slot = (u32)g_nobj;
        for (i = 0; i < (int)n; i++)
            ld_memcpy(&g_phdrs[slot * DL_MAX_PHDRS + i],
                      buf + phoff + (u32)i * phentsz, sizeof(Elf32_Phdr));
    }

    ldso_sc(SYS_MUNMAP, (unsigned)buf, cap, 0);

    if (g_nobj < LDSO_MAX_OBJS) {
        ldobj_t* ob = &g_objs[g_nobj++];
        ob->name   = name;
        ob->dyn    = (u32)dyn;
        ob->dynsz  = dynsz;
        ob->symtab = 0; ob->strtab = 0; ob->strsz = 0; ob->symcount = 0;
        ob->rel = 0; ob->rel_sz = 0; ob->rel_ent = 0;
        ob->rela = 0; ob->rela_sz = 0; ob->rela_ent = 0;
        ob->tls_vaddr = tls_vaddr; ob->tls_filesz = tls_filesz;
        ob->tls_memsz = tls_memsz; ob->tls_align  = tls_align;
        ob->tls_off   = 0;
        ob->base      = span_lo;
        ob->size      = span_hi - span_lo;
        ob->phdr      = (u32)&g_phdrs[(u32)(g_nobj - 1) * DL_MAX_PHDRS];
        ob->phnum     = phnum < DL_MAX_PHDRS ? phnum : DL_MAX_PHDRS;
        process_object(ob);
        return 0;
    }
    return -1;
}

/* ── auxv parsing ────────────────────────────────────────────────────── */

static int parse_auxv(u32* stackp, auxv_t* av)
{
    u32* argv = (u32*)stackp[1];   /* CactOS exec stack: argc, argv_arr, envp_arr */
    u32* envp = (u32*)stackp[2];
    u32* aux;
    int  i = 0;

    ld_memset(av, 0, sizeof(*av));

    if (envp) {
        while (envp[i]) i++;
        aux = envp + i + 1;
    } else if (argv) {
        while (argv[i]) i++;
        aux = argv + i + 1;
    } else {
        return -1;
    }

    while (aux[0] != AT_NULL) {
        switch (aux[0]) {
        case AT_PHDR:   av->phdr   = aux[1]; break;
        case AT_PHENT:  av->phent  = aux[1]; break;
        case AT_PHNUM:  av->phnum  = aux[1]; break;
        case AT_PAGESZ: av->pagesz = aux[1]; break;
        case AT_BASE:   av->base   = aux[1]; break;
        case AT_ENTRY:  av->entry  = aux[1]; break;
        default: break;
        }
        aux += 2;
    }
    return (av->phdr && av->phnum && av->phent) ? 0 : -1;
}

/* ── static TLS ──────────────────────────────────────────────────────────── */

static u32 g_tls_total;

/* One module per object that carries a PT_TLS, in load order, and hand the
 * resulting layout to the libc — the libc cannot see the program headers, and
 * it is the one that builds each thread's block (src/tls_static.c). */
static void tls_publish(void)
{
    cact_tls_layout_t lay;
    int i, n = 0;
    u32 addr;

    ld_memset(&lay, 0, sizeof(lay));
    for (i = 0; i < g_nobj && n < CACT_TLS_MAX_MODULES; i++) {
        ldobj_t* ob = &g_objs[i];
        ob->tls_off = 0;
        ob->tls_modid = 0;
        if (!ob->tls_memsz) continue;
        lay.mods[n].off    = 0;
        lay.mods[n].vaddr  = ob->tls_vaddr;
        lay.mods[n].filesz = ob->tls_filesz;
        lay.mods[n].memsz  = ob->tls_memsz;
        lay.mods[n].align  = ob->tls_align;
        n++;
    }
    /* Room for the objects that will show up later: a dlopen()'d plugin's own
     * __thread has to land inside every thread's block, and the block is made
     * once, before _start.  It is published even when nothing here uses
     * __thread, so a program with none of its own still has %gs pointing at a
     * block the moment it loads something that does. */
    g_tls_total = cact_tls_plan_reserved(&lay, (u32)n, CACT_TLS_RESERVE);

    /* the same walk, so each object learns its own offset and module id */
    n = 0;
    for (i = 0; i < g_nobj && n < CACT_TLS_MAX_MODULES; i++) {
        if (!g_objs[i].tls_memsz) continue;
        g_objs[i].tls_off   = lay.mods[n].off;
        g_objs[i].tls_modid = lay.mods[n].id;
        n++;
    }

    addr = resolve_symbol("cact_tls_layout");
    if (addr) ld_memcpy((void*)addr, &lay, sizeof(lay));
    else ld_dbg("[ld.so] libc has no cact_tls_layout\n");
}

/* Resolve what the linker left: after linking, static TLS access to a variable
 * is an R_386_TLS_TPOFF against a symbol whose st_value is its offset within
 * its module's TLS image, and the displacement to store is the distance from
 * the thread pointer (the TCB, which sits just past the block) down to the
 * variable.  GNU ld reduces both the program's own local-exec access and a
 * library's initial-exec access to this one relocation, so there is a single
 * case here.  General-dynamic is the pair R_386_TLS_DTPMOD32/_DTPOFF32 (the
 * module id and the offset ___tls_get_addr() adds), which only a shared object
 * that is not initial-exec emits; the rest name a model this ld.so does not
 * implement, so they are reported rather than silently left unresolved. */
static void tls_relocate(void)
{
    int i;

    for (i = 0; i < g_nobj; i++) {
        ldobj_t* ob = &g_objs[i];
        u32 j, n;

        if (ob->rel && ob->rel_sz && ob->rel_ent) {
            n = ob->rel_sz / ob->rel_ent;
            for (j = 0; j < n; j++) {
                Elf32_Rel* r = (Elf32_Rel*)((u8*)ob->rel + j * ob->rel_ent);
                u8 t = (u8)ELF32_R_TYPE(r->r_info);
                u32 si = ELF32_R_SYM(r->r_info);
                u32* target = (u32*)r->r_offset;
                u32 S = 0;

                if (t == R_386_TLS_TPOFF || t == R_386_TLS_TPOFF32) {
                    if (si && si < ob->symcount) S = ob->symtab[si].st_value;
                    *target = ob->tls_off + S + *target - g_tls_total;
                    continue;
                }
                if (t == R_386_TLS_DTPMOD32) {
                    /* General-dynamic's module-id half: the defining object's
                     * TLS id.  The program is -fPIE (local-exec) and no startup
                     * library carries __thread, so this only matters to an
                     * object that is neither — the same "assume it is the
                     * object's own symbol" rule the TPOFF case above uses. */
                    if (!ob->tls_memsz) {
                        ld_dbg("[ld.so] unsupported TLS relocation\n");
                        continue;
                    }
                    *target = ob->tls_modid + *target;
                    continue;
                }
                if (t == R_386_TLS_DTPOFF32) {
                    if (si && si < ob->symcount) S = ob->symtab[si].st_value;
                    *target = S + *target;
                    continue;
                }
                if (t >= R_386_TLS_TPOFF && t <= R_386_TLS_TPOFF32)
                    ld_dbg("[ld.so] unsupported TLS relocation\n");
            }
        }
        if (ob->rela && ob->rela_sz && ob->rela_ent) {
            n = ob->rela_sz / ob->rela_ent;
            for (j = 0; j < n; j++) {
                Elf32_Rela* r = (Elf32_Rela*)((u8*)ob->rela + j * ob->rela_ent);
                u8 t = (u8)ELF32_R_TYPE(r->r_info);
                u32 si = ELF32_R_SYM(r->r_info);
                u32* target = (u32*)r->r_offset;
                u32 S = 0;

                if (t == R_386_TLS_TPOFF || t == R_386_TLS_TPOFF32) {
                    if (si && si < ob->symcount) S = ob->symtab[si].st_value;
                    *target = ob->tls_off + S + (u32)r->r_addend - g_tls_total;
                    continue;
                }
                if (t == R_386_TLS_DTPMOD32) {
                    if (!ob->tls_memsz) {
                        ld_dbg("[ld.so] unsupported TLS relocation\n");
                        continue;
                    }
                    *target = ob->tls_modid + (u32)r->r_addend;
                    continue;
                }
                if (t == R_386_TLS_DTPOFF32) {
                    if (si && si < ob->symcount) S = ob->symtab[si].st_value;
                    *target = S + (u32)r->r_addend;
                    continue;
                }
                if (t >= R_386_TLS_TPOFF && t <= R_386_TLS_TPOFF32)
                    ld_dbg("[ld.so] unsupported TLS relocation\n");
            }
        }
    }
}

/* Hand the libc the list of objects already mapped, so dlopen() can reuse them
 * (clibc.so above all: every plugin's DT_NEEDED names it) and can refuse to map
 * a new object over one of them.  Pinned: dlclose() never unloads these. */
static void dl_publish(void)
{
    cact_dl_table_t t;
    u32 addr = resolve_symbol("cact_dl_table");
    int i;

    if (!addr) {
        ld_dbg("[ld.so] libc has no cact_dl_table\n");
        return;
    }
    t.count = 0;
    for (i = 0; i < g_nobj && t.count < CACT_DL_MAX_OBJECTS; i++) {
        ldobj_t* ob = &g_objs[i];
        cact_dl_object_t* o = &t.objs[t.count];
        o->name     = ob->name;
        o->dyn      = ob->dyn;
        o->dynsz    = ob->dynsz;
        o->symtab   = (u32)ob->symtab;
        o->strtab   = (u32)ob->strtab;
        o->symcount = ob->symcount;
        o->strsz    = ob->strsz;
        o->phdr     = ob->phdr;
        o->phnum    = ob->phnum;
        o->base     = ob->base;
        o->size     = ob->size;
        o->flags    = CACT_DL_PINNED;
        o->refs     = 1;
        o->tls_off  = ob->tls_memsz ? ob->tls_off : CACT_DL_NO_TLS;
        o->tls_modid = ob->tls_memsz ? ob->tls_modid : 0;
        t.count++;
    }
    ld_memcpy((void*)addr, &t, sizeof(t));
}

/* ── entry ───────────────────────────────────────────────────────────── */

u32 ldso_entry(u32* stackp)
{
    auxv_t   av;
    Elf32_Phdr* ph;
    u32      main_dyn = 0, main_dynsz = 0;
    u32      main_tls_vaddr = 0, main_tls_filesz = 0, main_tls_memsz = 0, main_tls_align = 4;
    u32      main_lo = 0, main_hi = 0;
    ldobj_t* mainobj;
    u32      i;

    if (parse_auxv(stackp, &av) != 0) {
        ld_dbg("[ld.so] no auxv\n");
        return 0;
    }

    /* find main's PT_DYNAMIC (and PT_TLS) via the mapped phdr table */
    for (i = 0; i < av.phnum; i++) {
        ph = (Elf32_Phdr*)(av.phdr + i * av.phent);
        if (ph->p_type == PT_DYNAMIC) {
            main_dyn   = ph->p_vaddr;
            main_dynsz = ph->p_filesz;
        } else if (ph->p_type == PT_TLS) {
            main_tls_vaddr  = ph->p_vaddr;
            main_tls_filesz = ph->p_filesz;
            main_tls_memsz  = ph->p_memsz;
            main_tls_align  = ph->p_align;
        } else if (ph->p_type == PT_LOAD && ph->p_memsz) {
            u32 lo = ph->p_vaddr & ~0xFFFu;
            u32 hi = (ph->p_vaddr + ph->p_memsz + 0xFFFu) & ~0xFFFu;
            if (!main_lo || lo < main_lo) main_lo = lo;
            if (hi > main_hi) main_hi = hi;
        }
    }
    if (!main_dyn) {
        ld_dbg("[ld.so] no PT_DYNAMIC in main\n");
        return 0;
    }

    mainobj = &g_objs[g_nobj++];
    mainobj->name   = "<main>";
    mainobj->dyn    = main_dyn;
    mainobj->dynsz  = main_dynsz;
    mainobj->symtab = 0; mainobj->strtab = 0; mainobj->strsz = 0; mainobj->symcount = 0;
    mainobj->rel = 0; mainobj->rel_sz = 0; mainobj->rel_ent = 0;
    mainobj->rela = 0; mainobj->rela_sz = 0; mainobj->rela_ent = 0;
    mainobj->tls_vaddr = main_tls_vaddr; mainobj->tls_filesz = main_tls_filesz;
    mainobj->tls_memsz = main_tls_memsz; mainobj->tls_align  = main_tls_align;
    mainobj->tls_off   = 0;
    mainobj->base      = main_lo;
    mainobj->size      = main_hi - main_lo;
    /* The main's phdrs *are* mapped (AT_PHDR), so copy them out of there. */
    {
        u32 n = av.phnum < DL_MAX_PHDRS ? av.phnum : DL_MAX_PHDRS;
        for (i = 0; i < n; i++)
            ld_memcpy(&g_phdrs[i], (void*)(av.phdr + i * av.phent), sizeof(Elf32_Phdr));
        mainobj->phdr  = (u32)&g_phdrs[0];
        mainobj->phnum = n;
    }

    process_object(mainobj);

    /* Every object is loaded now, so the static TLS block can be laid out and
     * the TLS relocations (which depend on its total size) resolved.  Both are
     * no-ops when nothing in the program uses __thread. */
    tls_publish();
    tls_relocate();

    /* What dlopen() has to know about the image it is starting inside. */
    dl_publish();

    return av.entry;
}
