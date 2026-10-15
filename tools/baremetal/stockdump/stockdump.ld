SECTIONS
{
	.text 0x80142468 : SUBALIGN(4) { *(.text) }
	/DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.gnu.attributes) }
}
