/* Host-test renames for src/semaphore.c (see rename_tz.h for why they exist).
 *
 * Every sem_* entry point is glibc's too, so the unit's definitions have to be
 * renamed away from glibc's — and unlike the tz names, interposing these would
 * be worse than losing a reference: glibc's sem_t is 32 bytes and this libc's
 * is 8, so a call meant for one would overwrite the other's object.  src/futex.c
 * comes along for the ride (cact_ctl and the futex wrappers are its own
 * symbols, so they need no rename). */
#ifndef CACT_HOST_RENAME_SEMAPHORE_H
#define CACT_HOST_RENAME_SEMAPHORE_H

#undef errno
#define __errno_location cact_host_errno_location

#define sem_init       cact_sem_init
#define sem_destroy    cact_sem_destroy
#define sem_wait       cact_sem_wait
#define sem_trywait    cact_sem_trywait
#define sem_timedwait  cact_sem_timedwait
#define sem_post       cact_sem_post
#define sem_getvalue   cact_sem_getvalue

#endif
