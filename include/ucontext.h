#ifndef _SYS_UCONTEXT_H
#define _SYS_UCONTEXT_H

#define REG_GS       0
#define REG_FS       1
#define REG_ES       2
#define REG_DS       3
#define REG_EDI      4
#define REG_ESI      5
#define REG_EBP      6
#define REG_ESP      7
#define REG_EBX      8
#define REG_EDX      9
#define REG_ECX     10
#define REG_EAX     11
#define REG_TRAPNO  12
#define REG_ERR     13
#define REG_EIP     14
#define REG_CS      15
#define REG_EFL     16
#define REG_UESP    17
#define REG_SS      18

typedef struct {
    unsigned long gregs[19];
} mcontext_t;

typedef struct {
    void *uc_link;
    unsigned long uc_flags;
    mcontext_t uc_mcontext;
} ucontext_t;

#endif
