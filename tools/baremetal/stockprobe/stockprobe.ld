ENTRY(probe)
SECTIONS
{
	. = 0xA3800000;
	.text : { *(.text.probe) *(.text*) *(.rodata*) *(.data*) *(.sdata*) *(.bss*) *(.sbss*) }
	/DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.gnu.attributes) }
}
