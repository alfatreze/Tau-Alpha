/* Firmware link map.
 * RAM is 256 KB of on-chip BRAM at 0x0000_0000 (cached) by default -- 192 KB
 * on the opt-in RAM-shrink RTL (`TAU_RAM_192K`, docs/PHASE_F_SPEC.md section 4,
 * docs/AUDIT_TRAIL.md B-223..B-235). MMIO lives at 0x8000_0000 and an uncached
 * alias of this same RAM at 0xC000_0000 -- neither is in the link map;
 * firmware reaches them through volatile pointers.
 *
 * Layout, low -> high:
 *   .text/.rodata/.data/.bss
 *   heap            (Helix mallocs its 23816-byte decoder instance here)
 *   MP3 ring buffer (32 KB, RESERVED -- APF DMAs into it)
 *   stack           (16 KB)
 *
 * The ring is carved out explicitly rather than hardcoded at some address the
 * heap might one day grow into: it is a DMA target, so a collision would show
 * up as corrupted audio or a wild decoder crash, not a clean allocation failure.
 */

MEMORY {
	ram : ORIGIN = 0x00000000, LENGTH = 0x00040000   /* 256 KB, the true physical maximum on every
	                                                   * bitstream -- see _ram_limit below for the
	                                                   * artificial, opt-in 192 KB ceiling instead
	                                                   * of shrinking this region itself. */
	/* A-103: uncached CPU window onto SDRAM (physical 1 MiB up). Only the playlist
	 * buffers, 13,312 B, and only in TAU_PL_SDRAM builds; the section is empty
	 * otherwise. NOLOAD: never in the ROM image, contents undefined at boot. */
	sdram : ORIGIN = 0xA0100000, LENGTH = 0x00003400
	/* P5: uncached CPU window onto PSRAM (0xA4000000). Only the album-art accumulator,
	 * 11,040 B, and only in TAU_ART_PSRAM builds; empty otherwise. NOLOAD. */
	psram : ORIGIN = 0xA4000000, LENGTH = 0x00004000   /* art accumulator: 15,360 B at ART_IMG 128 (was 0x2C00 at 92); the library image starts at +0x10000 */
	/* Phase G1: cold READ-ONLY DATA (meter previews, help text) lives in PSRAM at 8 MiB and is loaded at boot from the
	 * data slot file tau-cold.bin (tools/pack_cold.py). Never in the ROM image (build.sh strips it with objcopy -R).
	 * Empty unless TAU_COLD=1. 1 MiB reserved; the library index (0x010000..0x40FFFF) and queue areas are below. */
	pcold : ORIGIN = 0xA4800000, LENGTH = 0x00100000
	/* Phase G2/G4: cold CODE, executed from PSRAM through the instruction alias (PSRAM byte offset = address - 0x2400_0000).
	 * It sits directly after the cold data in the same 1 MiB region, so ONE image (tau-cold.bin) carries both. */
	pcode : ORIGIN = 0x24800000, LENGTH = 0x00100000
}

ENTRY(_start)

/* Minimum token heap. 1 KB for every product build. Developer stress builds pass
 * -Wl,--defsym=_min_heap=128: malloc is the fixed arena in alloc.c and nothing
 * links printf, so _sbrk is only a fallback, and the stress HUD and pump alone
 * exceed the product's 2.4 KB slack. PROVIDE never overrides a --defsym. */
PROVIDE(_min_heap = 1024);

_stack_size = 0x4000;    /* 16 KB */
/* 24 KB, down from 32.
 *
 * The ring sits ABOVE the image, so shrinking it moves _tag_start up and
 * hands 8 KB straight to the firmware -- which is what made room for a second
 * decoder's code. Both decoders' text must be resident; only their buffers
 * can swap.
 *
 * Affordable now in a way it was not when 32 KB was chosen. The ring exists to
 * absorb SD stalls, and the card measured 736 KB/s sequential against 40 KB/s
 * of MP3 demand -- an 18x margin. 24 KB is still ~0.6 s at 320 kbps, and
 * ~0.23 s at the 105 KB/s the CD-rip FLAC on the test card needs. Watch the
 * underrun flag if either ever looks marginal. */
_ring_size  = 0x6000;    /* 24 KB -- ~0.6 s at 320 kbps */
_tag_size   = 0x1000;    /* 4 KB  -- ID3 re-probe landing zone */

