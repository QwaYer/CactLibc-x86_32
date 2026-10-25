/* Host-test renames for src/thread_local.c (see rename_stdio.h).
 *
 * __errno_location is renamed to a unit-private symbol: the unit *defines* it,
 * so it must not collide with the shim's own definition. */
#ifndef CACT_HOST_RENAME_THREAD_LOCAL_H
#define CACT_HOST_RENAME_THREAD_LOCAL_H

#undef errno
#define __errno_location cact_tl_errno_location

#define set_thread_area cact_set_thread_area

#endif
