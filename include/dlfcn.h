#ifndef _DLFCN_H
#define _DLFCN_H

#define RTLD_LAZY     1
#define RTLD_NOW      2
#define RTLD_GLOBAL   0x100
#define RTLD_LOCAL    0
#define RTLD_DEFAULT  ((void*)0)
/* For dlsym(): the object after the caller's own.  For dlinfo() there is no
 * "next" object to name, so it is refused. */
#define RTLD_NEXT     ((void*)-1)

void *dlopen(const char *filename, int flags);
int dlclose(void *handle);
void *dlsym(void *handle, const char *symbol);
/* The GNU versioned-symbol lookup; CactOS has no version scripts, so this is
 * dlsym() with the version ignored. */
void *dlvsym(void *handle, const char *symbol, const char *version);
char *dlerror(void);

/* dladdr(): which object (and which symbol) an address belongs to. */
typedef struct {
    const char *dli_fname;   /* the object's name */
    void       *dli_fbase;   /* its base address */
    const char *dli_sname;   /* the nearest symbol at or below the address, or 0 */
    void       *dli_saddr;   /* that symbol's address, or 0 */
} Dl_info;

int dladdr(const void *addr, Dl_info *info);

/* dlinfo(): ask the loader about one object (the Solaris interface glibc has).
 * The requests that make sense for a loader with no search path and no
 * namespaces are implemented:
 *
 *   RTLD_DI_LINKMAP  *(struct link_map **)arg = the object's link map
 *                    (include/link.h); `l_addr` is always 0 here, because
 *                    CactOS maps every image at its link-time address.
 *   RTLD_DI_ORIGIN   copies the object's directory (the part of its path up to
 *                    the last '/') into arg, which must be a PATH_MAX buffer.
 *                    For a bare name that is /usr/lib, where the loaders look,
 *                    and for the program itself "." (the loader is not told
 *                    where the executable came from).
 *   RTLD_DI_LMID     *(Lmid_t *)arg = LM_ID_BASE — there is one namespace
 *                    (see dlmopen() below).
 *   RTLD_DI_PHDR     *(const Elf32_Phdr **)arg = the object's program header
 *                    table, and the *number of entries* is the return value
 *                    (glibc does the same, odd as it looks).
 *
 * Anything else (the search-path SERINFO/SERINFOSIZE, the Solaris profiling and
 * CONFIGADDR requests, TLS_MODID/TLS_DATA) reports through dlerror() and
 * returns -1, as glibc does for a request it does not serve.  Unlike glibc, a
 * NULL arg is refused rather than dereferenced. */
typedef long int Lmid_t;

/* Namespace ids for dlmopen().  A namespace here is a **symbol scope**, not a
 * second address space: every image is mapped once, at its link-time address, so
 * a library belongs to exactly one namespace (loading it into a second one is
 * refused by the same address-collision check that protects any live mapping).
 * What a namespace buys is isolation — a lookup started inside one sees that
 * namespace's objects and nothing else, except the pinned objects (the program
 * and clibc.so), which every namespace shares because they cannot be mapped
 * twice.  LM_ID_BASE is the program's own namespace; LM_ID_NEWLM asks for a
 * fresh one (a returned id stays valid until the last object in it is
 * dlclose()d). */
#define LM_ID_BASE   0
#define LM_ID_NEWLM  (-1)

#define RTLD_DI_LMID        1
#define RTLD_DI_LINKMAP     2
#define RTLD_DI_CONFIGADDR  3
#define RTLD_DI_SERINFO     4
#define RTLD_DI_SERINFOSIZE 5
#define RTLD_DI_ORIGIN      6
#define RTLD_DI_PROFILENAME 7
#define RTLD_DI_PROFILEOUT  8
#define RTLD_DI_TLS_MODID   9
#define RTLD_DI_TLS_DATA    10
#define RTLD_DI_PHDR        11

int dlinfo(void *handle, int request, void *arg);

/* dlmopen(): load into a namespace.  LM_ID_BASE is the program's own namespace
 * (so it behaves as dlopen()); LM_ID_NEWLM creates a fresh one and returns an id
 * that later calls may name; any other value must be an id dlmopen() handed out
 * earlier and still live.  See the note on the LM_ID_* constants for what a
 * namespace is — and is not — here.  Prefer dlopen() unless you are porting code
 * that calls dlmopen(). */
void *dlmopen(Lmid_t nsid, const char *filename, int flags);

#endif

