#ifndef CACT_DL_ELF_H
#define CACT_DL_ELF_H

/* ELF32 shapes and constants shared by the two loaders: ld.so (which maps the
 * program and its DT_NEEDED libraries) and the libc's dlopen() (which maps
 * whatever a program asks for later).  Both work on the same i386 images, and
 * CactOS maps every image at its link-time address, so no bias is ever applied
 * to a symbol value or a relocation addend. */

#include <stdint.h>

typedef struct {
    uint32_t  st_name;
    uint32_t  st_value;
    uint32_t  st_size;
    uint8_t   st_info;
    uint8_t   st_other;
    uint16_t  st_shndx;
} Elf32_Sym;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

typedef struct {
    int32_t d_tag;
    union {
        uint32_t d_val;
        uint32_t d_ptr;
    } d_un;
} Elf32_Dyn;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} Elf32_Rel;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;
} Elf32_Rela;

/* The program header table's own entry: everything we need is a fixed offset
 * except these three, which the loader reads directly. */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

#define ELF32_R_SYM(i)   ((i) >> 8)
#define ELF32_R_TYPE(i)  ((uint8_t)(i))
#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xf)

#define ELF32_EI_MAGIC0 0x7Fu        /* e_ident[0] */
#define ELF32_EI_MAGIC1 'E'
#define ELF32_EI_MAGIC2 'L'
#define ELF32_EI_MAGIC3 'F'

#define PT_LOAD      1
#define PT_DYNAMIC   2
#define PT_INTERP    3
#define PT_TLS       7

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
#define DT_INIT_ARRAY    25
#define DT_FINI_ARRAY    26
#define DT_INIT_ARRAYSZ  27
#define DT_FINI_ARRAYSZ  28
#define DT_GNU_HASH  0x6ffffef5

#define R_386_NONE      0
#define R_386_32        1
#define R_386_PC32      2
#define R_386_COPY      5
#define R_386_GLOB_DAT  6
#define R_386_JMP_SLOT  7
#define R_386_RELATIVE  8

/* i386 TLS relocations.  GNU ld reduces every static access to
 * R_386_TLS_TPOFF/_TPOFF32, whether the code is a program's own local-exec
 * access or the GOT slot an initial-exec access loads; the loaders resolve
 * those.  General-dynamic comes through as the pair R_386_TLS_DTPMOD32 /
 * _DTPOFF32 (the module id and the offset ___tls_get_addr() adds), which the
 * loaders also resolve.  The rest name a model neither loader implements, and
 * they are reported rather than resolved wrongly. */
#define R_386_TLS_TPOFF      14
#define R_386_TLS_IE         15
#define R_386_TLS_GOTIE      16
#define R_386_TLS_LE         17
#define R_386_TLS_GD         18
#define R_386_TLS_LDM        19
#define R_386_TLS_DTPMOD32   35
#define R_386_TLS_DTPOFF32   36
#define R_386_TLS_TPOFF32    37

#define SHN_UNDEF 0
#define STB_GLOBAL 1
#define STB_WEAK   2
#define STT_TLS    6

#endif
