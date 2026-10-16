/* Player 2 link map (192 KB RAM link only: Player 2 requires the 192 KB bitstream, spec section 13).
 * ROM and RAM are the same on-chip memory at 0x0. MMIO is at 0x8000_0000 and is reached through volatile pointers (platform/mmio.h).
 * Layout, low to high:  .text .rodata .data .bss | free | tag landing zone | compressed-data ring (APF DMA target) | stack
 * The ring and the tag zone are carved out explicitly (they are DMA targets: an overlap would corrupt audio, not fail cleanly). */
MEMORY { ram : ORIGIN = 0x00000000, LENGTH = 0x00030000 }
ENTRY(_start)
_stack_size = 0x1400;      /* 5 KB (was 6; the measured peaks are 0.9 to 1.1 KB and platform_stack_peak() keeps checking, so this still leaves 4x); the shipped measurement of the worst peak is 1,672 B (B-230) and platform_stack_peak() keeps checking */
_ring_size  = 0x6000;      /* 24 KB compressed-data ring (spec section 12.1: protected) */
_tag_size   = 0x1000;      /* 4 KB ID3 / tag landing zone */
_min_free   = 1024;
SECTIONS {
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
