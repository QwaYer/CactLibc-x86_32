/* Host-test renames for src/dlfcn.c (see rename_tz.h for why they exist).
 *
 * The unit pulls in src/thread_local.c for the per-thread dlerror buffer, so
 * that file's own definitions are renamed away from glibc's too. */
#ifndef CACT_HOST_RENAME_DLFCN_H
#define CACT_HOST_RENAME_DLFCN_H

#undef errno
#define __errno_location cact_dl_errno_location
/* Private to this unit: thread_local.c *defines* it, so it must not collide
 * with glibc's symbol nor with the shim's recorder. */
#define set_thread_area  cact_dl_set_thread_area

#define dlopen  cact_dlopen
#define dlclose cact_dlclose
#define dlsym   cact_dlsym
#define dlvsym  cact_dlvsym
#define dladdr  cact_dladdr
#define dlinfo  cact_dlinfo
#define dlmopen cact_dlmopen
#define dl_iterate_phdr cact_dl_iterate_phdr
#define dlerror cact_dlerror

#endif
