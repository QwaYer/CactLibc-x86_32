#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <stdint.h>

/*
 * Минимальный syscall ABI (15 номеров). Номера и порядок должны совпадать с:
 *   CactKernel-x86_32/Cact/kernel/core/syscalls/syscalls.h
 * (syscall_num_t / SYSCALL_COUNT). Всё остальное — через VFS-узлы: см.
 * ioctl_abi.h и relay-слой в src/nodeio.c.
 */

#define SYS_OPEN      0
#define SYS_CLOSE     1
#define SYS_READ      2
#define SYS_WRITE     3
#define SYS_IOCTL     4
#define SYS_POLL      5
#define SYS_FORK      6
#define SYS_EXEC      7
#define SYS_EXIT      8
#define SYS_WAITPID   9
#define SYS_BRK       10
#define SYS_MMAP      11
#define SYS_MUNMAP    12
#define SYS_MPROTECT  13
#define SYS_SIGRETURN 14

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

intptr_t syscall(int num, uintptr_t p1, uintptr_t p2, uintptr_t p3);

/*
 * Syscall ABI (SYSENTER/SYSEXIT):
 *   EAX = syscall number
 *   EBX = arg1
 *   ESI = arg2          (ECX is stolen by the CPU as return-ESP)
 *   EDI = arg3          (EDX is stolen by the CPU as return-EIP)
 * Return value in EAX.
 */
static inline intptr_t __syscall0(int num) {
    intptr_t ret;
    __asm__ volatile (
        "movl %%esp, %%ecx\n\t"
        "call 1f\n\t"
        "1:\n\t"
        "popl %%edx\n\t"
        "addl $(2f - 1b), %%edx\n\t"
        "sysenter\n\t"
        "2:\n\t"
        : "=a"(ret)
        : "a"(num)
        : "ecx", "edx", "memory"
    );
    return ret;
}

static inline intptr_t __syscall1(int num, uintptr_t a1) {
    intptr_t ret;
    __asm__ volatile (
        "movl %%esp, %%ecx\n\t"
        "call 1f\n\t"
        "1:\n\t"
        "popl %%edx\n\t"
        "addl $(2f - 1b), %%edx\n\t"
        "sysenter\n\t"
        "2:\n\t"
        : "=a"(ret)
        : "a"(num), "b"(a1)
        : "ecx", "edx", "memory"
    );
    return ret;
}

static inline intptr_t __syscall2(int num, uintptr_t a1, uintptr_t a2) {
    intptr_t ret;
    __asm__ volatile (
        "movl %%esp, %%ecx\n\t"
        "call 1f\n\t"
        "1:\n\t"
        "popl %%edx\n\t"
        "addl $(2f - 1b), %%edx\n\t"
        "sysenter\n\t"
        "2:\n\t"
        : "=a"(ret)
        : "a"(num), "b"(a1), "S"(a2)
        : "ecx", "edx", "memory"
    );
    return ret;
}

static inline intptr_t __syscall3(int num, uintptr_t a1, uintptr_t a2, uintptr_t a3) {
    intptr_t ret;
    __asm__ volatile (
        "movl %%esp, %%ecx\n\t"
        "call 1f\n\t"
        "1:\n\t"
        "popl %%edx\n\t"
        "addl $(2f - 1b), %%edx\n\t"
        "sysenter\n\t"
        "2:\n\t"
        : "=a"(ret)
        : "a"(num), "b"(a1), "S"(a2), "D"(a3)
        : "ecx", "edx", "memory"
    );
    return ret;
}

#endif
