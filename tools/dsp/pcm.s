; pcm.s - our own program for the SE audio DSP of the MStar Kronus (MSD7818): plays 16-bit
; stereo PCM from Linux on HDMI, without any MStar DSP code (docs: own_dsp_audio.md).
; - The SE raises its slot 0x08 interrupt once per HDMI output frame (48 kHz). The handler
;   acknowledges it (DM(0x817F) read and written back twice, with an instruction in between)
;   and writes one L/R pair to DM(0x8060)/DM(0x8061) as 24-bit signed words (= PCM << 8).
; - The pairs come from a ring in DM 0x1000..0x1FFF (interleaved L, R; 2048 frames) that the
;   MIPS fills through IDMA (own_audio.h); the main loop copies the frame counter to the
;   mailbox 0x112DE0 (DM(0xB070)) so the MIPS knows how far the DSP has played.
; - The init below repeats the register set-up of the stock SE init (IO space, the 0xA0xx
;   block, ICNTL, IMASK), as found by tracing it; it is our code, not a copy of MStar's.
; DM is 0x0000..0x2FFF; registers and the ALU are 24 bits wide (16-bit constants are zero
; extended). Build: asm.py pcm.s -o pcm.hex ; gen_own_pcm.py pcm.hex own_pcm_prog.h
.org 0x00
        JUMP start
        RTI
        RTI
        RTI
.org 0x04
        ENA SEC_REG
        JUMP o04
        RTI
        RTI
.org 0x08
        ENA SEC_REG
        JUMP o08
        RTI
        RTI
.org 0x0C
        ENA SEC_REG
        JUMP cmd
        RTI
        RTI
.org 0x10
        ENA SEC_REG
        JUMP o10
        RTI
        RTI
.org 0x14
        ENA SEC_REG
        JUMP o14
        RTI
        RTI
.org 0x18
        ENA SEC_REG
        JUMP v19
        RTI
        RTI
.org 0x1C
        ENA SEC_REG
        JUMP sample
        RTI
        RTI
.org 0x20
        ENA SEC_REG
        JUMP o20
        RTI
        RTI
.org 0x24
        ENA SEC_REG
        JUMP o24
        RTI
        RTI
.org 0x28
        ENA SEC_REG
        JUMP tick
        RTI
        RTI
.org 0x2C
        ENA SEC_REG
        JUMP o2c
        RTI
        RTI

