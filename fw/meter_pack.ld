/* Link script for one meter pack. PACK_ORG (the slot's address) comes from the command line: -Wl,--defsym=PACK_ORG=0x... */
ENTRY(mtr_pack_entry)
SECTIONS
{
    . = PACK_ORG;
    .text   : { KEEP(*(.text.mtr_pack_entry)) *(.text*) }
    .rodata : { *(.rodata*) *(.srodata*) }
    .data   : { *(.data*) *(.sdata*) }
    . = ALIGN(4);
    _pack_load_end = .;
    .bss (NOLOAD) : { _pack_bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _pack_bss_end = .; }
    /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) *(.eh_frame*) }
}
