ENTRY(memprobe)
SECTIONS
{
	. = 0xA3800000;
	.text : { *(.text.memprobe) *(.text*) *(.rodata*) *(.data*) *(.sdata*) *(.bss*) *(.sbss*) }
	/DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.gnu.attributes) }
}
