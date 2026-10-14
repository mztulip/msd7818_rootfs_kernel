# Custom boot logo (MBoot)

The picture shown by MBoot right after power-on is a plain JPEG stored in
the SPI flash. This describes how to replace it and how to go back to the
original logo. Tested on the WIWA HD95 (MSD7818) on 2026-10-14:
`linux_logo.jpg` from this directory is flashed and displays correctly.

## How MBoot finds the logo

The location and size come from MBoot environment variables (`printenv`):

```
LOGO_SPI_ADDR=70000      # offset in the SPI flash
LOGO_BIN_SIZE=BFCE       # JPEG size in bytes (hex); the original WIWA logo is 0xBFCE = 49102 B
logo_cmd=boot_logo 0 0 1 1
```

- The logo slot is the flash block **`0x70000`-`0x7FFFF` (64 KB)**. The
  original firmware image starts right after it at `0x80000`, so **the JPEG
  must be smaller than 0x10000 bytes**. The rest of the block after the JPEG
  is filled with zeros.
- The hardware JPEG decoder is used. The original file is a **baseline (not
  progressive) JPEG, 720x576, YCbCr 4:4:4** (no chroma subsampling), and the
  new logo uses the same format. Other variants (4:2:0, other sizes) were not
  tested.
- MBoot stretches 720x576 to the full 1920x1080 screen. Pixels are wider
  than tall, so design the picture as 16:9 and squash it to 720x576.
  `png2logo.py` does that.
- `logofix` (run instead of `osd_create`, see the main README) converts
  whatever logo MBoot drew to RGB565 for Linux, so it works with any logo.

## Files

| File | What |
|---|---|
| `png2logo.py` | **any picture -> JPEG for MBoot** (16:9 -> 720x576, < 64 KB) plus the values to type in MBoot |
| `linux_logo.jpg` | the Linux logo (Tux + "Linux on MStar MSD7818"), 60718 B (`0xED2E`) |
| `mklogo.py` | generator of `linux_logo.jpg` (Pillow; needs `tux.png`, font paths are for Arch Linux) |
| `Tux.svg` | Tux by Larry Ewing (lewing@isc.tamu.edu) made with The GIMP, SVG version from Wikimedia Commons |

## Preparing the picture

Needs Python 3 and Pillow.

```sh
python3 png2logo.py my_logo.png -o linux_logo.jpg --bg 102040
#   linux_logo.jpg: 36644 bytes, quality 92
#   MBoot: setenv LOGO_BIN_SIZE 8F24
#   CRC32 of the JPEG:              56A9312B  (crc32 0x80C00000 8F24)
#   CRC32 of the 64 KB flash block: 33FB1A41  (crc32 0x80C00000 0x10000)
```

- Best results with a **16:9** source (1920x1080, 1024x576, ...): nothing is
  distorted and there are no bars. Other aspect ratios get bars in the
  `--bg` colour (hex RRGGBB), and transparency is filled with `--bg` too.
- `--stretch` resizes to 720x576 ignoring the aspect ratio, for a picture
  that is already squashed like the original logo.
- The script lowers the JPEG quality until the file fits in 64 KB. Fine
  detail and noise make the file bigger. If the quality drops below about
  70, simplify the picture.
- Write down `LOGO_BIN_SIZE` and both CRCs, they are needed below.

To rebuild the Linux logo itself:
```sh
rsvg-convert -h 360 Tux.svg -o tux.png
python3 mklogo.py
```

## Flashing it, step by step

1. **Copy the JPEG to the FAT partition** of the pendrive (the one with
   `vmlinux.bin`).
2. **Get the MBoot prompt.** Connect the serial console (115200 8N1), power
   on and press Enter until `<< MStar >>#` appears. Disable the watchdog:
   ```
   mw.w 0xBF006040 0 1; mw.w 0xBF006010 0 1; mw.w 0xBF006014 0 1
   ```
3. **Back up your current logo** to the pendrive. Write down the old size too:
   ```
   printenv LOGO_BIN_SIZE                         # e.g. BFCE on the WIWA HD95
   usb start
   spi_rdc 0x80B00000 0x70000 0x10000
   crc32 0x80B00000 0x10000                       # write it down
   fatwrite usb 0 0x80B00000 logo_backup.bin 10000
   ```
   It is a good idea to back up the whole 4 MB flash once as well:
   `spi_rdc 0x80000000 0 400000` and then `fatwrite usb 0 0x80000000 flash_backup.bin 400000`.
4. **Build the new 64 KB block in RAM**, zeros with the JPEG at the start:
   ```
   mw.l 0x80C00000 0 4000             # 0x4000 words = 64 KB of zeros
   usb start
   fatload usb 0 0x80C00000 linux_logo.jpg
   crc32 0x80C00000 0x10000           # must equal "CRC32 of the 64 KB flash block" from png2logo.py
   ```
5. **Write it to the flash.** `spi_wrc` erases, writes and verifies the block:
   ```
   spi_wrc 0x80C00000 0x70000 0x10000    # Erasing... Writing... Verifying...OK !!!
   spi_rdc 0x80D00000 0x70000 0x10000
   crc32 0x80D00000 0x10000              # same value as in step 4
   ```
6. **Set the new size** (the value printed by png2logo.py):
   ```
   setenv LOGO_BIN_SIZE ED2E             # ED2E = linux_logo.jpg from this directory
   saveenv
   ```
7. **Test:** `reset`. The new logo appears right after power-on.

Notes:
- `spi_wrc` needs addresses and length aligned to the 64 KB erase block.
  Always write exactly the one block at `0x70000`, never more: the next
  block (`0x80000`) is the original firmware.
- If the new logo does not decode, MBoot still reaches its console (press
  Enter during boot), so you can always restore the original.

## Restoring the original logo

With `logo_backup.bin` (step 3) on the FAT partition, in MBoot:
```
usb start
fatload usb 0 0x80C00000 logo_backup.bin
crc32 0x80C00000 0x10000              # the value written down in step 3
spi_wrc 0x80C00000 0x70000 0x10000
setenv LOGO_BIN_SIZE BFCE             # the old value from step 3
saveenv
```

## Preview on the TV without flashing

From running Linux, you can write the picture to the framebuffer scaled
the way MBoot does it (1920x1080 RGB565). This shows how it will look
on the TV:
```sh
# on the PC (Python 3 + Pillow + numpy)
python3 - <<'EOF'
from PIL import Image; import numpy as np
a=np.asarray(Image.open('linux_logo.jpg').convert('RGB').resize((1920,1080),Image.BILINEAR)).astype(np.uint16)
open('preview.raw','wb').write((((a[...,0]>>3)<<11)|((a[...,1]>>2)<<5)|(a[...,2]>>3)).astype('<u2').tobytes())
EOF
gzip -9 preview.raw        # copy preview.raw.gz to the board (pendrive, scp, serial)
# on the board
echo 0 > /sys/class/vtconsole/vtcon1/bind        # stop the console from drawing over it
gunzip -c preview.raw.gz | dd of=/dev/mem bs=4096 seek=$((0x2100000/4096))
echo 1 > /sys/class/vtconsole/vtcon1/bind        # console back
```
