# bootsel: boot menu for the WIWA HD95 (MSD7818)

A small bare-metal boot menu (like GRUB) that MBoot starts with `go`. Design notes and the full plan, in Polish: `docs/pl/plan_bootloader.md`.

**Status (2026-10-20): tested from RAM on the board over UART:** the default boot from flash (LZMA decode + CRC32 in about 1.5 s, shell 8.7 s after `go`), key `2` booting the kernel from the stick, and the menu without a stick kernel all work. The screen menu and the front-panel buttons are not checked yet. Not written to the flash yet.

## What it does

MBoot still does what it already can (watchdog off, USB, boot logo, `logofix`). It may also load `vmlinux.bin` from the USB stick to `0x81000000`. Then it jumps to `bootsel` at `0x80F00400`, which:

- shows a menu on UART0, on the screen (a box over the boot logo, 16×32 font) and on the front-panel LED display;
- option 1 (default after 3 s): **system from flash**. It decodes the LZMA kernel straight from the memory-mapped flash (`0x80000`), checks the CRC32 in its last 4 bytes, copies it to `0x80000000` and jumps there;
- option 2 (only when a kernel was loaded): **kernel from the USB stick**. It is recognised by its first instruction `j kernel_entry` (`CONFIG_BOOT_RAW`);
- keys: UART `1`, `2`, Enter; panel CH+/CH− move the selection, POWER boots; any key stops the countdown. The LED shows `FL 3` / `USb3` (selection and countdown), `boot`, or `Err`;
- on an error it returns to MBoot (rc 2–5), so `bootcmd` can fall back to `run flashboot`.

## Which root filesystem

The rootfs is always on the same medium as the kernel. Each kernel has a built-in DTB with its own `bootargs`, and `bootsel` passes nothing (`a0..a3 = 0`):

| Menu | Kernel | `root=` |
|---|---|---|
| 1 flash | `linux_mini` (LZMA at flash `0x80000`) | `/dev/mtdblock4` (squashfs at `0x1E0000`) |
| 2 USB | `vmlinux.bin` on the stick's FAT | `/dev/sda2` |

Fixed USB stick layout: partition 1 is FAT32 with `vmlinux.bin`, partition 2 is the ext2 rootfs.

## Files

| File | Purpose |
|---|---|
| `bootsel.c` | menu (UART, screen, FD650 panel), flash/USB boot, cache flush, jump |
| `lzma.c`, `lzma.h` | own LZMA ("LZMA alone") decoder written from the format spec, MIT |
| `test_lzma.c` | PC test: `gcc -O2 -o test_lzma test_lzma.c lzma.c` |
| `font.h`, `mkfont.py` | 16×32 bitmap font generated from DejaVu Sans Mono Bold (Bitstream Vera license) |
| `bootsel.ld`, `build.sh` | link at `0x80F00400`; build `bootsel.bin` and `bootblk.img` (64 KB flash block: `logofix.bin` at 0, `bootsel.bin` at `0x400`) |

## Memory

| What | Where |
|---|---|
| bootsel code + bss | `0x80F00400`…`0x80F0A900` |
| kernel loaded from USB | `0x81000000` |
| LZMA output | `0x82A00000` (after the framebuffers `0x2100000`…`0x2A00000`) |
| final kernel | `0x80000000` (max 15 MB, must stay below bootsel) |

## Test from RAM (no flash write)

Put `bootsel.bin` on the stick's FAT next to `vmlinux.bin`, press Enter at power-on, then in MBoot:
```
run wdoff
usb start
mw.l 0x81000000 0 1
fatload usb 0 0x81000000 vmlinux.bin
run flashlogo
fatload usb 0 0x80F00400 bootsel.bin
go 0x80F00400
```
