/*
 * Link script for the dlfcn host-test fixtures: a plugin image at a fixed
 * address, because CactOS maps every image at its link-time address and the
 * loader never applies a bias.  PLUGIN_BASE comes from --defsym so the same
 * script serves the good plugin, the one that cannot resolve a symbol, and the
 * one deliberately linked on top of clibc.so.
 */
ENTRY(plugin_entry)

SECTIONS {
    . = PLUGIN_BASE;

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
    .init_array : { *(.init_array) }
    /* For the fixtures that have a __thread of their own: these sections are
     * what the linker turns into the PT_TLS the loader has to place. */
    .tdata     : { *(.tdata .tdata.*) }
    .tbss      : { *(.tbss .tbss.*) }

    . = ALIGN(0x1000);
    .data.rel.ro : { *(.data.rel.ro .data.rel.ro.*) }
    .data      : { *(.data .data.*) }
    .got       : { *(.got) }
    .got.plt   : { *(.got.plt) }
    .bss       : { *(.bss .bss.*) *(COMMON) }
}
