/* Host-test renames for src/glob.c (see rename_stdio.h).
 *
 * glob/globfree go to cact_glob_real/cact_globfree_real — distinct names from
 * the shim's minimal glob() stand-in, which the wordexp unit uses.  The host has
 * no CactOS dirent/stat, so stat/opendir/readdir/closedir go to the shim's
 * repackers (which present the libc's struct layouts).  The `stat` rename also
 * renames the `struct stat` tag, consistently within this one translation unit,
 * which is harmless. */
#ifndef CACT_HOST_RENAME_GLOB_H
#define CACT_HOST_RENAME_GLOB_H

#undef errno
#define __errno_location cact_host_errno_location

#define glob     cact_glob_real
#define globfree cact_globfree_real

#define stat     cact_host_stat
#define opendir  cact_host_opendir
#define readdir  cact_host_readdir
#define closedir cact_host_closedir

#endif
