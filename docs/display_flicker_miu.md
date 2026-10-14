# Picture glitches under load: memory (MIU) priorities

Why fbDOOM made the HDMI picture flash and shift from time to time, how
the cause was found, and the fix (now in the
[`mstar-gopfb` driver](https://github.com/mztulip/linux_msd781x/blob/mstar_v6_7_rebase/drivers/video/fbdev/mstar-gopfb.c)
and in `vmlinux.bin` of this repository). Written for beginners.
Confirmed on a WIWA HD95 (MSD7818) on 2026-10-14.

## Symptom

With double buffering and the fast fbDOOM, single frames kept showing:
the picture shifted sideways, garbage from another part of the picture on
top of the normal one, parts of the game visible inside the black side
borders. It looked like an analogue TV losing sync. It happened in the
demo, in the game and even when standing still.

## Some background: who uses the memory

The board has one DRAM chip (64 MB), used by many blocks of the chip at
the same time:
- the **CPU** (MIPS): programs, data, DOOM's drawing,
- the **GOP** (graphics layer): **continuously reads** the picture from
  memory and sends it to HDMI. 1920x1080 at 2 bytes per pixel, 50 times
  a second, is about **207 MB/s**, without pause,
- USB, video decoder, audio, demodulator and so on.

The DRAM serves one request at a time. An **arbiter** in the memory
controller, which MStar calls the **MIU** (Memory Interface Unit), decides
who goes first. Every block is an MIU **client**, and clients are
organised in **groups** of 16.

The GOP has a small FIFO for the data it is about to show. If it gets no
new data from memory for a while, because the memory is busy with someone
else, the FIFO runs empty and the GOP sends wrong pixels or shifted lines
to HDMI (an underflow). The picture in memory is fine; only what reaches
the screen is broken.

DRAM is fastest with **sequential** accesses (a plain copy) and slowest
with **scattered** ones, because each of them opens a different DRAM row,
which takes time. DOOM draws walls in **columns**: a pixel, then 320 bytes
further, then another 320 bytes further. Every write goes somewhere else,
which is the worst pattern for the memory.

## How the cause was found

Every test ran on the board while someone watched the screen. The test
programs are in [`examples/`](../examples) (`bwtest.c`, `randhog.c`,
`fbflip.c`); the others are in the reverse engineering workspace.

| # | Hypothesis | Test | Result |
|---|---|---|---|
| 1 | the text console draws into the buffer (cursor) | console in `KD_GRAPHICS`, then unbound | still flickers |
| 2 | the driver misses vsyncs | 1000 flips, intervals measured | all about 20 ms |
| 3 | not enough bandwidth for full-frame copies | `bwtest`: 270 MB/s copy into the hidden buffer, static checkerboard shown | steady |
| 4 | flipping at a bad moment of the frame | flips 1 to 19.6 ms after vsync, both buffers identical | steady |
| 5 | address registers change "live" | address written without the trigger | picture unchanged, they are latched |
| 6 | the old buffer is still shown after the vsync | DOOM waiting 8 / 20.5 ms before drawing | still flickers (8 ms: only at the bottom) |
| 7 | wrong way of flipping | rewritten like MStar GFLIP: flip in the vsync interrupt with force-write | still flickers (kept, it is the vendor way) |
| 8 | DOOM corrupts the framebuffer | black borders checked in memory while playing | memory always clean: **only the display goes wrong** |
| 9 | black is transparent, another layer shows through | black screen + writes into the hidden buffer | stays black |
| 10 | DOOM without drawing | DOOM runs the demo but never touches the framebuffer; static checkerboard shown | **the checkerboard flickers**: the load itself is the cause |
| 11 | other GOP layers read memory | all GOP banks dumped | GOP0-2 windows disabled |
| 12 | CPU only (heat, power) | 2 busy loops | steady |
| 13 | kind of memory access | `randhog`: random writes / random reads / **column writes, 320 B stride** | random writes: one flash, reads: steady, **columns: chaos** |

From tests 10 and 13: **scattered CPU writes, like DOOM drawing columns,
take the memory away from the GOP**. The GOP cannot keep up reading the
picture, so HDMI gets shifted and broken lines. Plain copying (test 3) is
easy for the memory, which is why it did not flicker. A smaller DOOM
picture flickered even more, because the game then ran at more frames per
second and made more scattered writes.

Test 13 reproduces it without DOOM:
```sh
echo 0 > /sys/class/vtconsole/vtcon1/bind
./bwtest 1                              # checkerboard on screen
./randhog 20 8 c & ./randhog 20 8 c     # 20 s of column writes, 2 processes
echo 1 > /sys/class/vtconsole/vtcon1/bind
```
Before the fix the screen turns to chaos; after it only faint noise
remains.

## MIU registers

Source: MStar's `libdrvMIU.a` (Kronus BSP, with symbols), `halMIU.o`
disassembled:
- `HAL_MIU_SetHPriorityMask` sets the client's bit in register
  **`0x1248 + group * 0x20`**, the high-priority mask (1 = no high
  priority),
- `HAL_MIU_SetGroupPriority` writes a byte in register **`0x12FE`** (group
  order) and toggles bit 8 there to apply it,
- the `clientTbl` table (48 entries, `group << 4 | bit`) and the MIU
  headers of other MStar chips put the GOP clients (write, GOP0/1/2 read)
  in **group 1, bits 4-7**.

MIU0 is RIU bank `0x1012`, physical **`0x1F202400`** from Linux (register
`0x12xx` -> `0x1F200000 + 0x12xx * 2`).

| Register | Address (Linux) | Default | Meaning |
|---|---|---|---|
| `0x1248` | `0x1F202490` | `0xFFFF` | group 0 high-priority mask |
| `0x1268` | `0x1F2024D0` | `0xFFFF` | **group 1 (GOP)** |
| `0x1288` | `0x1F202510` | `0xFFFF` | group 2 |
| `0x12A8` | `0x1F202550` | `0xFFFF` | group 3 |
| `0x12FE` | `0x1F2025FC` | `0xE0E4` | low byte: group order (4 x 2 bits), `0xE4` = 0,1,2,3; bit 8 applies |

After MBoot **no client had high priority** (all `0xFFFF`), and the group
with the GOP was only second in the order. The stock firmware probably
sets this when its application starts; MBoot does not.

## Experiments (with `randhog ... c`)

| Setting | Screen |
|---|---|
| default | chaos |
| high priority for group 0, 2 or 3 only | chaos |
| **high priority for group 1** (`0x1F2024D0` = `0x0000`) | top calm, bottom still chaos |
| group 1 + order 1,0,2,3 (`0x12FE` = `0xE0E1`), not applied | first row fairly calm |
| **group 1 + order 1,0,2,3 applied with bit 8** (`0xE1E1`, then `0xE0E1`) | **steady**, barely visible fine noise |

By hand, for testing (until reboot):
```sh
devmem 0x1F2024D0 16 0x0000      # group 1 (GOP): high priority
devmem 0x1F2025FC 16 0xE1E1      # group order 1,0,2,3 + bit 8
devmem 0x1F2025FC 16 0xE0E1      # bit 8 back = applied
```

## The fix in the driver

The DTS node got a second register range (MIU0):
```dts
gop3-fb@203e00 {
	compatible = "mstar,msd7816-gop3-fb";
	reg = <0x203e00 0x200>,		/* GOP regs 0x1F00-0x1FFF */
	      <0x202400 0x200>;		/* MIU0 regs 0x1200-0x12FF (arbitration) */
	interrupts-extended = <&intc_irq 20 IRQ_TYPE_LEVEL_HIGH>;	/* GOP vsync */
	...
};
```
`probe` calls `gopfb_miu_priority()`: it clears the group 1 mask and
writes the order `0xE1` with a pulse on bit 8. `dmesg` shows:
`MIU: group 1 high priority (mask 0xffff -> 0x0000), order 0xe1`.
The range is mapped with plain `devm_ioremap`, because the `miu@202400`
node in `common.dtsi` describes the same area.

## Notes

- The whole group 1 gets high priority, so also its other clients
  (according to other MStar chips: video decoder MVOP, scaler, VE). With
  Linux and no video playback this does not matter. Not checked yet
  whether bits 4-7 alone (mask `0xFF0F`) would be enough.
- Which bit exactly is the GOP3 read is not known; the MStar table for
  this chip has no separate "GOP3_R".
- The driver now also flips like MStar GFLIP (vsync interrupt +
  force-write). That was not the cause, but it is the vendor's way and
  stays.
