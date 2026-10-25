/*
 * POSIX shared memory — shm_open()/shm_unlink() and the /dev/shm name space.
 *
 * This is deliberately a thin naming layer, not a new object type: the kernel
 * mounts a tmpfs at /dev/shm (Cact/fs/vfs/mntfs/mntfs.c), and a tmpfs file's
 * contents already live in a memfd page array (Cact/fs/rust_vfs/src/addr.rs).
 * So the descriptor this returns is an ordinary one — ftruncate() sizes the
 * object, read()/write() and a MAP_SHARED mmap() observe the same storage, and
 * two processes mapping the same object rendezvous on the kernel's shared
 * futex (keyed by the word's physical page).  SysV shmget/shmat is separate
 * and unchanged; this is the POSIX face of the same RAM.
 *
 * shm_unlink() removes the name only.  As POSIX requires, an object already
 * open or mapped stays alive: the tmpfs inode is freed when its last directory
 * link and its last open handle are both gone (tmpfs.c), and a live mapping
 * holds its own reference to the backing memfd.
 */

#include "mman.h"
#include "fcntl.h"
#include "unistd.h"
#include "errno.h"

/* The mount point, and the longest name a tmpfs directory entry can hold
 * (TMPFS_NAME_LEN - 1 = 127, Cact/fs/vfs/tmpfs/tmpfs.c). */
#define SHM_DIR       "/dev/shm"
#define SHM_NAME_MAX  127

#define SHM_PATH_MAX  (sizeof(SHM_DIR) + SHM_NAME_MAX + 1)

/* POSIX: the name is "/" followed by a non-empty component with no further
 * '/'.  Anything else ("name", "/", "/a/b") is EINVAL. */
static int shm_name_ok(const char *name) {
    if (!name || name[0] != '/') return 0;
    if (!name[1]) return 0;
    for (int i = 1; name[i]; i++)
        if (name[i] == '/') return 0;
    return 1;
}

static int shm_name_len(const char *name) {
    int n = 0;
    while (name[1 + n]) n++;
    return n;
}

/* "/name" -> "/dev/shm/name" (name already validated). */
static void shm_path(char *out, const char *name) {
    int i = 0;
    for (; SHM_DIR[i]; i++) out[i] = SHM_DIR[i];
    out[i++] = '/';
    for (int j = 1; name[j]; j++) out[i++] = name[j];
    out[i] = '\0';
}

int shm_open(const char *name, int oflag, mode_t mode) {
    if (!shm_name_ok(name)) { errno = EINVAL; return -1; }
    if (shm_name_len(name) > SHM_NAME_MAX) { errno = ENAMETOOLONG; return -1; }

    /* POSIX allows only O_RDONLY or O_RDWR as the access mode. */
    int acc = oflag & O_ACCMODE;
    if (acc != O_RDONLY && acc != O_RDWR) { errno = EINVAL; return -1; }

    char path[SHM_PATH_MAX];
    shm_path(path, name);

    int fd = open(path, oflag, mode);
    if (fd < 0) {
        /* SYS_OPEN reports every failure as a bare -1 (no errno channel), so
         * recover the reason the flags imply: O_CREAT|O_EXCL only fails
         * because the object exists, a non-creating open because it does not,
         * and anything else is a refusal. */
        if ((oflag & O_CREAT) && (oflag & O_EXCL)) errno = EEXIST;
        else if (!(oflag & O_CREAT))                errno = ENOENT;
        else                                        errno = EACCES;
        return -1;
    }

    /* POSIX: the descriptor has FD_CLOEXEC set.  This kernel takes it through
     * fcntl(F_SETFD), not from an O_CLOEXEC open flag. */
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

int shm_unlink(const char *name) {
    if (!shm_name_ok(name)) { errno = EINVAL; return -1; }
    if (shm_name_len(name) > SHM_NAME_MAX) { errno = ENAMETOOLONG; return -1; }

    char path[SHM_PATH_MAX];
    shm_path(path, name);
    return unlink(path);
}
