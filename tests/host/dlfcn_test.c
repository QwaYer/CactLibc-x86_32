/* dlfcn host test: the real loader, exercised on a real plugin image.
 *
 * The unit is mapped with the actual src/dlfcn.c, against fixtures built for
 * i686 by the harness (a plugin, one that cannot resolve a symbol, and one
 * deliberately linked on top of clibc.so).  The plugin cannot be *called* here
 * — it is i686 code — so every check reads the mapped memory: the copied .data
 * image, the addresses the relocation wrote, and the initialiser's effect.
 *
 * The addresses are real because CactOS images are linked at (and mapped to)
 * fixed addresses that happen to be free in this 64-bit process. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* The libc's own headers first, by path: the test main is compiled with the
 * *system* headers, so these are taken directly and every prototype below uses
 * the types they define. */
#include "../../include/dlfcn.h"
#include "../../include/link.h"
#include "../../include/dl_objects.h"
#include "../../include/dl_elf.h"
#include "../../include/tls_layout.h"

/* The unit's entry points, renamed (see rename_dlfcn.h). */
void *cact_dlopen(const char *filename, int flags);
void *cact_dlsym(void *handle, const char *symbol);
void *cact_dlvsym(void *handle, const char *symbol, const char *version);
int   cact_dlclose(void *handle);
char *cact_dlerror(void);
int   cact_dladdr(const void *addr, Dl_info *info);
int   cact_dlinfo(void *handle, int request, void *arg);
void *cact_dlmopen(long nsid, const char *filename, int flags);
int   cact_dl_iterate_phdr(int (*cb)(struct dl_phdr_info *, size_t, void *), void *data);

#define RTLD_LAZY   1
#define RTLD_NOW    2
#define RTLD_GLOBAL 0x100
#define RTLD_DEFAULT ((void *)0)

static int checks, fails;

#define CK(cond, ...) do { \
    checks++; \
    if (!(cond)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void *at(uint32_t va) { return (void *)(uintptr_t)va; }

/* dl_iterate_phdr callbacks, at file scope because the loader takes a plain
 * function pointer. */
static int g_total, g_saw_plugin, g_saw_libc, g_bad_phdr, g_bad_size, g_seen;

static int probe_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void)data;
    g_total++;
    if (size != sizeof(*info)) g_bad_size++;
    if (!info->dlpi_phdr || info->dlpi_phnum == 0) g_bad_phdr++;
    if (info->dlpi_name && strstr(info->dlpi_name, "plugin.so")) g_saw_plugin = 1;
    if (info->dlpi_name && strcmp(info->dlpi_name, "clibc.so") == 0) g_saw_libc = 1;
    /* the table has to be readable: walk it as a caller would */
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_memsz > 0x40000000u) g_bad_phdr++;
    }
    return 0;
}

static int stop_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void)info; (void)size; (void)data;
    g_seen++;
    return 1;                                   /* "stop, and pass 1 back" */
}

/* Stands in for the program's own program headers: on a real boot ld.so
 * publishes the main image as the first table entry. */
static Elf32_Phdr fake_phdrs[2];

