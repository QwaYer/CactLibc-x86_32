SECTIONS {
    . = 0x10000000;

    .dynamic   : { *(.dynamic) }
    .hash      : { *(.hash) }
    .gnu.hash  : { *(.gnu.hash) }
    .dynsym    : { *(.dynsym) }
    .dynstr    : { *(.dynstr) }
    .rel.dyn   : { *(.rel.dyn) }
    .rel.plt   : { *(.rel.plt) }

    .init      : { *(.init) }
    .plt       : { *(.plt) }
    .text      : { *(.text .text.*) }
    .fini      : { *(.fini) }

    .rodata    : { *(.rodata .rodata.*) }
    .eh_frame  : { *(.eh_frame) }

    . = ALIGN(0x1000);
    .data      : { *(.data .data.*) }
    .got       : { *(.got) }
    .got.plt   : { *(.got.plt) }
    .bss       : { *(.bss .bss.*) *(COMMON) }
}
