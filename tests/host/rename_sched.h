/* Host-test renames for src/sched.c (see rename_tz.h for why they exist). */
#ifndef CACT_HOST_RENAME_SCHED_H
#define CACT_HOST_RENAME_SCHED_H

#undef errno
#define __errno_location cact_host_errno_location

#define sched_setscheduler     cact_sched_setscheduler
#define sched_getscheduler     cact_sched_getscheduler
#define sched_setparam         cact_sched_setparam
#define sched_getparam         cact_sched_getparam
#define sched_get_priority_max cact_sched_get_priority_max
#define sched_get_priority_min cact_sched_get_priority_min
#define sched_rr_get_interval  cact_sched_rr_get_interval
#define sched_yield            cact_sched_yield

#endif