SECTIONS {
	.text : {
		KEEP(*(.text._start))
		*(.text .text.*)
	} > ram

	.rodata : { *(.rodata .rodata.*) *(.srodata .srodata.*) } > ram
	.data   : { *(.data .data.*) *(.sdata .sdata.*) } > ram

	.cold_data : {
		_cold_start = .;
		*(.cold_data .cold_data.*)
		. = ALIGN(4);
		_cold_end = .;
	} > pcold

	.cold_text ORIGIN(pcode) + ALIGN(SIZEOF(.cold_data), 32) : AT(ORIGIN(pcode) + ALIGN(SIZEOF(.cold_data), 32)) {
		_cold_text_start = .;
		*(.cold_text .cold_text.*)
		. = ALIGN(4);
		_cold_text_end = .;
	} > pcode
	/* The whole cold image as it lies in PSRAM: data, padding to 32 bytes, code. */
	_cold_img_size = ALIGN(SIZEOF(.cold_data), 32) + SIZEOF(.cold_text);

	.sdram (NOLOAD) : { *(.sdram .sdram.*) } > sdram
	.psram (NOLOAD) : { *(.psram .psram.*) } > psram

	.bss : {
		__bss_start = .;
		*(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
		__bss_end = .;
	} > ram

	. = ALIGN(16);
	_heap_start = .;

	/* RAM_192K: linked with `-Wl,--defsym=RAM_192K=1` (fw/build.sh's opt-in
	 * override, same style as SDRAM_BUSY) caps the firmware's own usable
	 * ceiling at 192 KB instead of the full 256 KB region -- the RAM-shrink
	 * RTL's real benefit (docs/PHASE_F_SPEC.md section 4, timing-closed
	 * B-235, not yet card-tested). Deliberately NOT done by shrinking the
	 * `ram` MEMORY region's own LENGTH above: `DEFINED()` inside a MEMORY
	 * block's LENGTH expression was tried first and silently had NO effect
	 * in this toolchain (`riscv-none-elf-ld` from `xpack-riscv-none-elf-gcc-
	 * 15.2.0`) -- both settings linked identical images with `_stack_top`
	 * unchanged, caught only by checking the actual `nm` symbol values
	 * after the build, not by any error. `DEFINED()` in an ordinary symbol
	 * assignment (here) is the standard, well-supported form instead.
	 *
	 * A 192 KB-linked image is always safe to run on the OLD 256 KB
	 * bitstream too (a smaller link fitting in less space than the physical
	 * RAM provides is never a problem, just wastes the unused 64 KB) -- the
	 * dangerous direction would be a 256 KB-linked image running on a NEW
	 * 192 KB bitstream, which this build never produces, so no runtime
	 * feature-detection is needed here the way PSRAM/blit-engine features
	 * need BLIT_READY()/COLD_READY() style checks for the reverse case (old
	 * firmware assuming a capability a bitstream lacks). */
	_ram_limit  = DEFINED(RAM_192K) ? (ORIGIN(ram) + 0x00030000) : (ORIGIN(ram) + LENGTH(ram));
	_stack_top  = _ram_limit;
	_ring_start = _stack_top - _stack_size - _ring_size;

	/* Separate landing zone for the periodic ID3 re-probe. It must NOT share
	 * the ring: the ring holds audio the decoder is actively consuming, and a
	 * probe re-reading offset 0 into it would destroy playback. Carved here
	 * for the same reason as the ring -- it is a DMA target, so an overlap
	 * would appear as corrupt audio rather than a clean allocation failure. */
	_tag_start  = _ring_start - _tag_size;
	_heap_end   = _tag_start;

	/* Fail the link rather than silently overlapping if the image ever grows
	 * past the reserved regions. */
	ASSERT(_heap_start < _tag_start, "firmware image collides with reserved DMA buffers")

	/* The gap between BSS and the DMA buffers IS the heap, and Helix mallocs
	 * 23816 bytes of it for the decoder instance -- so BSS fitting is not the
	 * same as the core working. Growing a static buffer into that space once
	 * left a 15 KB heap: BSS still fit, the linker was satisfied, the build
	 * passed, and MP3InitDecoder() would have returned 0 with no audio at
	 * all. Caught by checking the number, not by any tool.
	 *
	 * 24576 rather than 23816 exactly, to cover malloc per-chunk headers
	 * across the eight allocations and leave a little room. */
	/* 1 KB, not 24. The decoder no longer allocates from here: fw/alloc.c
	 * overrides malloc with a fixed arena, because newlib consumed the whole
	 * 33936-byte heap to serve 23816 bytes of Helix structs and refused to
	 * work at 25232 -- an overhead that could not be sized against.
	 *
	 * The guard that matters now is ARENA_BYTES in alloc.c, which is exact.
	 * This one only keeps a token heap so a stray newlib path that reaches
	 * _sbrk has somewhere to go rather than returning -1 in a corner. */
	ASSERT(_heap_end - _heap_start >= _min_heap, "no room left for even a token heap")
}
