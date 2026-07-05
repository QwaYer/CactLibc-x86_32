#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <stdint.h>

/*
 * Номера и порядок должны совпадать с:
 *   CactKernel-x86_32/Cact/kernel/core/syscalls/syscalls.h
 * (syscall_num_t / SYSCALL_COUNT). При добавлении syscall в ядре — править здесь
 * и пересобрать libc.a, затем перелинковать cactsole, CactUserBins-x86_32 и прочие ELF.
 */

/* 0 — отладка */
#define SYS_PRINT           0

/* 1–7 — процесс */
#define SYS_GETPID          1
#define SYS_GETPPID         2
#define SYS_FORK            3
#define SYS_EXEC            4
#define SYS_EXIT            5
#define SYS_WAITPID         6
#define SYS_SLEEP           7

/* 8–11 — группы / сессии */
#define SYS_SETSID          8
#define SYS_SETPGID         9
#define SYS_GETPGID         10
#define SYS_GETPGRP         11

/* 12–20 — сигналы */
#define SYS_KILL            12
#define SYS_SIGNAL          13
#define SYS_SIGACTION       14
#define SYS_SIGPROCMASK     15
#define SYS_SIGRETURN       16   
#define SYS_SIGPENDING      17
#define SYS_SIGSUSPEND      18
#define SYS_ALARM           19
#define SYS_SETITIMER       20

/* 21–32 — файловые дескрипторы */
#define SYS_OPEN            21
#define SYS_READ            22
#define SYS_WRITE           23
#define SYS_CLOSE           24
#define SYS_LSEEK           25
#define SYS_IOCTL           26
#define SYS_FCNTL           27
#define SYS_DUP             28
#define SYS_DUP2            29
#define SYS_PIPE            30
#define SYS_SELECT          31
#define SYS_POLL            32

/* 33–43 — метаданные файлов */
#define SYS_STAT            33
#define SYS_FSTAT           34
#define SYS_ACCESS          35
#define SYS_CHMOD           36
#define SYS_CHOWN           37
#define SYS_UMASK           38
#define SYS_TRUNCATE        39
#define SYS_FTRUNCATE       40
#define SYS_SYNC            41
#define SYS_FSYNC           42
#define SYS_MKNOD           43

/* 44–56 — каталоги и пути */
#define SYS_CREATE          44
#define SYS_MKDIR           45
#define SYS_RMDIR           46
#define SYS_DELETE          47
#define SYS_UNLINK          48
#define SYS_RENAME          49
#define SYS_LINK            50
#define SYS_SYMLINK         51
#define SYS_READLINK        52
#define SYS_GETDENTS        53
#define SYS_CHDIR           54
#define SYS_GETCWD          55
#define SYS_CHROOT          56

/* 57–60 — системные операции */
#define SYS_MOUNT           57
#define SYS_UMOUNT          58
#define SYS_REBOOT          59
#define SYS_UNAME           60

/* 61–64 — память */
#define SYS_BRK             61
#define SYS_MMAP            62
#define SYS_MUNMAP          63
#define SYS_MPROTECT        64

/* 65–68 — IPC / SHM */
#define SYS_SHMGET          65
#define SYS_SHMAT           66
#define SYS_SHMDT           67
#define SYS_SHMCTL          68

/* 69–71 — время */
#define SYS_GETTIMEOFDAY    69
#define SYS_CLOCK_GETTIME   70
#define SYS_NANOSLEEP       71

/* 72–77 — идентификация */
#define SYS_GETUID          72
#define SYS_GETGID          73
#define SYS_GETEUID         74
#define SYS_GETEGID         75
#define SYS_SETUID          76
#define SYS_SETGID          77

/* 78–89 — сеть */
#define SYS_SOCKET          78
#define SYS_BIND            79
#define SYS_CONNECT         80
#define SYS_LISTEN          81
#define SYS_ACCEPT          82
#define SYS_SEND            83
#define SYS_RECV            84
#define SYS_SENDTO          85
#define SYS_RECVFROM        86
#define SYS_SHUTDOWN        87
#define SYS_SETSOCKOPT      88
#define SYS_GETSOCKOPT      89
#define SYS_PING_ECHO       90
#define SYS_NETCFG_SET      91
#define SYS_MODULE_LOAD     92
#define SYS_MODULE_UNLOAD   93
#define SYS_DNS_RESOLVE     94

/* Номера 0 … SYSCALL_COUNT-1 */
#define SYS_SYSCALL_COUNT   95

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
 *   ECX = return ESP    (loaded by the stub)
 *   EDX = return EIP    (loaded by the stub via call/pop)
 * Return value in EAX. ECX/EDX are clobbered (standard for x86 syscalls).
 *
 * The return-EIP is derived position-independently (call 1f / pop / add) so
 * the same code works in libc.a (static) and libc.so (PIC/PIE).
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