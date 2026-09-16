/*
 * flock() — advisory whole-file locking.
 *
 * CactOS has no kernel record-lock support to build on: sys_fcntl() handles
 * only F_DUPFD/F_GETFD/F_SETFD/F_GETFL/F_SETFL and the VFS keeps no lock
 * table.  This is therefore a process-local advisory table keyed by inode:
 * it gives correct mutual exclusion between the open file descriptions of one
 * process (shared vs exclusive, and dup'd descriptors of the same file map to
 * the same entry), but it cannot exclude another process.
 *
 * libwayland only uses LOCK_EX|LOCK_NB to detect a rival compositor, so the
 * denied/blocked paths are the ones that matter here.  Once the kernel grows
 * F_SETLK/F_GETLK this can be reimplemented on top of it behind the same
 * interface, without touching the callers.
 */
#include "file.h"
#include "stat.h"
#include "unistd.h"
#include "errno.h"
#include "stdint.h"

#define FLOCK_SLOTS     64
#define FLOCK_RETRY_US  1000
#define FLOCK_RETRY_MAX 5000    /* ~5 s before a blocking request gives up */

#define FLOCK_FREE 0
#define FLOCK_SHARED 1
#define FLOCK_EXCLUSIVE 2

struct flock_slot {
    uint32_t ino;
    uint8_t  mode;
    uint32_t holders;
};

static struct flock_slot slots[FLOCK_SLOTS];

static struct flock_slot *slot_find(uint32_t ino) {
    for (int i = 0; i < FLOCK_SLOTS; i++)
        if (slots[i].mode != FLOCK_FREE && slots[i].ino == ino) return &slots[i];
    return 0;
}

static struct flock_slot *slot_take(uint32_t ino) {
    struct flock_slot *s = slot_find(ino);
    if (s) return s;
    for (int i = 0; i < FLOCK_SLOTS; i++) {
        if (slots[i].mode == FLOCK_FREE) {
            slots[i].ino = ino;
            slots[i].holders = 0;
            return &slots[i];
        }
    }
    return 0;
}

static int grant(uint32_t ino, int shared) {
    struct flock_slot *s = slot_find(ino);

    if (shared) {
        if (s && s->mode == FLOCK_EXCLUSIVE) return -1;
        if (!s) s = slot_take(ino);
        if (!s) { errno = ENOLCK; return -1; }
        s->mode = FLOCK_SHARED;
        s->holders++;
        return 0;
    }

    /* Exclusive: nobody may hold anything. */
    if (s && s->holders > 0) return -1;
    if (!s) s = slot_take(ino);
    if (!s) { errno = ENOLCK; return -1; }
    s->mode = FLOCK_EXCLUSIVE;
    s->holders = 1;
    return 0;
}

static void release(uint32_t ino) {
    struct flock_slot *s = slot_find(ino);
    if (!s) return;
    if (s->holders > 0) s->holders--;
    if (s->holders == 0) s->mode = FLOCK_FREE;
}

int flock(int fd, int operation) {
    struct stat st;
    int op = operation & ~LOCK_NB;
    int shared;
    int tries;

    if (fstat(fd, &st) != 0) return -1;

    if (op == LOCK_UN) {
        release(st.st_ino);
        return 0;
    }
    if (op == LOCK_SH) shared = 1;
    else if (op == LOCK_EX) shared = 0;
    else { errno = EINVAL; return -1; }

    tries = (operation & LOCK_NB) ? 1 : FLOCK_RETRY_MAX;
    while (tries-- > 0) {
        if (grant(st.st_ino, shared) == 0) return 0;
        if (!(operation & LOCK_NB)) usleep(FLOCK_RETRY_US);
    }

    errno = EWOULDBLOCK;
    return -1;
}
