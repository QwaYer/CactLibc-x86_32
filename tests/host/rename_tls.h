/* Host-test renames for src/tls_static.c (see rename_tz.h for the rationale).
 *
 * set_thread_area goes to the shim's recorder so the test can see the %gs base
 * the libc tried to install. */
#ifndef CACT_HOST_RENAME_TLS_H
#define CACT_HOST_RENAME_TLS_H

#undef errno
#define __errno_location cact_host_errno_location

#define set_thread_area cact_host_set_thread_area

#endif
