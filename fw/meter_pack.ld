/* Link script for one meter pack. PACK_ORG (the slot's address in the PSRAM code window) and SCRATCH_ORG (the on-chip meter scratch area) come from the
 * command line: -Wl,--defsym=PACK_ORG=0x... -Wl,--defsym=SCRATCH_ORG=0x...
 *   slot (PSRAM, executed and read through the uncached window):  .text, .rodata, the initial values of .data, then .pstate (state that is only
 *                                                                  touched a word at a time: history rings declared MTR_PSRAM)
 *   scratch (on-chip RAM, shared by whichever meter is active):    .data and .bss, the per-frame working state; the loader copies .data in and zeroes .bss */
ENTRY(mtr_pack_entry)
SECTIONS
{
    . = PACK_ORG;
    .text   : { KEEP(*(.text.mtr_pack_entry)) *(.text*) }
    /* The PSRAM instruction alias (0x24xx_xxxx) is fetch-only: a data load there is a bus error (docs/MMIO_ALLOCATION.md). So .rodata and .pstate are LINKED at the data
     * alias (0xA4xx_xxxx = alias + DATA_ALIAS, 0x80000000), the same bytes; code reaches them pc-relative, which wraps correctly in 32 bits. Host tests use DATA_ALIAS=0. */
    .rodata ADDR(.text) + SIZEOF(.text) + DATA_ALIAS : AT(ADDR(.text) + SIZEOF(.text)) { *(.rodata*) *(.srodata*) }
    _pack_data_lma = ALIGN(LOADADDR(.rodata) + SIZEOF(.rodata), 4);
    .data SCRATCH_ORG : AT(_pack_data_lma) { _pack_data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _pack_data_end = .; }
    .bss _pack_data_end (NOLOAD) : { _pack_bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _pack_bss_end = .; }
    _pack_load_end = _pack_data_lma + SIZEOF(.data);
    .pstate _pack_load_end + DATA_ALIAS (NOLOAD) : { _pack_pstate_start = .; *(.pstate*) . = ALIGN(4); _pack_pstate_end = .; }
    /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) *(.eh_frame*) }
}
