# Linux on the WIWA HD95 (MStar MSD7818)

**New here? Start with [docs/getting_started.md](docs/getting_started.md)**:
from an unmodified box to Linux, fbDOOM and sound, step by step.
Login on the rootfs: `root`, no password (set one with `passwd`).

**Linux in the box's own flash, no pendrive needed:** ready images in `flash/`,
written from the MBoot prompt with a FAT32 pendrive, see
[Flash system](#flash-system-linux-in-the-boxs-own-flash). How to build them
yourself (kernel, rootfs, fbDOOM): [build_manual.md](build_manual.md).

Kernel source: https://github.com/mztulip/linux_msd781x (branch `mstar_v6_7_rebase`),
a fork of https://github.com/maniekx86/linux_msd781x/commits/mstar_v6_7_rebase/
with the WIWA HD95 (MSD7818) changes listed below. Two configurations:
`wiwa_hd95_defconfig` (rootfs on the USB stick, DTB `msd7816-test.dtb`; the
`vmlinux.bin` here) and `wiwa_hd95_mini_defconfig` (a small kernel plus a
squashfs rootfs that fit the box's own 4 MB SPI flash instead of the original
firmware, DTB `msd7816-test-flash.dtb`). The kernel
[README](https://github.com/mztulip/linux_msd781x/blob/mstar_v6_7_rebase/README.md)
describes both, with the flash layout and build commands.

Compiled using:


```
mipsel-linux-gnu-gcc --version
mipsel-linux-gnu-gcc (GCC) 12.2.0
```

## Contents

| File | What |
|---|---|
| `vmlinux.bin` | kernel 6.7.0-rc6 with appended DTB, entry point **`0x805e9be4`**; I2C and front panel drivers built in |
| `logofix/logofix.bin` | MBoot helper that fixes the boot logo colours, run before the kernel (source and `build.sh` next to it) |
| `rootfs_gcc.7z` | Buildroot rootfs (ext2) with gcc |
| `docs/framebuffer_driver.md` | how the double buffered framebuffer driver was written, step by step |
| `docs/display_flicker_miu.md` | why the picture glitched under memory load (fbDOOM) and the MIU priority fix |
| `tools/` | our sources: programs built on the board (`tools/board/`: framebuffer tests, audio library and `wavplay`, demod mailbox, RF scan…), bare-metal MBoot helpers and stock firmware probes (`tools/baremetal/`), serial console scripts, Ghidra scripts. See [Tools](#tools) |
| `docs/pl/` | the full Polish working notes of the project (registers, I2C, tuner, demodulator, GOP, audio, stock firmware probes, …); index: [docs/pl/README.md](docs/pl/README.md) |
| `docs/rf_scanner.md` | **rfscan**: RF spectrum analyzer (44–880 MHz) using the box's tuner and demodulator, drawn on the TV; example scans in `docs/rfscan_data/` |
| `docs/getting_started.md` | step-by-step guide: backup, pendrive, boot, I2C and front panel, apps, fbDOOM, sound |
| `docs/audio.md` | sound from Linux: how the two audio DSPs are started and fed (WAV player, fbDOOM sound effects) |
| `docs/own_dsp_audio.md` | sound on HDMI from **our own SE DSP program**, without MStar code: sample format, interrupt, `own_audio.h`, 4-register init |
| `examples/bwtest.c`, `examples/randhog.c` | checkerboard + memory load tests that reproduce/verify the glitches |
| `bin/fbdoom` | fbDOOM with double buffering, fast drawing and serial-console controls (source: https://github.com/mztulip/fbDOOM) |
| `bin/fbdoom_sound` | the same fbDOOM **with sound effects** through our own DSP program (`OWN_SOUND=1`, no MStar code; HDMI). Older build: after `^C` it leaves the last audio looping on HDMI, see `docs/own_dsp_audio.md` |
| `bin/fbdoom_music` | fbDOOM with sound effects **and music** (OPL2 emulation, `opl2.c`), static (musl), runs on both rootfs; the one in the flash system |
| `flash/` | **flash system images**: `kernel.img` (mini kernel, LZMA), `logofix.img`, `rootfs.img` (squashfs: BusyBox, `fbdoom_music`, Freedoom E1M1), `md5sums.txt`. See [Flash system](#flash-system-linux-in-the-boxs-own-flash) |
| `build_manual.md` | how to build the flash images from source: mini kernel, Buildroot rootfs, fbDOOM, IWAD |
| `rootfs_mini/` | Buildroot config, overlay and post-build script of the flash rootfs |
| `tools/flash/mkflash.py` | packs the kernel for MBoot's `LzmaDec` (LZMA + CRC32) and pads the images to their partitions |
| `examples/fbflip.c` | double buffering example and test for `/dev/fb0` (build on the board: `gcc -O2 -o fbflip fbflip.c`) |
| `bootlogo/` | custom MBoot boot logo: Linux logo, `png2logo.py` (any picture -> logo), flashing guide. See [bootlogo/README.md](bootlogo/README.md) |

## Sound

Sound works from Linux userspace (test tones, WAV files, fbDOOM sound
effects; HDMI and SCART at the same time) by driving the SoC's two audio
DSPs with the DSP code of the original firmware. The DSP code is MStar's
and is not included. See [docs/audio.md](docs/audio.md).

**New: sound without MStar code.** Our own small program for the SE DSP
(`tools/dsp/pcm.s`) plays PCM from Linux on HDMI, with `tools/board/own_audio.h`
(same API as `mstar_audio.h`) and a 4-register init instead of the stock
tables. fbDOOM with sound built on it is in `bin/fbdoom_sound`. See
[docs/own_dsp_audio.md](docs/own_dsp_audio.md). The SCART output is not done
with it yet.

## What is new in this kernel

- **Double buffered framebuffer with vsync** (`mstar-gopfb`, replaces
  simpledrm). `/dev/fb0` is 1920x1080 RGB565 with `yres_virtual` = 2160:
  draw into the hidden half, show it with `FBIOPAN_DISPLAY` (switched at
  vsync) and wait with `FBIO_WAITFORVSYNC` (the HDMI output runs at 50 Hz).
  No more tearing. Example: `examples/fbflip.c`, full description:
  [docs/framebuffer_driver.md](docs/framebuffer_driver.md). Source:
  [mstar-gopfb.c](https://github.com/mztulip/linux_msd781x/blob/mstar_v6_7_rebase/drivers/video/fbdev/mstar-gopfb.c).
- **No more glitches under memory load.** Buffers are flipped in the GOP
  vsync interrupt (like MStar's GFLIP), and the driver gives the GOP
  priority in the memory arbiter (MIU). Without that, scattered CPU writes
  (fbDOOM drawing columns) starved the display and HDMI showed shifted
  lines and garbage. Details: [docs/display_flicker_miu.md](docs/display_flicker_miu.md).
- **Correct colours, 65536 instead of 4096.** MBoot's `osd_create 1 1` sets
  GOP format 2, which is ARGB4444, not RGB565, and the old kernel converted
  everything to xRGB4444. It also garbled the WIWA boot logo (green
  rectangle, wrong colours). `logofix` replaces `osd_create`: it converts the
  logo (YUV 4:2:2) to RGB565 and switches the GOP to format 1 (real RGB565)
  at vsync, so the logo stays correct until Linux takes over the same buffer.
  **This kernel needs `logofix` instead of `osd_create`**, otherwise the
  colours are wrong (for example a light blue console).
- **I2C driver** for the Kronus hardware I2C master (a Philips SIO1 style
  engine), pinmux set from the DTS.
- **Front panel driver** (FD650/TM1650 style controller on the PM SAR pins).
- Both drivers are **built into the kernel**, no `insmod` needed. The HDMI
  jitter once blamed on them was the GOP memory starvation fixed by the MIU
  priority above.

## Pendrive

To prepare the pendrive, create two partitions:
1. FAT32, about 100 MB: copy `vmlinux.bin` and `logofix/logofix.bin` there
2. a Linux partition, larger than the `rootfs.ext2` file

Rootfs is created with buildroot. It must be unpacked first.
Copy rootfs.ext2 to the second partition of the pendrive (here `/dev/sdb2`):
```
7z x rootfs_gcc.7z
mv rootfs_gcc.ext2 rootfs.ext2
sudo dd if=rootfs.ext2 of=/dev/sdb2 bs=1M status=progress
```

## Booting

Serial console 115200 8N1. Press Enter during power-on to get the
`<< MStar >>#` prompt.

```
usb start
fatload usb 0 0x80F00000 logofix.bin
go 0x80F00000
fatload usb 0 0x80000000 vmlinux.bin
go 0x80000000
```
`go 0x80000000` works for any build: with `CONFIG_BOOT_RAW=y` the first
instruction of the image is a jump to the real entry point (`0x805e9be4` here).
Log in as `root` (no password; set one with `passwd`).

`go 0x80F00000` should end with `## Application terminated, rc = 0x0`
(`rc = 0x1` means the GOP was not showing the YUV logo and nothing was changed).

Automatic start. The watchdog must be disabled first, otherwise the board
keeps resetting:
```
setenv bootlinux usb start\;fatload usb 0 0x80F00000 logofix.bin\;go 0x80F00000\;fatload usb 0 0x80000000 vmlinux.bin\;go 0x80000000\;
setenv bootcmd mw.w 0xBF006040 0 1\;mw.w 0xBF006010 0 1\;mw.w 0xBF006014 0 1\;run bootlinux\;
saveenv
```
Then reset the power supply. Because of `go 0x80000000`, `bootlinux` does
not change when you replace `vmlinux.bin` with a new build.

The three `mw.w` writes switch off the SoC watchdog (`0xBF006000`): MBoot
leaves it running for the original firmware, which keeps resetting it, and
Linux does not.

The stock bootcmd, to restore the original firmware:
```
setenv bootcmd spi_rdc 0x80B00000 0x8001C 0x289977\;LzmaDec 0x80B00000 0x289977 0x80000180 0x81000000\;go 0x80000224\;
saveenv
```

## Flash system: Linux in the box's own flash

Instead of booting from the pendrive, the box can run a small Linux from its
own 4 MB SPI flash, in place of the original firmware:
- it starts in about 10 s, and root is logged in automatically on the serial console and on the TV (`tty1`);
- `doom` starts fbDOOM with sound effects and music on HDMI (Freedoom E1M1);
- the root filesystem is read-only squashfs, with `/tmp` in RAM, so after a reset the system is always clean;
- a pendrive, if present, is mounted on `/mnt/usb` (first FAT partition).

**This erases the original firmware** (flash `0x80000`–`0x3E0000`). MBoot,
the boot logo and the MBoot environment are not touched, so the
`<< MStar >>#` prompt stays available. **Make a full backup of the flash
first** ([getting_started.md, step 1](docs/getting_started.md#1-get-the-bootloader-prompt-and-back-up-the-flash)).

No network or TFTP is needed. MBoot reads files from a FAT32 pendrive
(`fatload usb`) into RAM, and `spi_wrc` erases, writes and verifies the flash
from RAM.

Flash layout:

| Offset | Size | Linux partition | Content | File |
|---|---|---|---|---|
| `0x000000` | 448 KB | `mtd0` boot | sboot + MBoot (unchanged) | |
| `0x070000` | 64 KB | `mtd1` logo | boot logo (unchanged) | |
| `0x080000` | 1216 KB used (1344 KB partition) | `mtd2` kernel | mini kernel, LZMA | `flash/kernel.img` |
| `0x1D0000` | 64 KB | `mtd3` logofix | `logofix` | `flash/logofix.img` |
| `0x1E0000` | 2 MB | `mtd4` rootfs | squashfs | `flash/rootfs.img` |
| `0x3E0000` | 128 KB | `mtd5` env | MBoot environment (unchanged) | |

### 1. Pendrive

Copy `flash/kernel.img`, `flash/logofix.img` and `flash/rootfs.img` to the
first (FAT32) partition of a pendrive and check them with
`md5sum -c flash/md5sums.txt`. Put the pendrive into the box.

### 2. Write the flash from MBoot

Serial console 115200 8N1; press Enter during power-on to get the
`<< MStar >>#` prompt. First switch the watchdog off, otherwise the board can
reset in the middle of writing:
```
mw.w 0xBF006040 0 1
mw.w 0xBF006010 0 1
mw.w 0xBF006014 0 1
usb start
```
Then, for each image, load it to RAM, write it, read it back and compare:
```
fatload usb 0 0x80C00000 kernel.img
spi_wrc 0x80C00000 0x80000 0x130000
spi_rdc 0x81000000 0x80000 0x130000
cmp.l 0x80C00000 0x81000000 4C000

fatload usb 0 0x80C00000 logofix.img
spi_wrc 0x80C00000 0x1D0000 0x10000
spi_rdc 0x81000000 0x1D0000 0x10000
cmp.l 0x80C00000 0x81000000 4000

fatload usb 0 0x80C00000 rootfs.img
spi_wrc 0x80C00000 0x1E0000 0x200000
spi_rdc 0x81000000 0x1E0000 0x200000
cmp.l 0x80C00000 0x81000000 80000
```
- `spi_wrc` must end with `Verifying...OK`.
- Each `cmp.l` must report that all words were the same: 311296 for the kernel, 16384 for logofix, 524288 for the rootfs.
- If one fails, write that image again before going on.

### 3. Automatic start

MBoot silently cuts command lines after about 128 characters, so the start
is split into variables. Each line below is shorter than that:
```
setenv wdoff mw.w 0xBF006040 0 1\;mw.w 0xBF006010 0 1\;mw.w 0xBF006014 0 1\;
setenv flashlogo spi_rdc 0x80F00000 0x1D0000 0x10000\;go 0x80F00000\;
setenv flashboot spi_rdc 0x80B00000 0x80000 0x130000\;LzmaDec 0x80B00000 0x1285DA 0x80000000 0x81000000\;go 0x80000000\;
setenv bootcmd run wdoff\;usb start\;run flashlogo\;run flashboot\;
saveenv
reset
```
- `printenv` should show the four variables with plain `;`.
- `0x1285DA` is the LZMA size of the published `kernel.img`. A kernel you build yourself has a different size: `tools/flash/mkflash.py` prints it ([build_manual.md](build_manual.md)).
- If the size is wrong, `LzmaDec` reports a CRC error, rewrites the environment and resets; the MBoot prompt still works.
- `usb start` is needed even without a pendrive: the kernel does not power up the USB PHY itself. Without a pendrive it only prints an error, and the start goes on.

After `reset` you see the boot logo, then MBoot prints `CRC check success`,
and after about 10 s the shell (`#`) appears on the serial console and on the
TV. Type `doom` to play: keys come from the serial terminal, or use a USB
keyboard with `openvt -c 2 -s -w -- doom`.

The pendrive system from [Booting](#booting) still works: press Enter at
power-on, then `run wdoff`, then `run bootlinux` (if you saved `bootlinux`).

### Updating one part

Each image can be rewritten on its own with its lines from step 2. For example, a
new rootfs needs only the `rootfs.img` block; the kernel and the environment
stay. After a new kernel, also change the size in `flashboot`.

### Back to the original firmware

Write the original contents back from your backup (`backup.bin` from step 1
of getting_started, 4 MB) and restore the stock `bootcmd`:
```
mw.w 0xBF006040 0 1
mw.w 0xBF006010 0 1
mw.w 0xBF006014 0 1
usb start
fatload usb 0 0x80C00000 backup.bin
spi_wrc 0x80C80000 0x80000 0x360000
setenv bootcmd spi_rdc 0x80B00000 0x8001C 0x289977\;LzmaDec 0x80B00000 0x289977 0x80000180 0x81000000\;go 0x80000224\;
saveenv
```
The backup is loaded at `0x80C00000`, so flash offset `0x80000` is at RAM
`0x80C80000`, and `0x360000` bytes reach up to the environment at
`0x3E0000`. This way back has not been tried on our box yet.

## I2C and front panel

Both drivers are built in. I2C bus 0 is the MxL5007T tuner at `0x60`; the
front panel (FD650/TM1650 style) gives POWER / CH+ / CH- keys and the
4-digit display.

I2C, reading the MxL5007T tuner ID (expected `0x14`):
```
i2ctransfer -y 0 w2@0x60 0xfb 0xd9 r1
```

Front panel display and keys:
```
echo HELO > /sys/devices/platform/1f000000.riu/1f002840.front-panel/text
cat /dev/input/event0 | hexdump    # keys: KEY_POWER, KEY_CHANNELUP, KEY_CHANNELDOWN
```

## logofix

Bare-metal MIPS program (about 1 KB), linked at `0x80F00000`. Build:
```
cd logofix && sh build.sh
```
GOP colour formats found on the board: 1 = RGB565, 2 = ARGB4444,
9 = YUV 4:2:2 (bytes Y0 V Y1 U, used by the MBoot logo).

```
cat /proc/cpuinfo 
system type      : MStar MIPS SoC [Linux-ChenXing]
machine         : MStar MSD7816 test
processor      : 0
cpu model      : MIPS 34Kc V5.5  FPU V0.0
BogoMIPS      : 364.54
wait instruction   : yes
microsecond timers   : yes
tlb_entries      : 32
extra interrupt vector   : yes
hardware watchpoint   : yes, count: 4, address/irw mask: [0x0ffc, 0x0ffc, 0x0ffb, 0x0ffb]
isa         : mips1 mips2 mips32r1 mips32r2
ASEs implemented   : mips16 dsp mt
Options implemented   : tlb 4kex 4k_cache 32fpr prefetch mcheck ejtag llsc userlocal vint perf_cntr_intr_bit nan_legacy perf mm_full
shadow register sets   : 1
kscratch registers   : 0
package         : 0
core         : 1
VPE         : 0
VCED exceptions      : not available
VCEI exceptions      : not available

processor      : 1
cpu model      : MIPS 34Kc V5.5
BogoMIPS      : 369.66
wait instruction   : yes
microsecond timers   : yes
tlb_entries      : 32
extra interrupt vector   : yes
hardware watchpoint   : yes, count: 4, address/irw mask: [0x0ffc, 0x0ffc, 0x0ffb, 0x0ffb]
isa         : mips1 mips2 mips32r1 mips32r2
ASEs implemented   : mips16 dsp mt
Options implemented   : tlb 4kex 4k_cache 32fpr prefetch mcheck ejtag llsc userlocal vint perf_cntr_intr_bit nan_legacy perf mm_full
shadow register sets   : 1
kscratch registers   : 0
package         : 0
core         : 1
VPE         : 1
VCED exceptions      : not available
VCEI exceptions      : not available
```

## Tools

`tools/` holds the sources written during the reverse engineering:

| Directory | What |
|---|---|
| `tools/board/` | C programs built on the board itself (`gcc -O2 -o x x.c`), uploaded over the serial console (`tools/uart_upload.py`): framebuffer and vsync tests (`fbflip`, `gopflip`, `flipcheck`, `bwtest`…), front panel (`frontpanel`), demodulator 8051 mailbox (`dmdmbx`), RF spectrum scan (`rfscan`, see [docs/rf_scanner.md](docs/rf_scanner.md)), register dump (`riudump`), audio (`mstar_audio.h`, `wavplay`, `audiotest3`) |
| `tools/board/extract_audio_fw.py` | generates the audio DSP firmware headers that `mstar_audio.h` needs (`dsp_fw.h`, `dec03_fw.h`, `se31_fw.h`, `xpcm_fw.h`, `audio_init_table.h`) **from your own flash dump**: `python3 extract_audio_fw.py wiwa_hd95_BACKUP.BIN tools/board` |
| `tools/baremetal/logofix/` | MBoot helper that fixes the boot logo colours (source of `logofix/logofix.bin`) |
| `tools/baremetal/stockdump/`, `stockprobe/` | probes that run inside the live stock firmware (loaded from MBoot into RAM, the demo console `Help` patched to jump to them); guide in Polish: [docs/pl/sondy_stock.md](docs/pl/sondy_stock.md) |
| `tools/serial/` | Python (pyserial) helpers for the UART console: run commands, catch the MBoot prompt, log in, load a probe into the stock firmware |
| `tools/ghidra/`, `tools/*.py` | Ghidra scripts (8051 analysis, exports) and BSP function matching (`match_libs.py`) |
| `tools/bootlogo/` | boot logo generators (v1 Tux, v2 retro) |
| `tools/flash/mkflash.py` | flash images: kernel LZMA + CRC32 for `LzmaDec`, padding (see [build_manual.md](build_manual.md)) |
| `tools/opl_host/` | host test of the fbDOOM music: renders a song with `opl2.c`, fmopl or Nuked OPL3 (reference) to raw PCM; `oplplay … rt` on the board loads the CPU like the game's music |

Most paths inside the scripts and the Polish notes refer to the original
working directory layout (`binaries/`, `fbDOOM/`, …), not to this repo.

## License

Our own work in this repository (documentation, `tools/`, `logofix/`,
`examples/`, boot logo scripts) is under the [MIT license](LICENSE). The
kernel (GPL-2.0), `bin/fbdoom` (GPL-2.0), the Buildroot rootfs
(per package licenses) and the Tux images (Larry Ewing) keep their own
licenses; the details are in [LICENSE](LICENSE). MStar's DSP code is not
included.
