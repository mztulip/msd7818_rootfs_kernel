ENTRY(bootsel)
SECTIONS
{
	. = 0x80F00400;		/* right after logofix in the same 64 KB flash block */
	.text : {
		*(.text.bootsel)
		*(.text*)
	}
	.rodata : { *(.rodata*) }
	.data : { *(.data*) *(.sdata*) }
	.bss : { *(.bss*) *(.sbss*) *(COMMON) }
	/DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes) }
}
