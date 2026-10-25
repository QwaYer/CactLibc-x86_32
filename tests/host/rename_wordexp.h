/* Host-test renames for src/wordexp.c (see rename_stdio.h).  glob/globfree are
 * redirected to the shim's minimal GLOB_NOCHECK stand-in. */
#ifndef CACT_HOST_RENAME_WORDEXP_H
#define CACT_HOST_RENAME_WORDEXP_H

#undef errno
#define __errno_location cact_host_errno_location

#define wordexp  cact_wordexp
#define wordfree cact_wordfree
#define glob     cact_glob
#define globfree cact_globfree

#endif
