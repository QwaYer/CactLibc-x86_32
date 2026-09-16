#ifndef _FILE_H
#define _FILE_H

/* flock() — advisory whole-file locking (POSIX <sys/file.h> spelling).
 *
 * CactOS has no kernel record-lock support yet, so this is a process-local
 * advisory lock — see src/flock.c for the exact limitation. */

#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8

int flock(int fd, int operation);

#endif /* _FILE_H */
