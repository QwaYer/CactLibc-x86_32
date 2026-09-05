#ifndef CACT_LDSO_H
#define CACT_LDSO_H

#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t  s32;

/* ELF32 core structures (freestanding copy — no libc headers allowed) */
typedef struct {
    u32  st_name;
    u32  st_value;
    u32  st_size;
    u8   st_info;
    u8   st_other;
    u16  st_shndx;
} Elf32_Sym;

typedef struct {
    u32 p_type;
    u32 p_offset;
    u32 p_vaddr;
    u32 p_paddr;
    u32 p_filesz;
    u32 p_memsz;
    u32 p_flags;
    u32 p_align;
} Elf32_Phdr;

typedef struct {
    s32 d_tag;
    union {
        u32 d_val;
        u32 d_ptr;
    } d_un;
} Elf32_Dyn;

typedef struct {
    u32 r_offset;
    u32 r_info;
} Elf32_Rel;

typedef struct {
    u32 r_offset;
    u32 r_info;
    s32 r_addend;
} Elf32_Rela;

#define ELF32_R_SYM(i)   ((i) >> 8)
#define ELF32_R_TYPE(i)  ((u8)(i))
#define ELF32_ST_BIND(i) ((i) >> 4)

#define PT_LOAD      1
#define PT_DYNAMIC   2
#define PT_INTERP    3

#define DT_NULL      0
#define DT_NEEDED    1
#define DT_PLTRELSZ  2
#define DT_PLTGOT    3
#define DT_HASH      4
#define DT_STRTAB    5
#define DT_SYMTAB    6
#define DT_RELA      7
#define DT_RELASZ    8
#define DT_RELAENT   9
#define DT_STRSZ     10
#define DT_SYMENT    11
#define DT_INIT      12
#define DT_FINI      13
#define DT_SONAME    14
#define DT_REL       17
#define DT_RELSZ     18
#define DT_RELENT    19
#define DT_PLTREL    20
#define DT_JMPREL    23
#define DT_GNU_HASH  0x6ffffef5

#define R_386_NONE      0
#define R_386_32        1
#define R_386_PC32      2
#define R_386_GLOB_DAT  6
#define R_386_JMP_SLOT  7
#define R_386_RELATIVE  8
#define R_386_COPY      5

#define SHN_UNDEF 0
#define STB_WEAK  2

/* auxv tags (Linux i386) */
#define AT_NULL    0
#define AT_PHDR    3
#define AT_PHENT   4
#define AT_PHNUM   5
#define AT_PAGESZ  6
#define AT_BASE    7
#define AT_ENTRY   9

/* raw syscall numbers — must match CactKernel syscalls.h */
#define SYS_OPEN   0
#define SYS_CLOSE  1
#define SYS_READ   2
#define SYS_WRITE  3
#define SYS_EXIT   8
#define SYS_MMAP   11
#define SYS_MUNMAP 12

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

#define MAP_PRIVATE 0x02
#define MAP_FIXED   0x10
#define MAP_ANON    0x20
#define MAP_FAILED  ((void*)-1)

#define SO_PATH_MAX     256
#define LDSO_MAX_OBJS   8
#define LDSO_MAX_NEEDED 16
#define LDSO_SCRATCH    0x400000u   /* 4 MiB file scratch (cctkfs libs are << 1 MiB) */

long ldso_sc(unsigned num, unsigned a1, unsigned a2, unsigned a3);
void ldso_die(void);

#endif
