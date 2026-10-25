/* spawn host test: the shipped src/spawn.c, driven end to end.
 *
 * posix_spawn() really forks and execs here (/bin/true), so the scheduling
 * attributes are exercised through the child's actual setup: a supported policy
 * starts the program, an unsupported one makes the parent's posix_spawn() return
 * the error instead of leaving a dead child behind a "success".  The libc's
 * spawnattr/file-actions types are opaque to this file — it only ever drives
 * them through their accessors, on an oversized, suitably aligned buffer, so no
 * struct layout is mirrored and nothing can drift silently. */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

struct sched_param { int sched_priority; };

#define SCHED_OTHER 0
#define SCHED_FIFO  1
#define SCHED_RR    2

#define POSIX_SPAWN_SETSCHEDPARAM 0x10
#define POSIX_SPAWN_SETSCHEDULER  0x20

/* the libc's own types, renamed (see rename_spawn.h) */
int  cact_posix_spawn(int *pid, const char *path, const void *fa, const void *attr,
                      char *const argv[], char *const envp[]);
int  cact_spawnattr_init(void *attr);
int  cact_spawnattr_destroy(void *attr);
int  cact_spawnattr_setflags(void *attr, short flags);
int  cact_spawnattr_setpgroup(void *attr, int pgroup);
int  cact_spawnattr_getschedpolicy(const void *attr, int *policy);
int  cact_spawnattr_setschedpolicy(void *attr, int policy);
int  cact_spawnattr_getschedparam(const void *attr, struct sched_param *param);
int  cact_spawnattr_setschedparam(void *attr, const struct sched_param *param);
int  cact_spawn_fa_init(void *fa);
int  cact_spawn_fa_destroy(void *fa);
int  cact_spawn_fa_addopen(void *fa, int fd, const char *path, int oflag, unsigned mode);

#define EINVAL 22

/* Alignment comes from the pointer member; the size is comfortably above both
 * the attr (24) and the file-actions (12) structs. */
typedef union { void *align; unsigned char raw[128]; } opaque_t;

static int checks, fails;

#define CK(cond, ...) do { \
    checks++; \
    if (!(cond)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static int spawn_true(const opaque_t *attr) {
    char *const argv[] = { (char *)"/bin/true", NULL };
    int pid = -1;
    int rc = cact_posix_spawn(&pid, "/bin/true", NULL, attr, argv, NULL);
    if (rc == 0) { int st = 0; waitpid(pid, &st, 0); }
    return rc;
}

int main(void) {
    opaque_t a, f;
    struct sched_param sp;
    int pol = -1, rc;

    /* accessors and the documented defaults */
    CK(cact_spawnattr_init(&a) == 0, "attr_init");
    CK(cact_spawnattr_getschedpolicy(&a, &pol) == 0 && pol == SCHED_OTHER,
       "default policy is %d", pol);
    memset(&sp, 0, sizeof sp);
    CK(cact_spawnattr_getschedparam(&a, &sp) == 0 && sp.sched_priority == 0,
       "default priority is %d", sp.sched_priority);

    sp.sched_priority = 7;
    CK(cact_spawnattr_setschedparam(&a, &sp) == 0, "setschedparam");
    memset(&sp, 0, sizeof sp);
    CK(cact_spawnattr_getschedparam(&a, &sp) == 0 && sp.sched_priority == 7,
       "schedparam round trip (%d)", sp.sched_priority);
    CK(cact_spawnattr_setschedpolicy(&a, SCHED_FIFO) == 0, "setschedpolicy");
    CK(cact_spawnattr_getschedpolicy(&a, &pol) == 0 && pol == SCHED_FIFO,
       "policy round trip (%d)", pol);

    CK(cact_spawnattr_init(NULL) == EINVAL, "attr_init(NULL)");
    CK(cact_spawnattr_getschedpolicy(NULL, &pol) == EINVAL, "getpolicy(NULL)");
    CK(cact_spawnattr_setschedparam(&a, NULL) == EINVAL, "setschedparam(NULL)");
    CK(cact_spawnattr_setschedpolicy(&a, SCHED_OTHER) == 0, "setschedpolicy(OTHER)");

    /* A child asked for SCHED_OTHER — the only policy — runs. */
    cact_spawnattr_init(&a);
    cact_spawnattr_setflags(&a, POSIX_SPAWN_SETSCHEDULER);
    cact_spawnattr_setschedpolicy(&a, SCHED_OTHER);
    { struct sched_param p0; p0.sched_priority = 0;
      cact_spawnattr_setschedparam(&a, &p0); }
    CK(spawn_true(&a) == 0, "SCHED_OTHER child did not start");

    /* ... and one asked for a policy this system cannot honour does not start;
     * the parent gets the errno the child's setup produced. */
    cact_spawnattr_init(&a);
    cact_spawnattr_setflags(&a, POSIX_SPAWN_SETSCHEDULER);
    cact_spawnattr_setschedpolicy(&a, SCHED_FIFO);
    { struct sched_param p1; p1.sched_priority = 1;
      cact_spawnattr_setschedparam(&a, &p1); }
    rc = spawn_true(&a);
    CK(rc == EINVAL, "SCHED_FIFO child spawned (rc=%d, want EINVAL)", rc);

    /* SETSCHEDPARAM alone sets the priority of the SCHED_OTHER child: 0 is
     * valid, anything else is not. */
    cact_spawnattr_init(&a);
    cact_spawnattr_setflags(&a, POSIX_SPAWN_SETSCHEDPARAM);
    { struct sched_param p0; p0.sched_priority = 0;
      cact_spawnattr_setschedparam(&a, &p0); }
    CK(spawn_true(&a) == 0, "SETSCHEDPARAM(0) child did not start");

    cact_spawnattr_init(&a);
    cact_spawnattr_setflags(&a, POSIX_SPAWN_SETSCHEDPARAM);
    { struct sched_param p1; p1.sched_priority = 1;
      cact_spawnattr_setschedparam(&a, &p1); }
    rc = spawn_true(&a);
    CK(rc == EINVAL, "SETSCHEDPARAM(1) child spawned (rc=%d, want EINVAL)", rc);

    /* No scheduling flags: an ordinary spawn still works. */
    cact_spawnattr_init(&a);
    CK(spawn_true(&a) == 0, "plain child did not start");

    /* A file action that cannot be performed is reported as well — before this
     * it exited 127 without telling the parent, which read it as success.  The
     * *value* is not pinned: the child's `open` is glibc's here, so the errno it
     * reports is the fallback (the libc's own errno slot is what the real
     * libc's open would set); what matters is that the failure travels. */
    cact_spawn_fa_init(&f);
    CK(cact_spawn_fa_addopen(&f, 0, "/no/such/path/at/all", 0, 0) == 0, "addopen");
    cact_spawnattr_init(&a);
    {
        char *const argv[] = { (char *)"/bin/true", NULL };
        int pid = -1;
        rc = cact_posix_spawn(&pid, "/bin/true", &f, &a, argv, NULL);
        CK(rc != 0 && pid == -1,
           "a failing file action reported rc=%d pid=%d", rc, pid);
    }
    cact_spawn_fa_destroy(&f);

    printf("spawn: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
