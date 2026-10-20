# SE DSP tools

Our own tools for the SE (Sound Effect) audio DSP of the MStar Kronus (MSD7818),
an ADSP-21xx-like core with 24-bit words. No MStar code here.

| File | What |
|---|---|
| `asm.py` | small assembler (syntax as printed by the decoder; every encoding is checked by decoding it again) |
| `isa.py` | instruction decoder, used by `asm.py` |
| `pcm.s` | our SE program that plays PCM from Linux on HDMI ([docs/own_dsp_audio.md](../../docs/own_dsp_audio.md)) |
| `pcm.hex` | `pcm.s` assembled (24-bit words, hex) |

Rebuild the program and the header used by `tools/board/own_audio.h`:

```sh
cd tools/dsp
python3 asm.py pcm.s -o pcm.hex
python3 ../board/gen_own_pcm.py pcm.hex ../board/own_pcm_prog.h
```

The encodings that `asm.py` knows were checked on the board; anything else
(e.g. MAC instructions) can be put in with `.word`.
