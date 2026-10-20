# Getting started: Linux on your own WIWA HD95 (MStar MSD7818)

This guide goes from an unmodified box to Linux with the framebuffer, the
front panel, fbDOOM and (optionally) sound. Nothing in it writes to the SPI
flash except the bootloader environment (`saveenv`). That step can be
undone, and before it you make a full backup of the flash.

## What you need

- A WIWA HD95 (or another MSD7818/MSD7816 "Kronus" box; addresses may then
  differ).
- A **3.3 V USB-UART adapter** (do not use 5 V). Connect it to the box's
  serial header (UART0: GND, RX, TX), 115200 8N1. A terminal such as
  `picocom -b 115200 /dev/ttyUSB0`.
- A USB pendrive (1 GB or more; its contents will be erased).
- An HDMI screen or a TV on SCART.
- From the releases:

  | Download | Repo / release | Contents |
  |---|---|---|
  | kernel | [linux_msd781x releases](https://github.com/mztulip/linux_msd781x/releases) | `vmlinux.bin`, `logofix.bin`, DTB, config |
  | rootfs | this repo: `rootfs_gcc.7z` | Buildroot root filesystem (ext2, with gcc) |
  | applications | [this repo's releases](https://github.com/mztulip/msd7818_rootfs_kernel/releases) | test and hardware tools (`fbflip`, `frontpanel`, `rfscan`, …) |
  | fbDOOM | [fbDOOM releases](https://github.com/mztulip/fbDOOM/releases) | `fbdoom` (no sound) |

  `vmlinux.bin` and `logofix/logofix.bin` are also in this repo.

## 1. Get the bootloader prompt and back up the flash

Power the box on while pressing Enter in the terminal. The stock bootloader
(MBoot, U-Boot 1.1.6) stops at `<< MStar >>#`.

Back up the whole 4 MB SPI flash to a FAT-formatted pendrive first:
```
usb reset 0
spi_rdc 80000000 0 400000
fatwrite usb 0 80000000 backup.bin 400000
```
Keep `backup.bin`. Besides being your safety copy, you need it to generate
the audio DSP firmware (step 8).

## 2. Prepare the pendrive

Two partitions:
1. FAT32, about 100 MB: copy `vmlinux.bin` and `logofix.bin` there.
2. A Linux partition, at least 200 MB: write the rootfs to it.

```sh
7z x rootfs_gcc.7z
sudo dd if=rootfs_gcc.ext2 of=/dev/sdX2 bs=1M status=progress   # sdX = your pendrive!
sync
```

## 3. First boot by hand

Plug the pendrive into the box and stop at `<< MStar >>#` again:
```
usb start
fatload usb 0 0x80F00000 logofix.bin
go 0x80F00000
fatload usb 0 0x80000000 vmlinux.bin
go 0x80000000
```
- `logofix` switches the display to RGB565 and fixes the boot logo
  colours. The kernel expects it; without it the console is light blue.
- `0x80000000` is the load address. The kernel is built with
  `CONFIG_BOOT_RAW=y`, so its first instruction jumps to the real entry
  point (`0x805e9be4` for this build; it changes with every rebuild). The
  same `go 0x80000000` therefore works for any kernel build.

The kernel prints to the serial console and to the screen. It waits for
the pendrive (`root=/dev/sda2 rootwait`), then shows a login prompt on
both. The first boot waits a while for a network interface (`eth0`); that
is normal.

**Login: `root`, no password.** Set one with `passwd` if the box is on a network.

## 4. Automatic start (optional)

The watchdog of the SoC must be switched off first, otherwise the board
resets in a loop:
```
setenv bootlinux usb start\;fatload usb 0 0x80F00000 logofix.bin\;go 0x80F00000\;fatload usb 0 0x80000000 vmlinux.bin\;go 0x80000000\;
setenv bootcmd mw.w 0xBF006040 0 1\;mw.w 0xBF006010 0 1\;mw.w 0xBF006014 0 1\;run bootlinux\;
saveenv
```
To return to the original firmware:
```
setenv bootcmd spi_rdc 0x80B00000 0x8001C 0x289977\;LzmaDec 0x80B00000 0x289977 0x80000180 0x81000000\;go 0x80000224\;
saveenv
```
Remove the Linux pendrive while the original firmware runs. Once, it
corrupted a file on the FAT partition.

If the pendrive stops responding ("Device NOT ready", or a reset loop at
`go 0x80F00000`), unplug the power for 10 seconds. A reset alone does not
power-cycle the USB port.

## 5. Copy files to the board

Without a network, there are two ways:
- Put the files on the FAT partition and `mount /dev/sda1 /mnt` on the board.
- Send them over the serial console: `tools/uart_upload.py LOCAL REMOTE`
  (base64 + md5 check; gzip big files first).

The rootfs has gcc, so the small C programs in `tools/board/` can also be
compiled on the board itself (`gcc -O2 -o x x.c`).

## 6. I2C (tuner) and the front panel

Both drivers are built into the kernel.
```
i2ctransfer -y 0 w2@0x60 0xfb 0xd9 r1        # MxL5007T tuner ID, expect 0x14
echo HELO > /sys/devices/platform/1f000000.riu/1f002840.front-panel/text
```

## 7. Framebuffer and fbDOOM

`/dev/fb0` is 1920x1080 RGB565 with two buffers (page flipping at vsync,
50 Hz). Try `fbflip` from the applications release. fbDOOM:
```
./fbdoom -iwad doom1.wad
```
It is playable from the serial console (picocom) or with a USB keyboard:
`openvt -c 2 -s -w -- sh -c 'cd /home; ./fbdoom -iwad doom1.wad'`.
Free WADs: shareware `doom1.wad`, or Freedoom (https://freedoom.github.io).

## 8. Sound (optional, build it yourself)

Sound goes through the SoC's audio DSPs and needs MStar's DSP code from
your box's original firmware. It is not distributed, so generate it from
your backup:
```sh
python3 tools/board/extract_audio_fw.py backup.bin tools/board
```
Then copy `tools/board/{mstar_audio.h,wavplay.c,*_fw.h,audio_init_table.h}`
to the board and build `wavplay` there (`gcc -O2 -o wavplay wavplay.c`). For
fbDOOM with sound, build it on a PC with `make NOSDL=1 MSTAR_SOUND=1
MSTAR_AUDIO_DIR=…/tools/board`. Details: [audio.md](audio.md).

## More

- What the kernel changes do: [framebuffer_driver.md](framebuffer_driver.md),
  [display_flicker_miu.md](display_flicker_miu.md).
- Boot logo: [../bootlogo/README.md](../bootlogo/README.md).
- All the reverse engineering notes (in Polish): [pl/README.md](pl/README.md).
