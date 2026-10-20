# Sound from our own DSP program, without MStar code

Status (2026-10-20): **works on HDMI**. A small program of our own for the SE
(Sound Effect) DSP of the Kronus SoC plays 16-bit PCM sent from Linux. It
needs **no MStar DSP code and no tables from the stock firmware**, so the
programs and binaries built on it can be published, e.g. fbDOOM with sound
(`bin/fbdoom_sound`). The analog (SCART) output is not done yet.

Everything below was measured on the board, most of it with the PC's
microphone in front of the TV speaker (recorded with `parecord`, spectrum
with numpy). The background (the two DSPs, the stock firmware path) is in
[audio.md](audio.md); the full Polish log of the experiments, with the wrong
turns, is in [pl/wlasny_dsp_dzwiek.md](pl/wlasny_dsp_dzwiek.md).

## Files

| File | What |
|---|---|
| `tools/dsp/pcm.s` | the SE program: takes the HDMI output interrupt and sends samples from a ring in DSP data memory |
| `tools/dsp/asm.py`, `tools/dsp/isa.py` | our assembler and instruction decoder for the SE (`python3 asm.py pcm.s -o pcm.hex`) |
| `tools/board/own_audio.h` | library with the same API as `mstar_audio.h`: `maudio_open(48000, 2)`, `maudio_write(samples, frames)`, `maudio_close()` |
| `tools/board/own_pcm_prog.h`, `gen_own_pcm.py` | the SE program as an array of words (generated from `pcm.hex`) |
| `tools/board/owntest.c` | test: 1 kHz, then 440/660 Hz through `own_audio.h` (`gcc -O2 -o owntest owntest.c -lm`) |
| `tools/board/dsppcm.c` | research player (raw s16le stereo 48 kHz or a test signal) with options to find the minimal init; needs `mstar_audio.h` and its generated headers |
| `tools/board/idmabench.c` | IDMA speed test (about 2.2 M words/s) |
| `tools/board/doomrun.sh` | runs fbDOOM on `tty2` for some seconds and gives the serial console back |
| `bin/fbdoom_sound` | fbDOOM built with `OWN_SOUND=1` (source: https://github.com/mztulip/fbDOOM) |

## How the HDMI output of the SE works

- The SE raises its **slot 0x08 interrupt** (vector `0x09`) once per HDMI
  output frame, 48000 times a second.
- **Acknowledge:** read `DM(0x817F)` and write the value back twice, **with
  an instruction between the two writes** (the stock code uses `AR = AR OR 0`
  and `AR = AR AND 0xFFFF`). Two back-to-back writes do not acknowledge it;
  the interrupt then floods the DSP (about 850 k/s, the other interrupts stop).
- **Sample:** `DM(0x8060)` = left, `DM(0x8061)` = right, each a **24-bit
  signed word**. 16-bit PCM has to be shifted left by 8:
  `SR = LSHIFT AR BY 8 (LO)`, `DM(0x8060) = SR0`. Writing a register loaded
  with a 16-bit value gives bits 15:0 with a zero top byte: about 42 dB too
  quiet, and it behaves as if unsigned. (This cost most of the time: it
  looked like offset-binary coding until a 24-bit table written over IDMA
  played at full level.)
- The DM-to-DRAM DMA of frames that the stock code runs (`0xA07E..0xA081`) is
  **not needed** for HDMI.

## Facts about the SE found on the way

- **Registers and the ALU are 24 bits wide.** 16-bit constants are zero
  extended: `AR = 0x9630` is the positive `0x009630`, so `IF LT` after
  `AR - 0x0680` is false. Code that relies on 16-bit signed compares has to
  be written differently.
- **Data memory:** `0x0000..0x2FFF` (12 K words), no mirrors; `0x3000` and
  above does not exist (reads 0).
- **IDMA from the MIPS into SE DM:** the address `| 0x8000` to `0x112A84`,
  then for each word four byte writes to `0x112A82/83` (byte 1, byte 2,
  byte 0, 0) and a check of the busy flag (`0x112A80` bit 4). The address
  increments by itself. It works while the DSP runs, at about 2.2 M words/s
  (48 kHz stereo needs 96 k/s).
- A 24-bit word written over IDMA and moved by `MR0 = DM(...)`,
  `DM(0x8060) = MR0` reaches the output with all 24 bits.

## The program, `tools/dsp/pcm.s`

- Ring in DM `0x1000..0x1FFF`: 2048 frames, L and R interleaved, words =
  `pcm << 8`, read through a circular DAG buffer (`I5 = 0x1000`, `M5 = 1`,
  `L5 = 0x1000`).
- Slot 0x08 handler: acknowledge, `MR0 = DM(I5, M5)` -> `0x8060`,
  `MR0 = DM(I5, M5)` -> `0x8061`, count frames in `DM(0x2B20)`.
- The main loop copies the frame counter to the mailbox `0x112DE0`
  (`DM(0xB070)`), so the MIPS knows how much has been played.
- All other interrupts: a plain `RTI`. The init repeats the register set-up
  of the stock SE init (IO space, `0xA0xx` block, ICNTL, IMASK) as found by
  tracing it.

## The Linux side, `tools/board/own_audio.h`

1. **Minimal audio block init** (found by automatic bisection, below).
2. The HDMI audio path as `hdmi_audio_on()` in `mstar_audio.h` (banks
   `0x1131`/`0x1133`) and the analog mute (GPIO44) released.
3. SE reset, the program loaded over IDMA (word 0 last: the SE starts on it),
   the ring cleared.
4. `maudio_write` converts the samples to 24-bit words and writes them over
   IDMA, staying 1536 frames (32 ms) ahead of the DSP.

Measured: 48000 frames/s, a clean 1 kHz tone (harmonics about 2 %, like the
stock path), 440/660 Hz changes without clicks. It works after a cold boot
and after the stock path (`audiotest3`) has been used.

## Minimal init

After a cold boot the stock init table (`MDrv_AUDIO_WriteInitTable`, 192
writes) is needed: without it there is silence and IDMA does not work. But
only **4** of its writes are. Found with ddmin (about 80 runs, each one a
reboot of the board, a test tone and the 1 kHz peak in the recording):

| RIU register | Write |
|---|---|
| `0x112C00` | `3`, then `0` (audio block reset, first) |
| `0x110C80` | `0x0E` (bank `0x110C`, probably clocks) |
| `0x112CA4` | `0xFF` |
| `0x112C48` | bit 7 = 1 |
| `0x112CB1` | bit 2 = 1 |
| `0x112D31`, `0x112B42` | power on: bit 1 = 0, `0` |

`mad2_setmeminfo_se` (the SE DRAM window) is not needed. `hdmi_audio_on`
is: without it the DSP counts frames but HDMI stays silent.

## fbDOOM

`make clean; make NOSDL=1 OWN_SOUND=1 MSTAR_AUDIO_DIR=.../tools/board` in
the [fbDOOM fork](https://github.com/mztulip/fbDOOM) builds the sound effects
on `own_audio.h`; the result is `bin/fbdoom_sound`. Recorded attract demo:
the loudness follows the same course as the build with MStar's code, a bit
louder; with `-nosound` the recording is flat. Run it as `bin/fbdoom`
(see [getting_started.md](getting_started.md)); fbDOOM puts the serial
console into raw mode, so after it type `^C` and `stty sane`
(`tools/board/doomrun.sh` does that).

## To do

- The analog (SCART) output: which SE ports / AUOUT go to the DAC.
- Rates other than 48 kHz (resampling on the MIPS or in the DSP).
- An ALSA driver in the kernel instead of `/dev/mem`.
