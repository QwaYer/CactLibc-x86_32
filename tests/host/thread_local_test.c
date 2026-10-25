/* thread_local host test: set_thread_area()'s request to the kernel.
 *
 * The registry half of thread_local.c (finding a thread's errno slot by stack
 * range) needs real threads and the real kernel, so it is not exercised here;
 * what is checked is that the %gs-base call carries the right procctl and base. */

#include <stdio.h>
#include <string.h>
#include <errno.h>

/* Layout-compatible with the libc's <sys/ldt.h> struct user_desc where it
 * matters: base_addr is the second word. */
struct user_desc {
    unsigned int entry_number;
    unsigned int base_addr;
    unsigned int limit;
    unsigned int flags;
};

int  cact_set_thread_area(struct user_desc *u_info);
int *cact_tl_errno_location(void);

extern int           cact_host_ctl_calls;
extern unsigned long cact_host_ctl_cmd;
extern unsigned int  cact_host_ctl_arg0;
extern int           cact_host_ctl_ret;

#define CACT_PROCCTL_SET_TLS 0x3218

static int checks, fails;

int main(void) {
    struct user_desc ud;
    memset(&ud, 0, sizeof ud);
    ud.entry_number = (unsigned int)-1;
    ud.base_addr    = 0x12345000;

    cact_host_ctl_calls = 0;
    cact_host_ctl_cmd = 0;
    cact_host_ctl_arg0 = 0;
    cact_host_ctl_ret = 0;

    int r = cact_set_thread_area(&ud);
    checks++;
    if (r != 0 || cact_host_ctl_calls != 1 ||
        cact_host_ctl_cmd != CACT_PROCCTL_SET_TLS ||
        cact_host_ctl_arg0 != 0x12345000u) {
        fails++;
        printf("FAIL set: r=%d calls=%d cmd=%#lx base=%#x\n",
               r, cact_host_ctl_calls, cact_host_ctl_cmd, cact_host_ctl_arg0);
    }

    /* Clearing the base is a base of 0, not an error. */
    cact_host_ctl_calls = 0;
    cact_host_ctl_arg0 = 0xdeadbeef;
    ud.base_addr = 0;
    cact_set_thread_area(&ud);
    checks++;
    if (cact_host_ctl_calls != 1 || cact_host_ctl_arg0 != 0) {
        fails++;
        printf("FAIL clear: calls=%d base=%#x\n", cact_host_ctl_calls, cact_host_ctl_arg0);
    }

    /* NULL is rejected in the libc, without reaching the kernel. */
    cact_host_ctl_calls = 0;
    *cact_tl_errno_location() = 0;
    int r2 = cact_set_thread_area(0);
    int e2 = *cact_tl_errno_location();
    checks++;
    if (r2 != -1 || e2 != EFAULT || cact_host_ctl_calls != 0) {
        fails++;
        printf("FAIL null: r=%d errno=%d calls=%d\n", r2, e2, cact_host_ctl_calls);
    }

    /* A kernel failure maps to -1. */
    cact_host_ctl_ret = -1;
    int r3 = cact_set_thread_area(&ud);
    checks++;
    if (r3 != -1) { fails++; printf("FAIL err propagation: r=%d\n", r3); }

    printf("thread_local: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
