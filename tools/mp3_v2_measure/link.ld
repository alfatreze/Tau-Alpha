ENTRY(_start)
SECTIONS {
	. = 0;
	.text : { *(.text._start) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.* .srodata .srodata.*) }
	.data : { *(.data .data.* .sdata .sdata.*) }
	__bss_start = .;
	.bss : { *(.bss .bss.* .sbss .sbss.* COMMON) }
	. = ALIGN(16);
	__bss_end = .;
	. = 0x00D00000;
	_stack_top = .;
}
