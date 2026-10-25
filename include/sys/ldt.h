#ifndef _SYS_LDT_H
#define _SYS_LDT_H

/* CactOS keeps no LDT: the kernel gives every task one descriptor whose base is
 * its `%gs` thread pointer (see CACT_PROCCTL_SET_TLS).  set_thread_area()
 * installs `base_addr` as that base directly, so the function is source-
 * compatible with the Linux i386 one while the rest of the structure has
 * nothing to describe and is ignored (limit, the segment flags and the chosen
 * entry_number all have no effect; (unsigned)-1 for entry_number — "pick one"
 * — is equally a no-op). */

struct user_desc {
    unsigned int entry_number;
    unsigned int base_addr;
    unsigned int limit;
    unsigned int seg_32bit:1;
    unsigned int contents:2;
    unsigned int read_exec_only:1;
    unsigned int limit_in_pages:1;
    unsigned int seg_not_present:1;
    unsigned int useable:1;
};

/* Set the calling task's %gs base to u_info->base_addr.  0 clears it. */
int set_thread_area(struct user_desc *u_info);

#endif
