/* Link script for one meter pack. PACK_ORG (the slot's address in the PSRAM code window), DATA_ALIAS and SCRATCH_ORG (the on-chip meter scratch area) come from the command line:
 * -Wl,--defsym=PACK_ORG=0x... -Wl,--defsym=DATA_ALIAS=0x... -Wl,--defsym=SCRATCH_ORG=0x...
 *   slot (PSRAM): code is EXECUTED at the 0x24xx_xxxx instruction alias; .rodata and .pstate are LINKED at the 0xA4xx_xxxx data alias (DATA_ALIAS), because the instruction alias is fetch-only
 *                 (a data load from it is a bus error): .text, .rodata, the initial values of .data, the descriptor (.mtr_desc), then .pstate (state touched a word at a time: history rings declared in .pstate)
 *   scratch (on-chip RAM, shared by whichever meter is active): .data and .bss, the per-frame working state; the loader copies .data in and zeroes .bss */
ENTRY(mtr_pack_entry)
SECTIONS
{
    . = PACK_ORG;
    .text   : { KEEP(*(.text.mtr_pack_entry)) *(.text*) }
    .rodata ADDR(.text) + SIZEOF(.text) + DATA_ALIAS : AT(ADDR(.text) + SIZEOF(.text)) { *(.rodata*) *(.srodata*) }
    _pack_data_lma = ALIGN(LOADADDR(.rodata) + SIZEOF(.rodata), 4);
    .data SCRATCH_ORG : AT(_pack_data_lma) { _pack_data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _pack_data_end = .; }
    .bss _pack_data_end (NOLOAD) : { _pack_bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _pack_bss_end = .; }
    _pack_desc_lma = ALIGN(_pack_data_lma + SIZEOF(.data), 4);
    .mtr_desc _pack_desc_lma + DATA_ALIAS : AT(_pack_desc_lma) { KEEP(*(.mtr_desc)) }       /* the meter descriptor (tools/player2/meter_desc.py), last in the image: the loader finds it from its trailer */
    _pack_load_end = _pack_desc_lma + SIZEOF(.mtr_desc);
    .pstate _pack_load_end + DATA_ALIAS (NOLOAD) : { _pack_pstate_start = .; *(.pstate*) . = ALIGN(4); _pack_pstate_end = .; }
    /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) *(.eh_frame*) }
}
