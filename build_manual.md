# Building the flash system yourself

How to build, from source, the three images that `flash/` holds: the mini
kernel, `logofix` and the squashfs rootfs with fbDOOM. How to write them to
the box from the MBoot prompt is in the [README](README.md#flash-system-linux-in-the-boxs-own-flash).

The pieces and where they come from:

| Image | Flash offset | Built from |
|---|---|---|
| `flash/kernel.img` | `0x080000` | Linux 6.7, https://github.com/mztulip/linux_msd781x, branch `mstar_v6_7_rebase`, config `wiwa_hd95_mini_defconfig`, LZMA with a CRC32 for MBoot's `LzmaDec` |
| `flash/logofix.img` | `0x1D0000` | `logofix/` in this repo |
| `flash/rootfs.img` | `0x1E0000` | Buildroot (musl, static BusyBox) with `rootfs_mini/` from this repo, plus fbDOOM (https://github.com/mztulip/fbDOOM, branch `master`) and a cut-down Freedoom IWAD |

Every image is padded with `0xFF` to the size of its flash partition.
`tools/flash/mkflash.py` does the LZMA packing and the padding. It
reproduces the published images bit for bit from the same inputs.

Directory layout used below (the Buildroot config refers to this repo as
`../msd7818_rootfs_kernel` from the Buildroot directory, so keep them side by side):
```
work/
  msd7818_rootfs_kernel/    this repo
  buildroot/                Buildroot
  linux_mini/               kernel, mini config
  fbDOOM/                   fbDOOM
```

## 1. Toolchains

- **Kernel:** `mipsel-linux-gnu-gcc` **12.2** with `mipsel-linux-gnu-binutils`.
  GCC 15 fails on this kernel (`cannot use keyword 'false' as enumeration
  constant`). On Arch Linux: the AUR package `mipsel-linux-gnu-gcc` at version
  12.2.0-3.
- **Rootfs and fbDOOM:** Buildroot builds its own musl cross toolchain
  (`rootfs_mini_out/host/bin/mipsel-buildroot-linux-musl-*`), see step 4.

## 2. Mini kernel

```sh
cd work
git clone -b mstar_v6_7_rebase https://github.com/mztulip/linux_msd781x linux_mini
cd linux_mini
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- wiwa_hd95_mini_defconfig
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- -j6
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- dtbs
cp vmlinux vmlinux_dtb
mipsel-linux-gnu-objcopy --update-section .appended_dtb=arch/mips/boot/dts/mstar/msd7816-test-flash.dtb vmlinux_dtb
mipsel-linux-gnu-objcopy -O binary vmlinux_dtb vmlinux_mini.bin
python3 ../msd7818_rootfs_kernel/tools/flash/mkflash.py kernel vmlinux_mini.bin kernel.img
#  kernel.img: LZMA 1213914 bytes = 0x1285da (LzmaDec size), unpacked 3963144
```
- Use the cross `objcopy`; the host one fails.
- The DTB `msd7816-test-flash.dtb` holds the command line
  (`root=/dev/mtdblock4 rootfstype=squashfs ro`) and the flash partitions.
- The kernel must fit the 1.3 MB partition (`mkflash.py` stops if it does not).

**The LZMA size changes with every build.** `mkflash.py` prints it, and it
must be written into the `LzmaDec` command of the `flashboot` variable (see
the README). The published `kernel.img` has `0x1285DA`. If the size in
`LzmaDec` is wrong, MBoot reports a CRC error, rewrites its environment and
resets. The MBoot prompt is still reachable after that (Enter at power-on).

## 3. logofix

The source is in `logofix/` (bare-metal MIPS, linked at `0x80F00000`). It
converts the YUV boot logo to RGB565 and switches the display (GOP) to
RGB565 before the kernel takes over.
```sh
cd work/msd7818_rootfs_kernel/logofix
sh build.sh                                   # -> logofix.bin (about 1 KB)
python3 ../tools/flash/mkflash.py pad logofix.bin logofix.img 0x10000
```

## 4. Buildroot: toolchain and rootfs

The published rootfs was built with Buildroot commit `d3229d7fa0`
(`2025.08-rc3-84-gd3229d7fa0`). Other versions may work but were not tried.
```sh
cd work
git clone https://github.com/buildroot/buildroot.git
cd buildroot
git checkout d3229d7fa0
make O=../rootfs_mini_out BR2_DEFCONFIG=../msd7818_rootfs_kernel/rootfs_mini/wiwa_mini_defconfig defconfig
make O=../rootfs_mini_out -j8                 # 30-60 min the first time
```
Two problems with a very new host (checked with GCC 16.2 and glibc 2.43):
- host-gcc fails on `char8_t` in libcody: add `HOSTCXX="/usr/bin/g++ -std=gnu++17"` to every `make` line.
- host-m4 1.4.20 fails with `error: expected identifier or '(' before '_Generic'`:
  its gnulib clashes with the C23 macros of glibc 2.43, and `-std=gnu17` does not help.
  Use the system m4 (1.4.21 or later) instead and mark the package as built:
  ```sh
  cp /usr/bin/m4 ../rootfs_mini_out/host/bin/m4
  touch ../rootfs_mini_out/build/host-m4-1.4.20/.stamp_built \
        ../rootfs_mini_out/build/host-m4-1.4.20/.stamp_host_installed
  make O=../rootfs_mini_out -j8 HOSTCXX="/usr/bin/g++ -std=gnu++17"
  ```

This first build gives the musl toolchain needed in step 5. The image it
makes has no game yet, because fbDOOM and the IWAD are added in steps 5–6
and the rootfs is rebuilt in step 7.

What `rootfs_mini/` contains:
- `wiwa_mini_defconfig`: mipsel 32r2 soft float, musl, static, `-Os`, squashfs XZ with 1 MB blocks, root without a password;
- `post_build.sh`: removes `libc.a` (static musl), the network and cron init scripts, and `seedrng` (it would write to the read-only root);
- `overlay/etc/inittab`, `overlay/usr/bin/autologin`: getty on the serial console and on `tty1` logs root in without a password;
- `overlay/etc/init.d/S20usb`: mounts the first FAT partition of a USB stick on `/mnt/usb`;
- `overlay/usr/bin/doom`: starts the game;
- `overlay/home/FREEDOOM_*.txt`: the Freedoom license and credits, which must stay next to the IWAD.

`overlay/home/fbdoom` and `overlay/home/freedoom_e1m1.wad` are not in git (`.gitignore`): steps 5 and 6 put them there.

## 5. fbDOOM

Our fork adds double buffering with vsync, fast 16 bpp drawing, keys from a
serial terminal, sound effects through our own SE DSP program (`OWN_SOUND=1`,
no MStar code) and OPL2 music (`opl2.c`). Its README describes the changes.
```sh
cd work
git clone https://github.com/mztulip/fbDOOM
cd fbDOOM/fbdoom
make NOSDL=1 OWN_SOUND=1 MSTAR_AUDIO_DIR=../../msd7818_rootfs_kernel/tools/board \
     CROSS_COMPILE=../../rootfs_mini_out/host/bin/mipsel-buildroot-linux-musl- \
     OBJDIR=build_musl OUTPUT=fbdoom_musl LDFLAGS="-Wl,--gc-sections -static"
../../rootfs_mini_out/host/bin/mipsel-buildroot-linux-musl-strip \
     -o ../../msd7818_rootfs_kernel/rootfs_mini/overlay/home/fbdoom fbdoom_musl
```
- `MSTAR_AUDIO_DIR` must hold `own_audio.h` and `own_pcm_prog.h` (in `tools/board/` of this repo).
- Run `make clean` when switching between builds: the Makefile does not rebuild on flag changes.
- The result is the same program as `bin/fbdoom_music` (about 550 KB, static).

## 6. Freedoom IWAD

The flash has room for one map, so `tools/doom/mkminiwad.py` makes a cut-down
IWAD from Freedoom: Phase 1 v0.13.0 (BSD-3-Clause; https://freedoom.github.io/,
download `freedoom-0.13.0.zip`). Description, in Polish: `tools/doom/README.md`.
```sh
python3 work/msd7818_rootfs_kernel/tools/doom/mkminiwad.py freedoom-0.13.0/freedoom1.wad \
        work/msd7818_rootfs_kernel/rootfs_mini/overlay/home/freedoom_e1m1.wad 1
#  freedoom_e1m1.wad: 1 maps, 1238 lumps, 4.09 MB, xz 1.32 MB
```

## 7. Rootfs with the game

```sh
cd work/buildroot
make O=../rootfs_mini_out -j8                 # a few seconds: copies the overlay, makes the image
ls -l ../rootfs_mini_out/images/rootfs.squashfs
python3 ../msd7818_rootfs_kernel/tools/flash/mkflash.py pad \
        ../rootfs_mini_out/images/rootfs.squashfs rootfs.img 0x200000
```
The rootfs partition has 2 097 152 bytes; the published image is 2 039 808
(57 KB left). `mkflash.py` stops if the image does not fit.

## 8. Try before writing (optional)

To test a new rootfs without touching the flash, put it on a spare partition
of the USB stick and boot a kernel whose DTB says `root=/dev/sda3`. Change
`bootargs` in a copy of `msd7816-test-flash.dts` (or decompile the DTB with
`dtc`, edit it and compile it again), append that DTB as in step 2 (no
kernel rebuild needed) and start it from the stick:
```sh
dd if=rootfs.squashfs of=/dev/sdX3 bs=64k     # on the PC (or on the board: /dev/sda3)
```
```
mw.w 0xBF006040 0 1
mw.w 0xBF006010 0 1
mw.w 0xBF006014 0 1
usb start
fatload usb 0 0x80F00000 logofix.bin
go 0x80F00000
fatload usb 0 0x80000000 vmlinux_sqtest.bin
go 0x80000000
```
The three `mw.w` writes switch the watchdog off. Without that the board resets after a few seconds.

## 9. Write to the flash

Copy `kernel.img`, `logofix.img` and `rootfs.img` to the FAT32 partition of
a USB stick and follow the README:
[Flash system](README.md#flash-system-linux-in-the-boxs-own-flash).
