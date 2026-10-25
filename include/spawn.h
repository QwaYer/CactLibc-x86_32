#ifndef _SPAWN_H
#define _SPAWN_H

#include "unistd.h"          /* pid_t, mode_t */
#include "signal.h"          /* sigset_t */
#include "sched.h"           /* struct sched_param, SCHED_OTHER */

/* posix_spawn: fork + (attributes, file actions) + exec, with the child's exec
 * error reported back to the parent through a close-on-exec pipe — the same
 * protocol glibc uses, which this kernel supports because `exec` really does
 * close FD_CLOEXEC descriptors (task_exec.rs). */

#define POSIX_SPAWN_RESETIDS    0x01
#define POSIX_SPAWN_SETPGROUP   0x02
#define POSIX_SPAWN_SETSIGDEF   0x04
#define POSIX_SPAWN_SETSIGMASK  0x08
#define POSIX_SPAWN_SETSCHEDPARAM 0x10
#define POSIX_SPAWN_SETSCHEDULER  0x20

typedef struct {
    short      flags;
    pid_t      pgroup;
    sigset_t   sigdefault;      /* signal masks, in this libc's SIG* bit form */
    sigset_t   sigmask;
    int        sched_policy;    /* POSIX_SPAWN_SETSCHEDULER: the child's policy */
    struct sched_param sched_param;  /* POSIX_SPAWN_SETSCHEDPARAM */
} posix_spawnattr_t;

typedef struct {
    int    type;                /* 0 = open, 1 = close, 2 = dup2 */
    int    fd;
    int    newfd;
    int    oflag;
    mode_t mode;
    char  *path;                /* owned copy (addopen must not alias the caller) */
} posix_spawn_file_action_t;

typedef struct {
    posix_spawn_file_action_t *acts;
    int n;
    int cap;
} posix_spawn_file_actions_t;

int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp,
                char *const argv[], char *const envp[]);
int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp,
                 char *const argv[], char *const envp[]);

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *fa);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *fa);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *fa, int fd,
                                     const char *path, int oflag, mode_t mode);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *fa, int fd);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *fa, int fd, int newfd);

int posix_spawnattr_init(posix_spawnattr_t *attr);
int posix_spawnattr_destroy(posix_spawnattr_t *attr);
int posix_spawnattr_getflags(const posix_spawnattr_t *attr, short *flags);
int posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags);
int posix_spawnattr_getpgroup(const posix_spawnattr_t *attr, pid_t *pgroup);
int posix_spawnattr_setpgroup(posix_spawnattr_t *attr, pid_t pgroup);
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *attr, sigset_t *set);
int posix_spawnattr_setsigdefault(posix_spawnattr_t *attr, const sigset_t *set);
int posix_spawnattr_getsigmask(const posix_spawnattr_t *attr, sigset_t *set);
int posix_spawnattr_setsigmask(posix_spawnattr_t *attr, const sigset_t *set);

/* The scheduling attributes.  CactOS supports SCHED_OTHER only (see <sched.h>),
 * so a child asked for SCHED_FIFO/SCHED_RR cannot be started and posix_spawn()
 * reports the failure instead of ignoring the request.  POSIX_SPAWN_SETSCHEDPARAM
 * on its own sets the priority of the (SCHED_OTHER) child, which must be 0. */
int posix_spawnattr_getschedparam(const posix_spawnattr_t *attr,
                                  struct sched_param *param);
int posix_spawnattr_setschedparam(posix_spawnattr_t *attr,
                                  const struct sched_param *param);
int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *attr, int *policy);
int posix_spawnattr_setschedpolicy(posix_spawnattr_t *attr, int policy);

#endif
