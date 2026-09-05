#include "shm.h"
#include "nodeio.h"
#include "errno.h"
#include <stdint.h>

int shmget(int key, unsigned int size, int flags) {
    cact_shmget_arg_t a;
    a.key   = (uint32_t)key;
    a.size  = size;
    a.flags = (uint32_t)flags;
    int r = nio_ctl(CACT_PROCCTL_SHMGET, &a);
    if (r < 0) { errno = -r; return -1; }
    return r;
}

void* shmat(int shmid, const void* shmaddr, int flags) {
    cact_shmat_arg_t a;
    a.shmid = (uint32_t)shmid;
    a.addr  = (uint32_t)(uintptr_t)shmaddr;
    a.flags = (uint32_t)flags;
    int r = nio_ctl(CACT_PROCCTL_SHMAT, &a);
    if (r < 0) { errno = -r; return (void*)-1; }
    return (void*)(uintptr_t)a.addr;
}

int shmdt(const void* shmaddr) {
    uint32_t addr = (uint32_t)(uintptr_t)shmaddr;
    int r = nio_ctl(CACT_PROCCTL_SHMDT, &addr);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}

int shmctl(int shmid, int cmd, void* buf) {
    cact_shmctl_arg_t a;
    a.shmid = (uint32_t)shmid;
    a.cmd   = (uint32_t)cmd;
    a.buf   = buf;
    int r = nio_ctl(CACT_PROCCTL_SHMCTL, &a);
    if (r < 0) { errno = -r; return -1; }
    return 0;
}
