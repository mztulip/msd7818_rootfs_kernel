# rfscan: a swept RF spectrum analyzer from the box's own tuner

`tools/board/rfscan.c` turns the WIWA HD95 into a simple RF spectrum
analyzer. It sweeps the MxL5007T tuner across the band (44–880 MHz), measures
the signal power at every step and draws a bar graph on the HDMI/SCART
screen. It also prints a table to the console. It shows **where there is a
signal**; it does not demodulate anything.

## How it measures

- The tuner is set up and tuned over I2C (`/dev/i2c-0`, the
  `i2c-mstar-miic` driver): DVB-T mode, IF 4.57 MHz, 24 MHz crystal, 8 MHz
  channel filter.
- **Where the samples come from.** The on-die DVB-T demodulator has its own
  8051 microcontroller. Byte `0x2098` of its XDATA memory follows the
  signal from the tuner: it stays at `0x80` when the tuner is asleep, and
  swings around `0x80` when it is tuned. It looks like the IF signal after
  the ADC, in offset binary. Linux reads it through the 8051's mailbox
  (about 4.4 µs per read). The reads come at random moments, so an FFT is
  not possible, but the **power** is: it is the variance of the samples.
- **One point:**
  1. tune to f;
  2. wait for the PLL lock and `SETTLE_MS` more (≥ 20 ms; shorter gives
     false readings);
  3. take N reads, xᵢ = vᵢ − 128;
  4. level = 10·log₁₀(variance) dB, relative and not calibrated to dBm.

  With N = 1500 the statistical error is about 0.16 dB.
- Each point is the total power in the 8 MHz window around f. Stepping f
  and drawing bars is how a classic swept-filter spectrum analyzer works.
  A DVB-T/T2 multiplex therefore looks like a flat hump one channel wide.
- A full sweep from 44 to 880 MHz in 4 MHz steps (210 points) takes about
  9 s.

## Running it

The demodulator's 8051 has to be running, and only the stock bootloader
loads its firmware. Stop at the MBoot prompt, run `tuner_init`, then start
Linux in the same session:
```
<< MStar >># tuner_init
<< MStar >># run bootlinux          (or the manual boot commands)
```
On the board (as root):
```sh
insmod i2c-mstar-miic.ko
echo 0 > /sys/class/vtconsole/vtcon1/bind      # otherwise the text console draws over the graph
./rfscan 44 880 4 1500 1 30                     # START STOP STEP_MHZ READS LOOPS SETTLE_MS
./rfscan 466 700 1 1500 0 30 > /tmp/rfscan.log 2>&1 &   # LOOPS 0 = sweep forever, in the background
killall rfscan
echo 1 > /sys/class/vtconsole/vtcon1/bind      # console back
```
Build on the board: `gcc -O2 -o rfscan rfscan.c -lm`, or take the binary
from the applications release.

The graph has a grid every 100 MHz and every 10 dB, a range of 0–40 dB, and a
red marker under the axis at the current position. In the continuous mode
only the current bar is redrawn, so the picture does not flicker.

## Example: with and without an antenna

Measured in Poland with a simple indoor antenna. The raw tables are in
[rfscan_data/](rfscan_data/) (`no_antenna_*` = baseline,
`antenna_*` = with the antenna; 44–880 MHz in 4 MHz steps and 466–700 MHz
in 1 MHz steps). Differences of 1 dB or more against the baseline:

| Frequency | Gain over baseline | Most likely |
|---|---|---|
| 470–478 MHz | up to +3.2 dB, ~8 MHz wide plateau | UHF channel 21 (474 MHz): a terrestrial TV multiplex |
| 665–671 MHz | about +2 dB, ~7 MHz wide | UHF channel 45 (666 MHz): a terrestrial TV multiplex |
| 740–744 MHz | +8…+9 dB | the strongest signal; the 4 MHz step does not show its shape |
| 816–820 MHz | +2.5 dB | LTE 800 downlink (791–821 MHz) |
| 444–448 MHz | +3.5 dB | around PMR446 (unconfirmed) |

Poland broadcasts terrestrial TV only in **DVB-T2/HEVC** since 2022. The
MSD7818 demodulator handles DVB-T (and DVB-C) only, so the box sees these
multiplexes in `rfscan` but the original firmware's channel scan finds
nothing.

## Limitations

- The levels are relative. Nobody controls the tuner gain (the
  demodulator's AGC loop is not running), so the contrast is only a few dB.
- The baseline jumps at about 128, 336, 540 and 650 MHz, where the tuner
  switches bands. Subtract a sweep without the antenna to see real signals.
- Single spikes at one frequency, which neighbouring points do not confirm,
  are tuning artefacts.

More detail, including how register `0x2098` was found (in Polish):
[pl/demodulator_if.md](pl/demodulator_if.md), section "Widmo przemiatane RF
na ekranie".
