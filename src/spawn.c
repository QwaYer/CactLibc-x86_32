#include "spawn.h"
#include "fcntl.h"
#include "errno.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "signal.h"

/* POSIX spawn: fork a child, let it apply the file actions and attributes,
 * exec, and report an exec failure to the parent.  The failure is carried over
 * a pipe whose write end is FD_CLOEXEC: on a successful exec the kernel closes
 * it (CactKernel task_exec.rs does unref+clear for descriptors with the flag),
 * so the parent's read returns EOF and it knows the exec succeeded. */

enum { FA_OPEN = 0, FA_CLOSE = 1, FA_DUP2 = 2 };

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *fa) {
    if (!fa) return EINVAL;
    fa->acts = 0;
    fa->n = 0;
    fa->cap = 0;
    return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *fa) {
    if (!fa) return EINVAL;
    for (int i = 0; i < fa->n; i++) free(fa->acts[i].path);
    free(fa->acts);
    fa->acts = 0;
    fa->n = fa->cap = 0;
    return 0;
}

static posix_spawn_file_action_t *fa_push(posix_spawn_file_actions_t *fa) {
    if (!fa) return 0;
    if (fa->n == fa->cap) {
        int cap = fa->cap ? fa->cap * 2 : 4;
        posix_spawn_file_action_t *grown =
            (posix_spawn_file_action_t *)realloc(fa->acts, (size_t)cap * sizeof(*grown));
        if (!grown) return 0;
        fa->acts = grown;
        fa->cap = cap;
    }
    posix_spawn_file_action_t *a = &fa->acts[fa->n++];
    memset(a, 0, sizeof(*a));
    return a;
}

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *fa, int fd,
                                     const char *path, int oflag, mode_t mode) {
    if (!fa || !path || fd < 0) return EINVAL;
    char *copy = (char *)malloc(strlen(path) + 1);
    if (!copy) return ENOMEM;

    posix_spawn_file_action_t *a = fa_push(fa);
    if (!a) { free(copy); return ENOMEM; }
    a->type = FA_OPEN;
    a->fd = fd;
    a->oflag = oflag;
    a->mode = mode;
    a->path = copy;                     /* the caller's string is not aliased */
    strcpy(copy, path);
    return 0;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *fa, int fd) {
    if (!fa || fd < 0) return EINVAL;
    posix_spawn_file_action_t *a = fa_push(fa);
    if (!a) return ENOMEM;
    a->type = FA_CLOSE;
    a->fd = fd;
    return 0;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *fa, int fd, int newfd) {
    if (!fa || fd < 0 || newfd < 0) return EINVAL;
    posix_spawn_file_action_t *a = fa_push(fa);
    if (!a) return ENOMEM;
    a->type = FA_DUP2;
    a->fd = fd;
    a->newfd = newfd;
    return 0;
}

int posix_spawnattr_init(posix_spawnattr_t *attr) {
    if (!attr) return EINVAL;
    attr->flags = 0;
    attr->pgroup = 0;
    attr->sigdefault = 0;
    attr->sigmask = 0;
    attr->sched_policy = SCHED_OTHER;
    attr->sched_param.sched_priority = 0;
    return 0;
}

int posix_spawnattr_destroy(posix_spawnattr_t *attr) {
    if (!attr) return EINVAL;
    return 0;
}

int posix_spawnattr_getflags(const posix_spawnattr_t *attr, short *flags) {
    if (!attr || !flags) return EINVAL;
    *flags = attr->flags;
    return 0;
}

int posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags) {
    if (!attr) return EINVAL;
    attr->flags = flags;
    return 0;
}

int posix_spawnattr_getpgroup(const posix_spawnattr_t *attr, pid_t *pgroup) {
    if (!attr || !pgroup) return EINVAL;
    *pgroup = attr->pgroup;
    return 0;
}

int posix_spawnattr_setpgroup(posix_spawnattr_t *attr, pid_t pgroup) {
    if (!attr) return EINVAL;
    attr->pgroup = pgroup;
    return 0;
}

int posix_spawnattr_getsigdefault(const posix_spawnattr_t *attr, sigset_t *set) {
    if (!attr || !set) return EINVAL;
    *set = attr->sigdefault;
    return 0;
}

int posix_spawnattr_setsigdefault(posix_spawnattr_t *attr, const sigset_t *set) {
    if (!attr || !set) return EINVAL;
    attr->sigdefault = *set;
    return 0;
}

int posix_spawnattr_getsigmask(const posix_spawnattr_t *attr, sigset_t *set) {
    if (!attr || !set) return EINVAL;
    *set = attr->sigmask;
    return 0;
}

int posix_spawnattr_setsigmask(posix_spawnattr_t *attr, const sigset_t *set) {
    if (!attr || !set) return EINVAL;
    attr->sigmask = *set;
    return 0;
}

/* The scheduling attributes only carry a value here; whether the policy can be
 * honoured is decided by sched_setscheduler() in the child, exactly as POSIX
 * puts it — an unsupported policy makes the spawn fail rather than being
 * silently dropped. */
int posix_spawnattr_getschedparam(const posix_spawnattr_t *attr,
                                  struct sched_param *param) {
    if (!attr || !param) return EINVAL;
    *param = attr->sched_param;
    return 0;
}

