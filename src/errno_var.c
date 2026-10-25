/* The process-global errno of the pre-2026-10 headers, kept only as a
 * link-time symbol so that userland binaries built against them still load.
 * Nothing in the library reads or writes it any more: <errno.h> defines errno
 * as a macro over __errno_location(), which is per thread (src/thread_local.c).
 *
 * errno.h is deliberately not included here — its macro would rewrite the
 * definition below. */
int errno = 0;
