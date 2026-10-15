ENTRY(setuptrace)
SECTIONS
{
	. = 0xA3800000;
	.text : { *(.text.setuptrace) *(.text*) *(.rodata*) *(.data*) *(.sdata*) *(.bss*) *(.sbss*) }
	/DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.gnu.attributes) }
}