int main(void) {
    const char *libc    = getenv("CACT_TEST_LIBC_SO");
    const char *plugin  = getenv("CACT_TEST_PLUGIN_SO");
    const char *undef   = getenv("CACT_TEST_PLUGIN_UNDEF_SO");
    const char *collide = getenv("CACT_TEST_PLUGIN_COLLIDE_SO");
    const char *tlsp    = getenv("CACT_TEST_PLUGIN_TLS_SO");
    const char *gdtlsp  = getenv("CACT_TEST_PLUGIN_GD_SO");
    const char *iso_plugin = getenv("CACT_TEST_PLUGIN_ISO_SO");

    if (!libc || !plugin || !undef || !collide || !tlsp || !gdtlsp || !iso_plugin) {
        printf("dlfcn: fixtures not in the environment — skipping\n");
        return 0;                                  /* the harness skips cleanly */
    }

    /* Stand in for the program itself, as ld.so would publish it: the *first*
     * table entry, pinned, spanning this binary's code.  RTLD_NEXT resolves
     * from the caller's object, and clibc.so/plugin must come after it. */
    {
        unsigned long lo = (unsigned long)&main;
        if ((unsigned long)&stop_cb  < lo) lo = (unsigned long)&stop_cb;
        if ((unsigned long)&probe_cb < lo) lo = (unsigned long)&probe_cb;

        memset(&cact_dl_table, 0, sizeof cact_dl_table);
        memset(&fake_phdrs, 0, sizeof fake_phdrs);
        fake_phdrs[0].p_type = 1;                  /* PT_LOAD */
        fake_phdrs[0].p_memsz = 0x1000;

        cact_dl_table.count = 1;
        cact_dl_table.objs[0].name   = "<main>";
        cact_dl_table.objs[0].base   = lo & ~0xFFFFul;
        cact_dl_table.objs[0].size   = 0x100000ul; /* the text plus the PLT */
        cact_dl_table.objs[0].phdr   = (unsigned long)fake_phdrs;
        cact_dl_table.objs[0].phnum  = 2;
        cact_dl_table.objs[0].flags  = 1;          /* CACT_DL_PINNED */
        cact_dl_table.objs[0].refs   = 1;
    }

    /* A missing file, and a file that is not ELF, must both report an error. */
    CK(cact_dlopen("/nonexistent/plugin.so", RTLD_NOW) == 0, "a missing file loaded");
    CK(cact_dlerror() != 0, "no error text for a missing file");
    CK(cact_dlerror() == 0, "dlerror did not clear itself");
    CK(cact_dlopen("/etc/hostname", RTLD_NOW) == 0, "a non-ELF file loaded");
    CK(cact_dlerror() != 0, "no error text for a non-ELF file");

    /* clibc.so, then the plugin that depends on it. */
    void *h_libc = cact_dlopen(libc, RTLD_NOW | RTLD_GLOBAL);
    CK(h_libc != 0, "clibc.so did not load: %s", cact_dlerror() ? cact_dlerror() : "?");

    /* On a real boot ld.so does not dlopen() clibc.so — it publishes it as one
     * of the *pinned* objects, which every namespace then shares.  Mark it the
     * same way here so a namespace sees clibc.so instead of trying (and failing)
     * to map a second copy of it. */
    if (h_libc) ((cact_dl_object_t *)h_libc)->flags |= 1;   /* CACT_DL_PINNED */

    void *h_plug = cact_dlopen(plugin, RTLD_NOW);
    CK(h_plug != 0, "the plugin did not load: %s", cact_dlerror() ? cact_dlerror() : "?");
    if (!h_libc || !h_plug) { printf("dlfcn: cannot continue\n"); return 1; }

    /* dlsym returns addresses inside the plugin's fixed image ... */
    void *add  = cact_dlsym(h_plug, "plugin_add");
    void *data = cact_dlsym(h_plug, "plugin_data");
    CK(add != 0, "plugin_add not found");
    CK(data != 0, "plugin_data not found");
    CK(((uint32_t)(uintptr_t)add  >> 24) == 0x0C, "plugin_add is not in the image: %p", add);

    /* ... and the memory there proves it: the .data image was copied and the
     * initialiser ran. */
    CK(*(int *)data == 0x1234, "plugin_data = %#x, the image was not copied", *(int *)data);
    {
        int *inited = (int *)cact_dlsym(h_plug, "plugin_inited");
        CK(inited != 0, "plugin_inited not found");
        if (inited) CK(*inited == 1, "the plugin's initialiser did not run");
    }

    /* Inter-object relocation: the plugin's two pointer initialisers must hold
     * clibc.so's addresses, resolved through the same table dlsym uses. */
    {
        void *strlen_expect = cact_dlsym(h_libc, "strlen");
        void *tzname_expect = cact_dlsym(h_libc, "tzname");
        void *sp = cact_dlsym(h_plug, "plugin_strlen_ptr");
        void *tp = cact_dlsym(h_plug, "plugin_tzname_ptr");
        CK(strlen_expect != 0 && tzname_expect != 0, "clibc.so symbols not found by dlsym");
        CK(sp && tp, "the plugin's pointer slots not found");
        if (sp && tp) {
            uint32_t got_s = *(uint32_t *)sp, got_t = *(uint32_t *)tp;
            CK(got_s == (uint32_t)(uintptr_t)strlen_expect,
               "strlen relocated to %#x, want %#x", got_s, (uint32_t)(uintptr_t)strlen_expect);
            CK(got_t == (uint32_t)(uintptr_t)tzname_expect,
               "tzname relocated to %#x, want %#x", got_t, (uint32_t)(uintptr_t)tzname_expect);
        }
    }

    /* A symbol that is not there, and RTLD_NEXT from the program itself. */
    CK(cact_dlsym(h_plug, "no_such_symbol") == 0, "dlsym found a symbol that is not there");
    CK(cact_dlerror() != 0, "no error text for a missing symbol");
    CK(cact_dlsym((void *)-1, "strlen") == cact_dlsym(h_libc, "strlen"),
       "RTLD_NEXT from the program did not reach clibc.so");
    CK(cact_dlsym((void *)-1, "plugin_add") == 0,
       "RTLD_NEXT returned a symbol from a RTLD_LOCAL plugin");
    CK(cact_dlsym((void *)-1, "no_such_symbol") == 0, "RTLD_NEXT found a symbol that is not there");

    /* RTLD_DEFAULT sees the pinned objects, not a local plugin. */
    CK(cact_dlsym(RTLD_DEFAULT, "strlen") != 0, "RTLD_DEFAULT cannot see clibc.so");
    CK(cact_dlsym(RTLD_DEFAULT, "plugin_add") == 0, "RTLD_DEFAULT sees a local plugin");

    /* The same file twice is the same object. */
    CK(cact_dlopen(plugin, RTLD_NOW) == h_plug, "a second dlopen made a second copy");

    /* A plugin linked where clibc.so already lives is refused, not mapped over. */
    CK(cact_dlopen(collide, RTLD_NOW) == 0, "a colliding plugin was mapped");
    CK(cact_dlerror() != 0, "no error text for a colliding plugin");

    /* A plugin that needs a symbol nobody defines must not load. */
    CK(cact_dlopen(undef, RTLD_NOW) == 0, "a plugin with an undefined symbol loaded");

    /* ── dlvsym / dladdr / dl_iterate_phdr ───────────────────────────────── */
    /* Both objects are still loaded here; the closing checks come last. */

    void *add2 = cact_dlsym(h_plug, "plugin_add");
    CK(cact_dlvsym(h_plug, "plugin_add", "ANY") == add2, "dlvsym disagreed with dlsym");
    CK(cact_dlvsym(h_plug, "no_such_symbol", "1.0") == 0,
       "dlvsym found a symbol that is not there");

    {
        Dl_info di;
        memset(&di, 0, sizeof di);
        int r = cact_dladdr(add2, &di);
        CK(r == 1, "dladdr did not recognise the plugin's own address");
        CK(di.dli_fname && strstr(di.dli_fname, "plugin.so"),
           "dladdr named the object \"%s\"", di.dli_fname ? di.dli_fname : "(null)");
        CK(di.dli_fbase == (void *)(uintptr_t)0x0C000000,
           "dladdr base is %p", di.dli_fbase);
        CK(di.dli_sname && strcmp(di.dli_sname, "plugin_add") == 0,
           "dladdr named the symbol \"%s\"", di.dli_sname ? di.dli_sname : "(null)");
        CK(di.dli_saddr == add2, "dladdr symbol address %p, want %p", di.dli_saddr, add2);

        /* A library symbol, and an address in no object at all. */
        memset(&di, 0, sizeof di);
        CK(cact_dladdr(cact_dlsym(h_libc, "strlen"), &di) == 1, "dladdr missed clibc.so");
        CK(di.dli_fname && strcmp(di.dli_fname, "clibc.so") == 0,
           "dladdr named the library \"%s\"", di.dli_fname ? di.dli_fname : "(null)");
        CK(di.dli_sname && strcmp(di.dli_sname, "strlen") == 0,
           "dladdr named the library symbol \"%s\"", di.dli_sname ? di.dli_sname : "(null)");

        memset(&di, 0, sizeof di);
        CK(cact_dladdr((void *)(uintptr_t)0x00100000, &di) == 0,
           "dladdr claimed an address in no object");
    }

    {
        int r = cact_dl_iterate_phdr(probe_cb, 0);
        CK(r == 0, "dl_iterate_phdr returned %d", r);
        CK(g_total >= 2, "dl_iterate_phdr saw only %d objects", g_total);
        CK(g_saw_plugin, "dl_iterate_phdr never saw the plugin");
        CK(g_saw_libc, "dl_iterate_phdr never saw clibc.so");
        CK(g_bad_phdr == 0, "%d objects had no program headers", g_bad_phdr);
        CK(g_bad_size == 0, "%d callbacks got a bad size", g_bad_size);
    }
    /* A callback that returns nonzero stops the walk, and its value is passed on. */
    CK(cact_dl_iterate_phdr(stop_cb, 0) == 1, "a stopping callback did not stop the walk");
    CK(g_seen == 1, "the walk did not stop at the first object (%d seen)", g_seen);

    /* ── static TLS: a plugin with a __thread of its own ────────────────────
     *
     * LD.SO + _START STAND-IN.  A real process gets its static TLS layout from
     * ld.so and one block per thread from the libc's _start; here the test
     * makes the same pair itself: an empty plan with the reserve the layout
     * always keeps, then the main thread's block.  The plugin loaded below must
     * then take a slice of that reserve.
     *
     * The plugin is built for the initial-exec model, so GNU ld left it an
     * R_386_TLS_TPOFF against a GOT slot holding the displacement from the
     * thread pointer — the same relocation ld.so resolves for the program
     * itself, here resolved against a module that did not exist when the block
     * was laid out. */
    {
        memset(&cact_tls_layout, 0, sizeof cact_tls_layout);
        cact_tls_plan(&cact_tls_layout, 0);
        uint32_t total = cact_tls_reserve(&cact_tls_layout, CACT_TLS_RESERVE);
        unsigned char *tp = (unsigned char *)cact_tls_block_new();

        CK(total == CACT_TLS_RESERVE && cact_tls_layout.spare == CACT_TLS_RESERVE,
           "the empty layout has total=%u spare=%u", total, cact_tls_layout.spare);
        CK(tp != 0, "no static TLS block for this thread");

        void *h_tls = cact_dlopen(tlsp, RTLD_NOW);
        CK(h_tls != 0, "the __thread plugin did not load: %s",
           cact_dlerror() ? cact_dlerror() : "?");

        if (h_tls && tp) {
            cact_dl_object_t *o = (cact_dl_object_t *)h_tls;
            uint32_t off = o->tls_off, i, tls_vaddr = 0, tls_filesz = 0, tls_memsz = 0;
            Elf32_Phdr *ph = (Elf32_Phdr *)(uintptr_t)o->phdr;

            CK(off != CACT_DL_NO_TLS, "the plugin's __thread got no block offset");
            CK(cact_tls_layout.count == 1, "the layout holds %u modules, want 1",
               cact_tls_layout.count);

            /* The module the loader added describes the plugin's own PT_TLS. */
            for (i = 0; i < o->phnum; i++)
                if (ph[i].p_type == PT_TLS) {
                    tls_vaddr  = ph[i].p_vaddr;
                    tls_filesz = ph[i].p_filesz;
                    tls_memsz  = ph[i].p_memsz;
                }
            CK(tls_memsz != 0, "the plugin has no PT_TLS");
            CK(cact_tls_layout.mods[0].vaddr  == tls_vaddr &&
               cact_tls_layout.mods[0].filesz == tls_filesz &&
               cact_tls_layout.mods[0].memsz  == tls_memsz,
               "the module does not describe the plugin's PT_TLS (%lu/%u/%u vs %u/%u/%u)",
               cact_tls_layout.mods[0].vaddr, cact_tls_layout.mods[0].filesz,
               cact_tls_layout.mods[0].memsz, tls_vaddr, tls_filesz, tls_memsz);

            /* The image reached the block this thread already had ... */
            CK(*(int *)(tp - (long)total + off) == 0x5A5A5A5A,
               "the plugin's __thread image is not in the existing block (%#x)",
               *(int *)(tp - (long)total + off));
            /* ... and a block made afterwards gets it from the layout. */
            {
                unsigned char *tp2 = (unsigned char *)cact_tls_block_new();
                CK(tp2 && tp2 != tp &&
                   *(int *)(tp2 - (long)total + off) == 0x5A5A5A5A,
                   "a later thread's block did not get the plugin's image");
            }

            /* The relocation: the GOT slot has to hold the displacement from
             * the thread pointer down to the variable (the plugin's only
             * variable sits at TLS offset 0). */
            {
                uint32_t rel = 0, relsz = 0, found = 0;
                Elf32_Dyn *d = (Elf32_Dyn *)(uintptr_t)o->dyn;
                for (i = 0; i < o->dynsz / sizeof(Elf32_Dyn); i++) {
                    if (d[i].d_tag == DT_REL)        rel   = d[i].d_un.d_ptr;
                    else if (d[i].d_tag == DT_RELSZ) relsz = d[i].d_un.d_val;
                    else if (d[i].d_tag == DT_NULL)  break;
                }
                CK(rel && relsz, "the plugin has no .rel.dyn to check");
                for (i = 0; rel && i < relsz / sizeof(Elf32_Rel); i++) {
                    Elf32_Rel *r = (Elf32_Rel *)(uintptr_t)(rel + i * sizeof(Elf32_Rel));
                    if (ELF32_R_TYPE(r->r_info) != R_386_TLS_TPOFF) continue;
                    found++;
                    CK(*(uint32_t *)(uintptr_t)r->r_offset == off - total,
                       "the TPOFF relocation wrote %#x, want %#x",
                       *(uint32_t *)(uintptr_t)r->r_offset, off - total);
                }
                CK(found >= 1, "the plugin has no TPOFF relocation to check");
            }
        }

        /* A plugin built for the default (general-dynamic) model: its access is
         * a ___tls_get_addr call against the GOT pair the linker emits, which
         * names the plugin's TLS module id and the variable's offset.  Both are
         * filled in from the module the loader just added, so the plugin loads
         * and cact_tls_addr() turns the pair into the variable's address. */
        void *h_gd = cact_dlopen(gdtlsp, RTLD_NOW);
        CK(h_gd != 0, "the general-dynamic __thread plugin did not load: %s",
           cact_dlerror() ? cact_dlerror() : "?");
        if (h_gd && tp) {
            cact_dl_object_t *g = (cact_dl_object_t *)h_gd;
            uint32_t rel = 0, relsz = 0, k, modid = 0, modoff = 0, seen = 0;
            Elf32_Dyn *d = (Elf32_Dyn *)(uintptr_t)g->dyn;

            CK(g->tls_off != CACT_DL_NO_TLS && g->tls_modid != 0,
               "the general-dynamic plugin got no TLS module (off %#x id %u)",
               g->tls_off, g->tls_modid);
            CK(cact_tls_layout.count == 2,
               "the layout holds %u modules, want 2", cact_tls_layout.count);
            CK(cact_tls_layout.mods[1].id  == g->tls_modid &&
               cact_tls_layout.mods[1].off == g->tls_off,
               "the layout's second module is not the plugin's");

            for (k = 0; k < g->dynsz / sizeof(Elf32_Dyn); k++) {
                if (d[k].d_tag == DT_REL)        rel   = d[k].d_un.d_ptr;
                else if (d[k].d_tag == DT_RELSZ) relsz = d[k].d_un.d_val;
                else if (d[k].d_tag == DT_NULL)  break;
            }
            CK(rel && relsz, "the general-dynamic plugin has no .rel.dyn");
            for (k = 0; rel && k < relsz / sizeof(Elf32_Rel); k++) {
                Elf32_Rel *r = (Elf32_Rel *)(uintptr_t)(rel + k * sizeof(Elf32_Rel));
                uint32_t t = ELF32_R_TYPE(r->r_info);
                if (t == R_386_TLS_DTPMOD32) {
                    modid = *(uint32_t *)(uintptr_t)r->r_offset; seen |= 1;
                } else if (t == R_386_TLS_DTPOFF32) {
                    modoff = *(uint32_t *)(uintptr_t)r->r_offset; seen |= 2;
                }
            }
            CK(seen == 3, "the plugin has no DTPMOD/DTPOFF pair (seen %u)", seen);
            CK(modid == g->tls_modid,
               "DTPMOD32 wrote %u, want the plugin's id %u", modid, g->tls_modid);
            CK(modoff == 0, "DTPOFF32 wrote %u, want the variable's TLS offset 0",
               modoff);

            /* What ___tls_get_addr() computes from the pair: the variable sits
             * at the module's block offset, i.e. tp - total + off. */
            CK((unsigned char *)cact_tls_addr(&cact_tls_layout, modid, modoff,
                                              (unsigned long)tp)
                   == tp - (long)total + g->tls_off,
               "cact_tls_addr did not land on the module's slice");

            CK(cact_dlclose(h_gd) == 0,
               "dlclose of the general-dynamic plugin failed");
            CK(cact_tls_layout.count == 1,
               "dlclose left the module in the layout (%u)", cact_tls_layout.count);
        }

        /* Unloading the object takes its image out of the layout, so nothing
         * copies out of a mapping that is gone. */
        if (h_tls) {
            CK(cact_dlclose(h_tls) == 0, "dlclose of the __thread plugin failed");
            CK(cact_tls_layout.count == 0 && cact_tls_layout.mods[0].memsz == 0,
               "dlclose left the TLS image published");
        }
    }

    /* dlmopen / dlinfo: a namespace is a symbol scope, and LM_ID_BASE is the
     * program's own one — so LM_ID_BASE is dlopen() (the plugin is already
     * loaded, so the handle is the same and only the reference count moves). */
    {
        void *hb = cact_dlmopen(LM_ID_BASE, plugin, RTLD_NOW);
        long lm = 12345;
        CK(hb != 0, "dlmopen(LM_ID_BASE) failed: %s",
           cact_dlerror() ? cact_dlerror() : "?");
        if (hb) {
            CK(cact_dlinfo(hb, RTLD_DI_LMID, &lm) == 0 && lm == LM_ID_BASE,
               "dlinfo(RTLD_DI_LMID) said %ld, want %d", lm, LM_ID_BASE);
            CK(cact_dlclose(hb) == 0, "dlclose of the dlmopen handle failed");
        }

        /* A fresh namespace of its own, holding the one plugin nothing else has
         * mapped.  Its symbols are reachable through the handle and through no
         * other scope — that isolation is the point. */
        lm = -1;
        void *iso = cact_dlmopen(LM_ID_NEWLM, iso_plugin, RTLD_NOW);
        CK(iso != 0, "dlmopen(LM_ID_NEWLM) failed: %s",
           cact_dlerror() ? cact_dlerror() : "?");
        if (iso) {
            long own = -1;
            CK(cact_dlinfo(iso, RTLD_DI_LMID, &own) == 0 && own != LM_ID_BASE,
               "a new namespace reported Lmid %ld", own);
            /* the same id, named explicitly, is a live namespace */
            CK(cact_dlmopen(own, iso_plugin, RTLD_NOW) == iso,
               "dlmopen(its own id) did not reuse the object");
            /* and it is the same object, so the reference count moved */
            CK(cact_dlclose(iso) == 0, "dlclose of the new namespace's object failed");
            /* the symbol is in the handle's scope ... */
            CK(cact_dlsym(iso, "plugin_iso_marker") != 0,
               "the new namespace's symbol was not found through its handle");
            /* ... and NOT in the global one */
            CK(cact_dlsym(RTLD_DEFAULT, "plugin_iso_marker") == 0,
               "a namespaced symbol leaked into the global scope");
            /* A library can live in one namespace only: the address is taken, so
             * the base namespace cannot have its own copy. */
            CK(cact_dlopen(iso_plugin, RTLD_NOW) == 0,
               "the same image loaded twice, in two namespaces");
            CK(cact_dlerror() != 0, "no error text for the second copy");
            CK(cact_dlclose(iso) == 0, "dlclose of the new namespace failed");
            /* its last object went away, so the id is not a live scope any more */
            CK(cact_dlmopen(own, iso_plugin, RTLD_NOW) == 0,
               "a released namespace id was still accepted");
            CK(cact_dlerror() != 0, "no error text for a stale namespace id");
        }

        CK(cact_dlmopen(LM_ID_NEWLM, plugin, RTLD_NOW) == 0,
           "an already-loaded plugin got a second copy in a new namespace");
        CK(cact_dlerror() != 0, "no error text for the refused second copy");
        CK(cact_dlmopen(99, plugin, RTLD_NOW) == 0,
           "an unknown namespace id was accepted");
    }

    /* Closing: the plugin goes away; clibc.so (pinned in a real process, where
     * ld.so loaded it) just drops a reference here. */
    CK(cact_dlclose(h_plug) == 0, "dlclose of the plugin failed");
    CK(cact_dlclose(h_libc) == 0, "dlclose of clibc.so failed");

    printf("dlfcn: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