int posix_spawnattr_setschedparam(posix_spawnattr_t *attr,
                                  const struct sched_param *param) {
    if (!attr || !param) return EINVAL;
    attr->sched_param = *param;
    return 0;
}

int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *attr, int *policy) {
    if (!attr || !policy) return EINVAL;
    *policy = attr->sched_policy;
    return 0;
}

int posix_spawnattr_setschedpolicy(posix_spawnattr_t *attr, int policy) {
    if (!attr) return EINVAL;
    attr->sched_policy = policy;
    return 0;
}

/* ── the spawn itself ───────────────────────────────────────────────────── */

/* A pre-exec failure in the child: exit with POSIX's spawn-error status, but
 * first hand the errno to the parent through the same pipe an exec failure uses
 * — otherwise the parent's read sees a plain EOF and reports a *successful*
 * spawn for a child that died in its setup.  The code must be non-zero: a zero
 * would take the parent's error branch and return 0 without ever setting `*pid`,
 * so a caller would see "success" and an untouched pid. */
static void child_fail(int errfd, int err) {
    ssize_t ignored;
    if (err <= 0) err = EIO;
    ignored = write(errfd, &err, sizeof err);
    (void)ignored;
    _exit(127);
}

static void child_setup(const posix_spawn_file_actions_t *fa,
                        const posix_spawnattr_t *attr, int errfd) {
    if (fa) {
        for (int i = 0; i < fa->n; i++) {
            const posix_spawn_file_action_t *a = &fa->acts[i];
            if (a->type == FA_OPEN) {
                int fd = open(a->path, a->oflag, a->mode);
                if (fd < 0) child_fail(errfd, errno);
                if (fd != a->fd) {
                    if (dup2(fd, a->fd) < 0) child_fail(errfd, errno);
                    close(fd);
                }
            } else if (a->type == FA_CLOSE) {
                close(a->fd);
            } else if (a->type == FA_DUP2) {
                if (dup2(a->fd, a->newfd) < 0) child_fail(errfd, errno);
            }
        }
    }
    if (!attr) return;

    if (attr->flags & POSIX_SPAWN_SETPGROUP)
        setpgid(0, attr->pgroup);
    if (attr->flags & POSIX_SPAWN_SETSIGMASK)
        sigprocmask(SIG_SETMASK, &attr->sigmask, 0);
    if (attr->flags & POSIX_SPAWN_SETSIGDEF) {
        for (int bit = 0; bit < KERNEL_NSIG; bit++)
            if (attr->sigdefault & (1u << bit))
                signal((int)(1u << bit), SIG_DFL);
    }
    if (attr->flags & POSIX_SPAWN_RESETIDS) {
        setgid(getgid());
        setuid(getuid());
    }
    /* The scheduling attributes, applied to this child before it execs.  POSIX
     * wants the effect of sched_setscheduler()/sched_setparam(); a policy this
     * system does not support (anything but SCHED_OTHER) therefore stops the
     * child, and the parent's posix_spawn() returns that error. */
    if (attr->flags & POSIX_SPAWN_SETSCHEDULER) {
        if (sched_setscheduler(0, attr->sched_policy, &attr->sched_param) != 0)
            child_fail(errfd, errno ? errno : EINVAL);
    } else if (attr->flags & POSIX_SPAWN_SETSCHEDPARAM) {
        if (sched_setparam(0, &attr->sched_param) != 0)
            child_fail(errfd, errno ? errno : EINVAL);
    }
}

static int spawn_common(pid_t *pid, const char *path, int search_path,
                        const posix_spawn_file_actions_t *fa,
                        const posix_spawnattr_t *attr,
                        char *const argv[], char *const envp[]) {
    if (!path || !*path) return ENOENT;

    int fds[2];
    if (pipe(fds) != 0) return errno ? errno : EIO;
    /* The write end must not survive a successful exec: that is what turns the
     * parent's read into EOF.  fcntl(F_SETFD) sets the same flag exec checks. */
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    pid_t child = fork();
    if (child < 0) {
        int e = errno ? errno : EAGAIN;
        close(fds[0]);
        close(fds[1]);
        return e;
    }

    if (child == 0) {
        close(fds[0]);
        child_setup(fa, attr, fds[1]);

        if (search_path) execvp(path, argv);
        else             execve(path, argv, envp ? envp : environ);

        int e = errno ? errno : ENOENT;
        ssize_t ignored = write(fds[1], &e, sizeof e);
        (void)ignored;
        _exit(127);
    }

    close(fds[1]);
    int child_err = 0;
    ssize_t n = read(fds[0], &child_err, sizeof child_err);
    close(fds[0]);

    if (n > 0) {                        /* the child could not exec */
        int status = 0;
        waitpid(child, &status, 0);
        return child_err;
    }
    if (pid) *pid = child;
    return 0;
}

int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp,
                char *const argv[], char *const envp[]) {
    return spawn_common(pid, path, 0, file_actions, attrp, argv, envp);
}

int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp,
                 char *const argv[], char *const envp[]) {
    return spawn_common(pid, file, 1, file_actions, attrp, argv, envp);
}
