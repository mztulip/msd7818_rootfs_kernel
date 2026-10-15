# Sound from Linux on the WIWA HD95 (MStar MSD7818 / Kronus)

Status (October 2026): **sound works from Linux userspace**: test tones, WAV
files and fbDOOM sound effects, on HDMI and on the analog SCART output at the
same time. There is no ALSA driver yet; the programs drive the hardware
through `/dev/mem` (root).

This page explains how the audio path works and how it was found. The DSP
code it needs belongs to MStar and comes from the box's original firmware,
so it is **not published here**: you have to extract it from your own flash
dump.

## The hardware: two DSPs, not a sound card

The Kronus SoC has no simple I2S/DAC "sound card". Audio goes through two
24-bit DSPs inside the SoC:

| DSP | Role |
|---|---|
| **DEC** | decodes the stream: MPEG, MP3, AC3, AAC, … and **xPCM** (plain PCM) |
| **SE** (Sound Effect) | post-processing (volume, effects) and the outputs: HDMI and the DAC behind SCART |

Each DSP has its own program memory, which the CPU loads through an IDMA
port. Part of each DSP's data lives in DRAM. The CPU talks to the DSPs
through "mailbox" registers in the audio register bank (`0x112Dxx`). The
DSP code is modular: a "system" core that handles the mailboxes, plus a
decoder or effect module loaded next to it. The original firmware (eCos)
carries all of them in tables (`pau_info`: code, program and data segments
with their DSP addresses).

There is also a "DMA Reader" PCM path in the audio block. It looks like the
obvious way to play PCM, but the original firmware does not use it for file
playback, and on its own it produced only silence.

## How the original firmware plays a WAV file

The stock media player was traced with small bare-metal probes, loaded from
the bootloader into the running stock firmware, plus a Ghidra decompile of
the firmware:

1. At start-up (`MApi_AUDIO_Initialize`), the firmware runs these steps:
   - It loads the audio register init table.
   - It loads the SE **decoder 0x31 before the SE system code**, starts SE,
     waits for the "alive" byte `0xE3` and sends command `0xF3`.
   - It loads the DEC **decoder 0x03 before the DEC system code**, starts
     DEC, waits for `0xE3` and sends `0xF3`.
   - It "reloads" the SE code for analog TV sound (SIF). In this firmware
     the SIF table is empty, so only the handshake runs: command `0xF0` →
     answer `0x33`, command `0xF1` → answer `0x77`, command `0`.
     **Without this handshake SE stays at `0xF3` and nothing reaches the
     outputs.**
2. For a WAV file, it reloads the DEC decoder with **xPCM** (same
   `0xF0`/`0xF1` handshake) and writes the PCM parameters (format, channels,
   rate, bits) into DSP data memory.
3. It sets the decoder command to **PLAYFILE**.
4. The DSP asks for data. It increments a request counter and gives an
   offset and a size (4 KB) inside a 64 KB ring at the start of the DEC
   DRAM area. The player copies raw 16-bit PCM there and reports "data
   ready": a counter, command `0xE0` and a toggled interrupt bit.

## What had to be found on the way

- **Silence with a working decoder.** DEC was decoding (its play-time
  counter ran), but SE had never got the reload handshake, so it never
  processed anything.
- **Cold boot vs. warm reset.** After a short reset the DSP program memory
  still held the stock firmware's code, so missing steps went unnoticed.
  Only after the board was unplugged for 10 seconds did it show that DEC
  also needs its decoder (0x03) loaded before the system code.
- **The "alive" byte lies.** The DSP→CPU mailbox keeps `0xE3` across DSP
  resets and the CPU cannot clear it, so it does not prove that the DSP is
  running. Only the answers `0x33`/`0x77` do.
- **SE reads its settings at start.** The output and SE settings have to be
  written before the DSPs start. Otherwise the first run after a cold boot
  is silent and the second one plays.
- **Crackling.** The DSP asks for data whenever its ring has room, about
  twice as fast as it plays. Feeding every request immediately overwrote
  samples that had not been played yet. Pacing the writer to real time plus
  a small lead fixed it.

## Using it

The code is in the reverse engineering workspace (`tools/board/`):

| File | What |
|---|---|
| `mstar_audio.h` | header-only library: `maudio_open(rate, channels)`, `maudio_write(buf, frames)`, `maudio_close()` |
| `wavplay.c` | WAV player (16-bit PCM, mono or stereo, 8–48 kHz) |
| `audiotest3.c` | sine generator with register diagnostics |

The DSP code headers (`dsp_fw.h`, `dec03_fw.h`, `se31_fw.h`, `xpcm_fw.h`)
and the init table (`audio_init_table.h`) are generated from the stock
firmware image (the LZMA app at flash offset `0x8001C`, decompressed). They
are not part of any public repo.

On the board (it has gcc):
```sh
gcc -O2 -o wavplay wavplay.c && sync
./wavplay music.wav
```
MP3 files have to be converted on a PC for now:
```sh
ffmpeg -i music.mp3 -ac 2 -ar 44100 -sample_fmt s16 music.wav
```

fbDOOM with sound effects: the `i_mstarsound.c` module in
https://github.com/mztulip/fbDOOM (8-channel software mixer, about 64 ms of
latency). Build it with `make NOSDL=1 MSTAR_SOUND=1 MSTAR_AUDIO_DIR=…`. The
prebuilt `bin/fbdoom` in this repo is the version **without** sound, because
a binary with sound would contain MStar's DSP code.

## Ideas for later

- **Hardware MP3:** load the DEC MP3 decoder (`0x07`) instead of xPCM and
  feed the raw MP3 bytes on the DSP's requests, as the stock player does.
  The CPU would do almost nothing.
- Alternatively, decode MP3 on the CPU with `mpg123` (Buildroot package)
  piped into a raw PCM player, or with the single-file `minimp3`.
- **Doom music:** add an OPL (AdLib) emulator to fbDOOM's `i_oplmusic.c` and
  mix it in the same thread as the sound effects.
- A real ALSA driver: reserve the DSP memory in the device tree and move
  the init and the request handling into the kernel. The DSP interrupt
  could replace polling.
