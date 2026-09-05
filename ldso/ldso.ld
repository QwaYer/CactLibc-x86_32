/*
 * Linker script for CactOS userspace ld.so.
 *
 * Fixed base 0x30000000: far above the main PIE images (0x08000000) and libc
 * (0x10000000), and below the generic mmap region (0x40000000).  The kernel
 * maps PT_INTERP interpreters exactly at their link-time vaddrs (bias 0).
 */

ENTRY(_start)

SECTIONS {
    . = 0x30000000;

    .text : {
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(0x1000);
    .data : {
        *(.data .data.*)
    }

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
}
