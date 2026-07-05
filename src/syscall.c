#include "syscall.h"

intptr_t syscall(int num, uintptr_t p1, uintptr_t p2, uintptr_t p3) {
    intptr_t ret;
    __asm__ volatile(
        "movl %%esp, %%ecx\n\t"
        "call 1f\n\t"
        "1:\n\t"
        "popl %%edx\n\t"
        "addl $(2f - 1b), %%edx\n\t"
        "sysenter\n\t"
        "2:\n\t"
        : "=a"(ret)
        : "a"(num), "b"(p1), "S"(p2), "D"(p3)
        : "ecx", "edx", "memory"
    );
    return ret;
}
