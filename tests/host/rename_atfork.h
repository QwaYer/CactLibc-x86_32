/* Host-test renames for src/atfork.c (see rename_tz.h for why they exist). */
#ifndef CACT_HOST_RENAME_ATFORK_H
#define CACT_HOST_RENAME_ATFORK_H

#undef errno
#define __errno_location cact_host_errno_location

#define pthread_atfork        cact_pthread_atfork
#define __cact_atfork_prepare cact_atfork_prepare
#define __cact_atfork_parent  cact_atfork_parent
#define __cact_atfork_child   cact_atfork_child

#endif