.org 0x30
start:  AR = 0
        ; the stock init zeroes 0xB038..0xB03F and 0xB070..0xB07F (DO loops with 07C000)
        DM(0xB038) = AR
        DM(0xB039) = AR
        DM(0xB03A) = AR
        DM(0xB03B) = AR
        DM(0xB03C) = AR
        DM(0xB03D) = AR
        DM(0xB03E) = AR
        DM(0xB03F) = AR
        DM(0xB070) = AR
        DM(0xB071) = AR
        DM(0xB072) = AR
        DM(0xB073) = AR
        DM(0xB074) = AR
        DM(0xB075) = AR
        DM(0xB076) = AR
        DM(0xB077) = AR
        DM(0xB078) = AR
        DM(0xB079) = AR
        DM(0xB07A) = AR
        DM(0xB07B) = AR
        DM(0xB07C) = AR
        DM(0xB07D) = AR
        DM(0xB07E) = AR
        DM(0xB07F) = AR
        DM(0x0100) = AR         ; (unused here, left from the test programs)
        DM(0x0101) = AR
        DM(0x0102) = AR
        DM(0x0103) = AR
        DM(0x0104) = AR
        DM(0x0105) = AR
        M0 = 0
        M1 = 1
        L0 = 0
        L1 = 0
        ; same register set-up as the stock init (0x84c6.., 0x850a.., 0xac54..0xac71)
        AR = MSTAT
        AR = AR OR 0x0080
        MSTAT = AR
        .word 0xa0001d          ; REG1[d] = 0x0001 (219x: IRPTL?)
        AR = 0
        IO(0x028) = AR
        AR = 0x0008
        IO(0x029) = AR
        AR = 0x000C
        IO(0x02A) = AR
        AR = 0x0020
        IO(0x02B) = AR
        AR = 0x0008
        IO(0x020) = AR
        AR = 0x000C
        IO(0x021) = AR
        AR = 0x003C
        IO(0x022) = AR
        IO(0x023) = AR
        AR = IO(0x01F)
        AY0 = 0xFC0F
        AR = AR AND AY0
        IO(0x01F) = AR
        AR = 0x8000
        IO(0x006) = AR
        .word 0x078000          ; PAGE 0x00
        .word 0xa00074          ; stock 0x8512: PAGE + REG1 load = REG3, ICNTL = 0x0007 (218x: IRQ0-2
                                ; edge-triggered; with 0x0008 = level, slot 0x08 floods; board reads 0x0008)
        AR = 0x1000
        IO(0x007) = AR
        AX0 = 0x00FE
        IO(0x01B) = AX0
        AX0 = 0x009C
        IO(0x01C) = AX0
        IO(0x01D) = AX0
        ENA TIMER
        ; 0xA0xx register block (DMA / audio interface?), same writes and order as the stock
        ; init (emulator log of sim.boot: 0x8524..0x853d, 0xa8d7..0xa8de)
        AR = 0x0010
        DM(0xA07A) = AR
        AR = 0x0010
        DM(0xA07E) = AR
        AR = 0x0010
        DM(0xA082) = AR
        AR = 0x0010
        DM(0xA086) = AR
        AR = 0x0010
        DM(0xA08A) = AR
        AR = 0x0010
        DM(0xA076) = AR
        AR = 0x0010
        DM(0xA0E0) = AR
        AR = 0x000E
        DM(0xA082) = AR
        AR = 0x0180
        DM(0xA085) = AR
        AR = 0x8180
        DM(0xA084) = AR
        AR = 0xF000
        DM(0xA083) = AR
        AR = 0x0020
        DM(0xA082) = AR
        AR = 0
        DM(0xB07A) = AR
        DM(0xB03A) = AR
        DM(0xB03B) = AR
        AR = 0xFC00
        DM(0xB201) = AR
        AR = 0x7200
        DM(0xA082) = AR
        AR = 0x8000
        DM(0xA083) = AR
        AR = 0x0330
        DM(0xA084) = AR
        AR = 0x002E
        DM(0xA082) = AR
        AR = 0x7398
        DM(0xA082) = AR
        AR = 0x8440
        DM(0xA083) = AR
        AR = 0x0360
        DM(0xA084) = AR
        AR = 0x002E
        DM(0xA082) = AR
        AR = 0x8000
        IO(0x006) = AR
        ; stock 0xac93..0xaca3
        AR = 0x0010
        DM(0xA020) = AR
        AR = 0x0000
        DM(0xA020) = AR
        AR = 0x0010
        DM(0xA020) = AR
        AR = 0x0200
        DM(0xA020) = AR
        AR = 0x0028
        DM(0xA001) = AR
        AR = 0x1FC0
        DM(0xA021) = AR         ; 0xA0xx: another register block (stock 0xac56..0xac5b)
        DM(0xA022) = AR
        AR = DM(0xA020)
        AR = AR OR 0x0002
        DM(0xA020) = AR
        .word 0xa00ffc          ; REG1[c] = 0x00ff (219x: IMASK)
        ENA TIMER
        AR = IO(0x007)
        AR = AR OR 0x0040
        IO(0x007) = AR
        ; stock 0xac78..0xac8f (after IO(0x007) |= 0x40)
        AR = 0x0010
        DM(0xA062) = AR
        AR = 0x0000
        DM(0xA062) = AR
        AR = 0x0048
        DM(0xA062) = AR
        AR = 0x0010
        DM(0xA072) = AR
        AR = 0x0000
        DM(0xA072) = AR
        AR = 0x0088
        DM(0xA072) = AR
        AR = 0x0010
        DM(0xA06A) = AR
        AR = 0x0000
        DM(0xA06A) = AR
        AR = 0
        DM(0xB202) = AR
        DM(0xB203) = AR
        AR = 0xFFFF
        DM(0xB201) = AR
        AR = 0x05A1
        DM(0xB07F) = AR         ; 0x2DFE: init done
        I5 = 0x1000             ; ring read pointer (circular, 0x1000 words)
        M5 = 1
        L5 = 0x1000
        AR = 0
        DM(0x2B20) = AR         ; frame counter
        AR = 0x0109             ; IMASK: timer (0x28), samples (0x1C), HDMI output (0x08)
        IMASK = AR
main:   AR = DM(0x2B20)         ; frame counter -> mailbox 0x2DE0 for the MIPS
        DM(0xB070) = AR
        JUMP main

o08:    AR = DM(0x817F)         ; acknowledge the HDMI output request: read value written back
        .word 0x23aaef          ; twice WITH an instruction in between (AR OR 0, AR AND 0xffff),
        DM(0x817F) = AR         ; back-to-back writes do not acknowledge (bisect08b.sh)
        .word 0x239aef
        DM(0x817F) = AR
        MR0 = DM(I5, M5)        ; 24-bit L sample (written by the MIPS as pcm << 8)
        DM(0x8060) = MR0
        MR0 = DM(I5, M5)        ; 24-bit R sample
        DM(0x8061) = MR0
        AX0 = DM(0x2B20)
        AR = AX0 + 1
        DM(0x2B20) = AR
        RTI

sample: RTI                     ; slot 0x1C (48 kHz): not used
cmd:    RTI
v19:    RTI
tick:   RTI
o04:    RTI
o10:    RTI
o14:    RTI
o20:    RTI
o24:    RTI
o2c:    RTI
