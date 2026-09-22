/* Phase G2 test firmware: hot code in BRAM, cold code linked for the PSRAM instruction alias (0x2480_0000) and stored in the ROM
 * image (LMA in BRAM); main() copies it into PSRAM through the data window (0xA480_0000) and then calls it. */
MEMORY {
	ram  : ORIGIN = 0x00000000, LENGTH = 0x00010000
	cold : ORIGIN = 0x24800000, LENGTH = 0x00100000
}
ENTRY(_start)
SECTIONS {
	.text : { KEEP(*(.text._start)) *(.text .text.*) } > ram
	.rodata : { *(.rodata .rodata.*) *(.srodata .srodata.*) } > ram
	.data : { *(.data .data.*) *(.sdata .sdata.*) . = ALIGN(32); } > ram      /* the cold image's LMA follows: word aligned */
	.cold : { _cold_vma = .; *(.cold .cold.*) . = ALIGN(4); _cold_end = .; } > cold AT > ram
	_cold_lma = LOADADDR(.cold);
	.bss : { __bss_start = .; *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) __bss_end = .; } > ram
	. = ALIGN(16);
	_heap_start = .;
	_stack_top = ORIGIN(ram) + LENGTH(ram);
}
