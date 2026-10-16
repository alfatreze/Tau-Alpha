/* Player 2 link map (192 KB RAM link only: Player 2 requires the 192 KB bitstream, spec section 13).
 * ROM and RAM are the same on-chip memory at 0x0. MMIO is at 0x8000_0000 and is reached through volatile pointers (platform/mmio.h).
 * Layout, low to high:  .text .rodata .data .bss | free | tag landing zone | compressed-data ring (APF DMA target) | stack
 * The ring and the tag zone are carved out explicitly (they are DMA targets: an overlap would corrupt audio, not fail cleanly). */
MEMORY {
	ram   : ORIGIN = 0x00000000, LENGTH = 0x00030000
	/* Cold image (spec P5): read-only data in the PSRAM window at 8 MiB (loaded at boot from tau-cold.bin, data slot 6), code executed from the same PSRAM through the instruction alias
	 * (PSRAM byte offset = address - 0x2400_0000). One 1 MiB region carries both, data first. Same addresses as the old player and the bitstream's instruction fetch. */
	pcold : ORIGIN = 0xA4800000, LENGTH = 0x00100000
	pcode : ORIGIN = 0x24800000, LENGTH = 0x00100000
}
ENTRY(_start)
_stack_size = 0x1400;      /* 5 KB (was 6; the measured peaks are 0.9 to 1.1 KB and platform_stack_peak() keeps checking, so this still leaves 4x); the shipped measurement of the worst peak is 1,672 B (B-230) and platform_stack_peak() keeps checking */
_ring_size  = 0x6000;      /* 24 KB compressed-data ring (spec section 12.1: protected) */
_tag_size   = 0x1000;      /* 4 KB ID3 / tag landing zone */
_min_free   = 1024;
SECTIONS {
	/* Cold objects are chosen here by file name (build.sh compiles every source to its own .o). These statements come FIRST so the generic .text/.rodata rules below do not take them. */
	.cold_data : {
		_cold_start = .;
		*/nowplaying.o(.rodata .rodata.* .srodata .srodata.*)
		*/browser_view.o(.rodata .rodata.* .srodata .srodata.*)
		*/meter.o(.rodata .rodata.* .srodata .srodata.*)
		*/settings_view.o(.rodata .rodata.* .srodata .srodata.*)
		*/info_counters.o(.rodata .rodata.* .srodata .srodata.*)
		*/info_page.o(.rodata .rodata.* .srodata .srodata.*)
		*/report_page.o(.rodata .rodata.* .srodata .srodata.*)
		*/report_record.o(.rodata .rodata.* .srodata .srodata.*)
		*/art.o(.rodata .rodata.* .srodata .srodata.*)
		*/browser.o(.rodata .rodata.* .srodata .srodata.*)
		*/settings.o(.rodata .rodata.* .srodata .srodata.*)
		*/library.o(.rodata .rodata.* .srodata .srodata.*)
		*/halcyon.o(.rodata .rodata.* .srodata .srodata.*)
		*/halcyon_view.o(.rodata .rodata.* .srodata .srodata.*)
		*/assets.o(.rodata .rodata.* .srodata .srodata.*)
		*(.cold_data .cold_data.*)
		. = ALIGN(4);
		_cold_end = .;
	} > pcold
	.cold_text ORIGIN(pcode) + ALIGN(SIZEOF(.cold_data), 32) : AT(ORIGIN(pcode) + ALIGN(SIZEOF(.cold_data), 32)) {
		_cold_text_start = .;
		*/nowplaying.o(.text .text.*)
		*/browser_view.o(.text .text.*)
		*/meter.o(.text .text.*)
		*/settings_view.o(.text .text.*)
		*/info_counters.o(.text .text.*)
		*/info_page.o(.text .text.*)
		*/report_page.o(.text .text.*)
		*/report_record.o(.text .text.*)
		*/art.o(.text .text.*)
		*/browser.o(.text .text.*)
		*/settings.o(.text .text.*)
		*/library.o(.text .text.*)
		*/halcyon.o(.text .text.*)
		*/halcyon_view.o(.text .text.*)
		*/assets.o(.text .text.*)
		*(.cold_text .cold_text.*)
		. = ALIGN(4);
		_cold_text_end = .;
	} > pcode
	_cold_img_size = ALIGN(SIZEOF(.cold_data), 32) + SIZEOF(.cold_text);
	ASSERT(_cold_img_size <= 0x100000, "the cold image does not fit its 1 MiB region")
	.text   : { KEEP(*(.text._start)) *(.text .text.*) } > ram
	.rodata : { *(.rodata .rodata.*) *(.srodata .srodata.*) } > ram
	.data   : { *(.data .data.*) *(.sdata .sdata.*) } > ram
	.bss    : { __bss_start = .; *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) __bss_end = .; } > ram
	. = ALIGN(16);
	_image_end  = .;
	_stack_top  = ORIGIN(ram) + LENGTH(ram);
	_ring_start = _stack_top - _stack_size - _ring_size;
	_tag_start  = _ring_start - _tag_size;
	ASSERT(_image_end + _min_free <= _tag_start, "Player 2 image collides with the reserved DMA buffers (or leaves less than _min_free)")
}
