/* Host-test renames for src/spawn.c (see rename_tz.h for why they exist).
 *
 * The sched_* calls the child makes are renamed to this libc's own too, so the
 * unit exercises the real attribute handling: without that, the link would bind
 * them to glibc's sched_setscheduler, which would try to set a system policy. */
#ifndef CACT_HOST_RENAME_SPAWN_H
#define CACT_HOST_RENAME_SPAWN_H

#undef errno
#define __errno_location cact_host_errno_location

#define posix_spawn                    cact_posix_spawn
#define posix_spawnp                   cact_posix_spawnp
#define posix_spawn_file_actions_init  cact_spawn_fa_init
#define posix_spawn_file_actions_destroy cact_spawn_fa_destroy
#define posix_spawn_file_actions_addopen  cact_spawn_fa_addopen
#define posix_spawn_file_actions_addclose cact_spawn_fa_addclose
#define posix_spawn_file_actions_adddup2  cact_spawn_fa_adddup2
#define posix_spawnattr_init           cact_spawnattr_init
#define posix_spawnattr_destroy        cact_spawnattr_destroy
#define posix_spawnattr_getflags       cact_spawnattr_getflags
#define posix_spawnattr_setflags       cact_spawnattr_setflags
#define posix_spawnattr_getpgroup      cact_spawnattr_getpgroup
#define posix_spawnattr_setpgroup      cact_spawnattr_setpgroup
#define posix_spawnattr_getsigdefault  cact_spawnattr_getsigdefault
#define posix_spawnattr_setsigdefault  cact_spawnattr_setsigdefault
#define posix_spawnattr_getsigmask     cact_spawnattr_getsigmask
#define posix_spawnattr_setsigmask     cact_spawnattr_setsigmask
#define posix_spawnattr_getschedparam  cact_spawnattr_getschedparam
#define posix_spawnattr_setschedparam  cact_spawnattr_setschedparam
#define posix_spawnattr_getschedpolicy cact_spawnattr_getschedpolicy
#define posix_spawnattr_setschedpolicy cact_spawnattr_setschedpolicy

#define sched_setscheduler     cact_sched_setscheduler
#define sched_getscheduler     cact_sched_getscheduler
#define sched_setparam         cact_sched_setparam
#define sched_getparam         cact_sched_getparam
#define sched_get_priority_max cact_sched_get_priority_max
#define sched_get_priority_min cact_sched_get_priority_min
#define sched_yield            cact_sched_yield

#endif
