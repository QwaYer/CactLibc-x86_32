/*
 * Linker script for CactOS userspace ld.so.
 *
 * Fixed base 0x18000000: above the main PIE images (0x08000000) and libc
 * (0x10000000), and *inside the low PMM reservation* (below RESERVED_END,
 * 0x20000000).  The kernel maps PT_INTERP interpreters exactly at their
 * link-time vaddrs (bias 0), and it reaches its own frames through the identity
 * map: a user mapping outside that reservation would replace the kernel's view
 * of a live frame in this process's page directory (see USER_FRAME_CEIL in
 * rust_mm/ffi.rs).  Keeping ld.so in the low region is what makes its fixed
 * base safe; the mmap window now starts at 0x80000000.
 */

ENTRY(_start)

SECTIONS {
    . = 0x18000000;

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
