/* Host-test renames for src/pthread.c (see rename_dlfcn.h for the errno trick:
 * thread_local.c *defines* __errno_location, so the whole unit must agree on one
 * name for it and keep away from the shim's recorder). */
#ifndef CACT_HOST_RENAME_PTHREAD_H
#define CACT_HOST_RENAME_PTHREAD_H

#undef errno
#define __errno_location cact_pth_errno_location
#define set_thread_area   cact_pth_set_thread_area
#define __cact_thread_exit_stub cact_pth_thread_exit_stub

#define __cact_pid_init              cact_pth_pid_init
#define pthread_attr_init            cact_pthread_attr_init
#define pthread_attr_destroy         cact_pthread_attr_destroy
#define pthread_attr_setinheritsched cact_pthread_attr_setinheritsched
#define pthread_attr_getinheritsched cact_pthread_attr_getinheritsched
#define pthread_attr_setschedpolicy  cact_pthread_attr_setschedpolicy
#define pthread_attr_getschedpolicy  cact_pthread_attr_getschedpolicy
#define pthread_attr_setschedparam   cact_pthread_attr_setschedparam
#define pthread_attr_getschedparam   cact_pthread_attr_getschedparam
#define pthread_attr_setscope        cact_pthread_attr_setscope
#define pthread_attr_getscope        cact_pthread_attr_getscope
#define pthread_setschedparam        cact_pthread_setschedparam
#define pthread_getschedparam        cact_pthread_getschedparam
#define pthread_setschedprio         cact_pthread_setschedprio
#define pthread_create               cact_pthread_create

#endif
