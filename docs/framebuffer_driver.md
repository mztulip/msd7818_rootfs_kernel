# Writing the double buffered framebuffer driver (`mstar-gopfb`)

A step by step account of how the driver was written, from probing the
hardware to a working `/dev/fb0` with page flipping at vsync. Every step can
be repeated on the board.

Source: [`drivers/video/fbdev/mstar-gopfb.c`](https://github.com/mztulip/linux_msd781x/blob/mstar_v6_7_rebase/drivers/video/fbdev/mstar-gopfb.c)
in https://github.com/mztulip/linux_msd781x (branch `mstar_v6_7_rebase`),
together with the [DTS](https://github.com/mztulip/linux_msd781x/blob/mstar_v6_7_rebase/arch/mips/boot/dts/mstar/msd7816-test.dts)
and [`wiwa_hd95_defconfig`](https://github.com/mztulip/linux_msd781x/blob/mstar_v6_7_rebase/arch/mips/configs/wiwa_hd95_defconfig).
Test program: [`examples/fbflip.c`](../examples/fbflip.c).

Status: works on the WIWA HD95 (MSD7818). It is part of the `vmlinux.bin`
in this repository (entry `0x805e87e4`).

## 0. Starting point and goal

- MBoot shows the boot logo through the **GOP** graphics layer and sets up
  HDMI 1080p. `logofix` leaves that layer in RGB565 on the buffer at
  physical `0x2100000`.
- Linux used `simple-framebuffer` (simpledrm). It writes into the memory
  the GOP shows and nothing else: there is no second buffer and no vsync, so
  animations tear.
- Goal: a plain fbdev driver (`/dev/fb0`) that
  - has 2 buffers (`yres_virtual = 2 * yres`),
  - switches the shown buffer at vsync (`FBIOPAN_DISPLAY`),
  - can wait for vsync (`FBIO_WAITFORVSYNC`),
  - **keeps the HDMI timing** set by MBoot.

fbdev rather than DRM: a single file of about 300 lines, and existing
programs (fbDOOM, the test tools) already use it. A DRM driver would mean
porting the whole pipeline from `drivers/gpu/drm/mstar` (ARM/Infinity).

## 1. How the GOP works

1. **A library with symbols.** The MStar MBoot sources ship
   `MstarCore/bsp/k1/lib/libapiGOP.a` (Kronus) with function names.
   `GOP_GWIN_TriggerRegWriteIn`, `GOP_SetGop23GwinInfo` and
   `MDrv_GOP_GWIN_SetWinFmt` show that:
   - the GOP registers are `0x1F00-0x1FFF` in the RIU and **banked**:
     register `0x1FFE` selects the bank (bits `[3:0]`),
   - bit 10 of `0x1FFE` is the **trigger**: the written registers are
     latched at the next vsync,
   - bits 12-15 are the **ACK** of GOP0-GOP3: `1` = waiting for vsync,
     `0` = applied,
   - the sequence is: write `bank | 0x400`, then `bank`, then wait for the
     ACK bit to drop.
2. **Addresses.** RIU register `0x1Fxx` (non-PM) is at CPU `0xBF200000 +
   xx*2` in MBoot and at physical `0x1F200000 + xx*2` from Linux
   (`devmem`): `0x1F00` -> `0x1F203E00`, `0x1FFE` -> `0x1F203FFC`.

## 2. Finding the right layer (`devmem` / `md` on the board)

The Linux buffer is at `0x2100000`. GOP addresses are in 8 byte units, so
look for `0x2100000 / 8 = 0x420000`, i.e. `0x0042` in the high address
word. Dumping every bank (in MBoot `mw.w bf203ffc 700N; md bf203e00 80`
for N = 0..D) showed:
- **bank `0xA`**: `0x1F00` = `0x3F11` (bit 0 enable, `[7:4]` format 1 =
  RGB565), `0x1F02`/`0x1F04` = `0x0000`/`0x0042`, `0x1F12` = `0x01E0`
  (pitch 3840/8): this is the **GOP3 window (GWIN)**,
- **bank `0x9`**: 1920x1080 stretch window, scale 1:1: **GOP3 control**,
- GOP0 (banks 0/1) is empty, so Linux is not on GOP0.

At rest `0x1FFE` reads `0x7000`. Keep the upper bits, only change the bank
number and the trigger bit.

## 3. Flipping by hand (`devmem`)

```sh
devmem 0x1F203FFC 16 0x700A          # bank A (GOP3 window)
devmem 0x1F203E04 16 0x7700          # address low word: 0x437700 = 200 lines further
devmem 0x1F203E08 16 0x0043          # address high word
devmem 0x1F203FFC 16 0x7409          # bank 9 + trigger
devmem 0x1F203FFC 16 0x7009          # bank 9
devmem 0x1F203FFC 16                 # 0xF009, shortly after 0x7009: applied at vsync
```
Two traps:
- the trigger must be written **with the GOP3 control bank (9)**, not the
  window bank (A). With bank A nothing happens,
- **do not change the bank before bit 15 drops to 0**, or the update is
  cancelled.

An address outside reserved memory shows "noise" (random memory) at the
bottom of the screen: the second buffer has to be reserved memory.

## 4. Proof of concept in user space

Before writing the driver, the same mechanism was tested in C through
`/dev/mem` (map the register page `0x1F203000` and both buffers), with the
second buffer temporarily in a free reserved region at `0x3C00000`:
```c
static void vsync_latch(void)           /* trigger + wait for the ACK */
{
	wr(BANKSEL, 0x7000 | 0x400 | 0x9);
	wr(BANKSEL, 0x7000 | 0x9);
	while (rd(BANKSEL) & 0x8000)
		;
	wr(BANKSEL, 0x7000);
}

static void show(int b)                 /* show buffer b from the next vsync */
{
	uint32_t a = buf_phys[b] >> 3;

	wr(BANKSEL, 0x7000 | 0xa);
	wr(GWIN_ADDR_L, a & 0xffff);
	wr(GWIN_ADDR_H, a >> 16);
	vsync_latch();
}
```
Results:
- the vsync period measured by waiting for the ACK: **20.000 ms, 50 Hz**
  (the output is 1080p50),
- drawing into the visible buffer: 602 fps, a moving bar tears,
- drawing into the hidden buffer and flipping: exactly 50 fps, the bar is
  solid (confirmed on screen).

The driver only wraps these register writes in the fbdev API.

## 5. Memory for the buffers (DTS)

- One frame is 1920 x 1080 x 2 B = **4147200 B** (`0x3F4800`), two frames
  8294400 B.
- The old `fb-memory@2100000` reservation had 4 MB. It was grown to
  **9 MB** (`0x900000`, up to `0x2A00000`; the next reserved region,
  `ve-memory`, starts at `0x3200000`) and labelled so the driver can
  reference it:
  ```dts
  reserved-memory {
  	fb_memory: fb-memory@2100000 {
  		reg = <0x2100000 0x900000>;
  	};
  };
  ```
- Buffer 0 starts at `0x2100000`, where MBoot draws the logo, so the logo
  stays on screen until the console draws.
- Cost: Linux has 40 MB of RAM left (`free -m`).

## 6. Device node (DTS)

The GOP registers are inside the RIU, which the DTS describes as the bus
`riu@1f000000` (`ranges` maps children from `0x1F000000`), hence
`reg = <0x203e00 0x200>`:
```dts
&riu {
	gop3-fb@203e00 {
		compatible = "mstar,msd7816-gop3-fb";
		reg = <0x203e00 0x200>;          /* 0x1F203E00: registers 0x1F00-0x1FFF */
		memory-region = <&fb_memory>;
		width = <1920>;
		height = <1080>;
	};
};
```
The `framebuffer@2100000` (`simple-framebuffer`) node was **removed**, so
simpledrm does not claim the same memory.

## 7. Driver skeleton

A plain `platform_driver` matched by `compatible`
(`module_platform_driver`). Driver state lives in `info->par`:
```c
struct gopfb {
	void __iomem *regs;		/* 0x1F203E00 */
	phys_addr_t base;		/* physical address of buffer 0 */
	spinlock_t lock;		/* bank select + trigger sequence */
	u32 pseudo_palette[16];		/* console colours */
};
```
Register constants are offsets from `0x1F203E00`: register `0x1Fxx` ->
`xx * 2`, e.g. `0x1F02` -> `0x004`, `0x1FFE` -> `0x1FC`.

## 8. `probe`, step by step

1. **Parameters from the DTS:** `width`, `height`
   (`of_property_read_u32`), pitch = `width * 2`, frame = `pitch * height`.
2. **Memory:** `of_parse_phandle(np, "memory-region", 0)` and
   `of_reserved_mem_lookup()` give `base` and `size`. Number of buffers =
   `min(buffers, size / frame)`, `buffers` being a module parameter
   (default 2).
3. **Mappings:**
   - registers with `devm_platform_ioremap_resource(pdev, 0)`,
   - buffers with `devm_ioremap(base, frame * N)`. On MIPS this gives an
     uncached (KSEG1) mapping, so the GOP sees CPU writes immediately,
     with no cache flushing.
4. **`fb_info`** (`framebuffer_alloc`):
   - `fix`: `smem_start = base`, `smem_len = frame * N`,
     `line_length = pitch`, `visual = FB_VISUAL_TRUECOLOR`, `ypanstep = 1`,
   - `var`: 1920x1080, **`yres_virtual = 1080 * N`**, 16 bpp, RGB565
     fields red `{11, 5}`, green `{5, 6}`, blue `{0, 5}`,
   - `fbops = &gopfb_ops` with `FB_DEFAULT_IOMEM_OPS` (ready-made
     read/write/mmap/drawing helpers for I/O memory), `pseudo_palette`.
5. **Taking over the layer**, under the spinlock and after waiting for
   any pending ACK:
   - bank A: RGB565 format in `0x1F00`, buffer 0 address in
     `0x1F02`/`0x1F04`, pitch in `0x1F12`,
   - trigger.

   A warning is printed if MBoot left format 2 (ARGB4444, from
   `osd_create`).
6. `register_framebuffer(info)` and the message
   `fb0: 1920x1080 RGB565, 2 buffers at 0x02100000`.

## 9. `fb_pan_display`: switching buffers

```c
static int gopfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct gopfb *g = info->par;
	u32 addr = (g->base + var->yoffset * info->fix.line_length) >> 3;
	...
	spin_lock_irqsave(&g->lock, flags);
	ret = gopfb_wait_idle_atomic(g);          /* the previous trigger must be done */
	if (!ret) {
		writew(BANK_IDLE | BANK_GOP3_GWIN, g->regs + GOP_BANKSEL);
		writew(addr & 0xffff, g->regs + GOP_GWIN_ADDR_L);
		writew(addr >> 16, g->regs + GOP_GWIN_ADDR_H);
		gopfb_trigger(g);                 /* bank 9 | 0x400, then bank 9 */
	}
	spin_unlock_irqrestore(&g->lock, flags);
	return ret;
}
```
Design decisions:
- `yoffset * 3840` is always a multiple of 8, so any `yoffset` gives a
  valid address (`ypanstep = 1`),
- it **does not wait for vsync**. fbdev panning takes effect "from the
  next frame", and waiting is done separately with `FBIO_WAITFORVSYNC`. It
  only waits for the previous trigger (at most 20 ms), because changing the
  bank before the ACK cancels the update (step 3),
- after the trigger **the bank stays at 9** until the ACK; the next call
  waits for it first anyway,
- `pan_display` can be called in atomic context (console), hence the
  spinlock and `readw_poll_timeout_atomic()` (10 us steps, 100 ms limit).

## 10. `FBIO_WAITFORVSYNC`

```c
static int gopfb_wait_for_vsync(struct gopfb *g)
{
	spin_lock_irqsave(&g->lock, flags);
	if (!gopfb_pending(g))
		gopfb_trigger(g);	/* nothing pending: an empty trigger = wait for vsync */
	spin_unlock_irqrestore(&g->lock, flags);

	return readw_poll_timeout(g->regs + GOP_BANKSEL, v, !(v & BANK_ACK_GOP3),
				  500, VSYNC_TIMEOUT_US);
}
```
- If a flip from `pan_display` is pending, it waits for **that** flip.
  After it returns, the old buffer is no longer shown and can be drawn to.
- If nothing is pending, a trigger without register changes is simply a
  wait for the next vsync.
- This runs in process context (ioctl), so it polls with sleeping
  (`readw_poll_timeout`, every 0.5 ms) and without holding the lock.
- Handled in `fb_ioctl`: `case FBIO_WAITFORVSYNC`, anything else returns
  `-ENOTTY`.

## 11. The other `fb_ops`

- `fb_setcolreg` fills the 16 console colours of `pseudo_palette` in
  RGB565 (like `simplefb.c`).
- `fb_check_var`: the mode is fixed, so changes of resolution, bpp or
  virtual size are rejected. Only `yoffset` can change.
- `info->flags = 0`, without `FBINFO_HWACCEL_YPAN`: the console (fbcon)
  scrolls by redrawing, not by panning, and does not touch the buffers of
  applications.

## 12. Kconfig, Makefile, configuration

`drivers/video/fbdev/Kconfig`:
```kconfig
config FB_MSTAR_GOP
	tristate "MStar Kronus GOP3 framebuffer (double buffered)"
	depends on FB && OF
	depends on MSTAR_MIPS || COMPILE_TEST
	select FB_IOMEM_HELPERS
```
`drivers/video/fbdev/Makefile`: `obj-$(CONFIG_FB_MSTAR_GOP) += mstar-gopfb.o`.

`.config`: `CONFIG_FB_MSTAR_GOP=y` (built in, the console needs it from
boot) and `# CONFIG_DRM_SIMPLEDRM is not set`, then `make olddefconfig`.
The result is `arch/mips/configs/wiwa_hd95_defconfig`
(`make savedefconfig`).

A built-in I2C driver made the HDMI picture jitter on this board, which is
why the I2C and front panel drivers are modules. This driver built in does
**not** cause jitter (checked on screen).

## 13. Building and booting

```sh
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- wiwa_hd95_defconfig
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- -j6
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- dtbs
cp vmlinux vmlinux_dtb
mipsel-linux-gnu-objcopy --update-section .appended_dtb=arch/mips/boot/dts/mstar/msd7816-test.dtb vmlinux_dtb
mipsel-linux-gnu-objcopy -O binary vmlinux_dtb vmlinux_gop.bin
readelf -h vmlinux | grep Entry          # 0x805e87e4
```
Try it by hand in MBoot first, without touching the autoboot settings:
```
usb start
fatload usb 0 0x80F00000 logofix.bin; go 0x80F00000
fatload usb 0 0x80000000 vmlinux_gop.bin; go 0x805e87e4
```

## 14. Checking

```sh
dmesg | grep gopfb          # mstar-gopfb 1f203e00.gop3-fb: fb0: 1920x1080 RGB565, 2 buffers at 0x02100000
cat /proc/fb                # 0 mstar-gop3
gcc -O2 -o fbflip fbflip.c  # examples/fbflip.c, built on the board
./fbflip vsync 100          # 1920x1080 virtual 1920x2160 ... period 19.952 ms, 50.12 Hz
echo 0 > /sys/class/vtconsole/vtcon1/bind
./fbflip single 1500        # 403 fps, the bar tears
./fbflip 500                # double buffered, the bar is solid
echo 1 > /sys/class/vtconsole/vtcon1/bind
```
All of this was confirmed on the board on 2026-10-14. The I2C and front
panel modules still load, and the tuner answers on I2C.

## 15. Using it in a program

```c
int fd = open("/dev/fb0", O_RDWR);
struct fb_var_screeninfo var;  struct fb_fix_screeninfo fix;  uint32_t zero = 0;
ioctl(fd, FBIOGET_VSCREENINFO, &var);                  /* yres_virtual = 2 * yres */
ioctl(fd, FBIOGET_FSCREENINFO, &fix);
uint16_t *mem = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
int back = 1;
for (;;) {
	uint16_t *buf = mem + back * var.xres * var.yres;
	/* ... draw a frame into buf ... */
	var.yoffset = back * var.yres;
	ioctl(fd, FBIOPAN_DISPLAY, &var);              /* show it from the next vsync */
	ioctl(fd, FBIO_WAITFORVSYNC, &zero);           /* the old buffer is free now */
	back ^= 1;
}
```

## 16. Possible next steps

- **A vsync interrupt instead of polling.** The disabled `gop@203e00`
  node in `kronus.dtsi` lists `interrupts-extended = <&intc_irq 20 ...>`
  (not verified). `FBIO_WAITFORVSYNC` could then sleep on a wait queue.
- **A lower resolution scaled by the GOP** (e.g. 960x540 stretched to
  1080p with the bank 9 stretch window and scale registers): faster
  games and 3 buffers in the same memory.
- **fbDOOM / chocolate-doom**: draw into the hidden buffer and use
  `FBIOPAN_DISPLAY`.
