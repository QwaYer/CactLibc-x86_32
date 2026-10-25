/* Host-test renames for src/shm_open.c.
 *
 * shm_open.c is deliberately a naming layer over the libc's own open(),
 * unlink() and fcntl() (see the file's header comment), so this unit builds it
 * against recording stubs for those three instead of a kernel.  That lets the
 * test check exactly what the layer assembled — the /dev/shm path, the flags,
 * the mode, the FD_CLOEXEC — which is the whole of its behaviour.  The kernel
 * half (the tmpfs mounted at /dev/shm, so the frames really are shared) needs a
 * boot, and is exercised by the on-target check, not here.
 *
 * shm_open/shm_unlink are renamed too: glibc has both. */
#ifndef CACT_HOST_RENAME_SHM_OPEN_H
#define CACT_HOST_RENAME_SHM_OPEN_H

#undef errno
#define __errno_location cact_host_errno_location

#define shm_open   cact_shm_open
#define shm_unlink cact_shm_unlink

/* The three entry points shm_open.c is built on, redirected to the recording
 * stubs defined in tests/host/shm_open_test.c. */
#define open   cact_shm_host_open
#define unlink cact_shm_host_unlink
#define fcntl  cact_shm_host_fcntl

#endif
