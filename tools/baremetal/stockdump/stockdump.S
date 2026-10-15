/*
 * stockdump - patch for the stock eCos firmware (decompressed.bin loaded at
 * 0x80000180 by MBoot): replaces the demo console "Help" handler (0x80142468,
 * 204 B) with a register dump printed through the firmware's own printf
 * (0x80244f74). The range table and formats live in the same 204 bytes
 * (the help strings at 0x8037F074 get overwritten at run time, e.g. by the
 * media player). The end marker is the line "00000000:".
 * Lines: "\nAAAAAAAA: vvvv vvvv ..." with 8 16-bit registers (CPU stride 4).
 */
	.set	noreorder
	.set	noat
	.equ	PRINTF, 0x80244f74

	.section .text
	.globl	start
start:
	addiu	$sp, $sp, -32
	sw	$ra, 28($sp)
	sw	$s0, 24($sp)
	sw	$s1, 20($sp)
	sw	$s2, 16($sp)
	lui	$s2, %hi(ranges)
	addiu	$s2, $s2, %lo(ranges)
rng:
	lw	$s0, 0($s2)
	beqz	$s0, done
	lw	$s1, 4($s2)
	addiu	$s2, $s2, 8
line:
	lui	$a0, %hi(fmt_addr)
	addiu	$a0, $a0, %lo(fmt_addr)
	jal	PRINTF
	move	$a1, $s0
val:
	lhu	$a1, 0($s0)
	lui	$a0, %hi(fmt_val)
	addiu	$a0, $a0, %lo(fmt_val)
	jal	PRINTF
	addiu	$s0, $s0, 4
	andi	$t0, $s0, 0x1f
	bnez	$t0, val
	nop
	bne	$s0, $s1, line
	nop
	b	rng
	nop
done:
	lui	$a0, %hi(fmt_addr)
	addiu	$a0, $a0, %lo(fmt_addr)
	jal	PRINTF
	move	$a1, $zero			/* end marker "00000000:" */
	lw	$ra, 28($sp)
	lw	$s0, 24($sp)
	lw	$s1, 20($sp)
	lw	$s2, 16($sp)
	jr	$ra
	addiu	$sp, $sp, 32

	.align	2
ranges:					/* CPU start, end (exclusive) */
	.word	0xbf225400, 0xbf226000	/* audio 0x112A00-0x112FFF */
	.word	0xbf226200, 0xbf226400	/* HDMI TX 0x1131xx */
	.word	0xbf226600, 0xbf226800	/* HDMI TX 0x1133xx */
	.word	0
fmt_addr:	.asciz	"\n%08x:"
fmt_val:	.asciz	" %04x"
