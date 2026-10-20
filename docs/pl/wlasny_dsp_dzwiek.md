# Dźwięk z własnego programu DSP, bez kodu MStara

Stan na 2026-10-20: **działa na HDMI** ✅. Nasz własny program dla procesora SE (Sound Effect) gra PCM podawany z Linuksa. Nie potrzebuje żadnego kodu DSP MStara ani tablic z fabrycznego firmware'u, więc programy i binarki z nim można publikować (np. fbDOOM z dźwiękiem, `OWN_SOUND=1`). Wyjście analogowe (SCART) jeszcze nie jest zrobione ❓.

Wszystkie wyniki poniżej są zmierzone na płytce, większość mikrofonem komputera przy głośniku telewizora (nagranie `parecord`, widmo w numpy). Pełny przebieg prób i błędne tropy: `dsp_re/README.md`, sekcja „Sesja 2026-10-20 (trzecia)”.

## Pliki

| Plik | Co |
|---|---|
| `dsp_re/own/pcm.s` | program SE: odbiera przerwanie wyjścia HDMI i wysyła próbki z pierścienia w DM (w publicznym repo: `tools/dsp/pcm.s`) |
| `dsp_re/asm.py`, `dsp_re/isa.py` | nasz asembler i dekoder instrukcji SE (`asm.py pcm.s -o pcm.hex`) |
| `tools/board/own_audio.h` | biblioteka z tym samym API co `mstar_audio.h`: `maudio_open(48000, 2)`, `maudio_write(próbki, ramki)`, `maudio_close()` |
| `tools/board/own_pcm_prog.h`, `gen_own_pcm.py` | program SE jako tablica słów (generowana z `pcm.hex`) |
| `tools/board/owntest.c` | test: 1 kHz, potem 440/660 Hz przez `own_audio.h` |
| `tools/board/dsppcm.c` | odtwarzacz badawczy: plik/stdin s16le stereo 48 kHz albo sygnał testowy; opcje do szukania minimalnego initu (`-n`, `-i`, `-m`, `-H`) |
| `tools/board/idmabench.c` | pomiar szybkości IDMA (ok. 2,2 mln słów/s) |
| `tools/board/doomrun.sh` | uruchamia fbDOOM na `tty2` na zadany czas i oddaje konsolę |
| `fbDOOM/fbdoom` | `make clean; make NOSDL=1 OWN_SOUND=1` = fbDOOM z dźwiękiem przez `own_audio.h` |

## Jak działa wyjście HDMI w SE ✅

- SE zgłasza **przerwanie slotu `0x08`** (wektor `0x09`) raz na ramkę wyjścia HDMI, 48 000 razy na sekundę.
- **Potwierdzenie:** odczyt `DM(0x817F)` i dwa zapisy tej wartości z powrotem, **z instrukcją między zapisami** (oryginał: `AR = AR OR 0`, `AR = AR AND 0xFFFF`). Dwa zapisy jeden po drugim nie potwierdzają i przerwanie zalewa procesor (ok. 850 tys./s, inne przerwania stają).
- **Próbka:** `DM(0x8060)` = lewy kanał, `DM(0x8061)` = prawy, każda jako **słowo 24-bitowe ze znakiem**. PCM 16-bit trzeba przesunąć o 8 bitów w lewo: `SR = LSHIFT AR BY 8 (LO)`, `DM(0x8060) = SR0`. Zapis z rejestru załadowanego wartością 16-bitową daje bity 15:0 przy zerowym górnym bajcie: sygnał ok. 42 dB za cichy, który zachowuje się „jak bez znaku”.
- DMA pierścienia ramek do DRAM, który robi oryginał (`A07E–A081`), **nie jest potrzebne** do HDMI.

## Fakty o SE, które wyszły przy okazji ✅

- **Rejestry i ALU mają 24 bity.** Stałe 16-bit są rozszerzane zerami: `AR = 0x9630` daje dodatnie `0x009630`, więc `IF LT` po `AR − 0x0680` jest fałszem. Porównania „ze znakiem 16-bit” trzeba pisać inaczej (np. licznik zamiast porównania wskaźnika).
- **DM:** `0x0000–0x2FFF` (12 K słów), bez luster; `0x3000` i wyżej nie istnieje (czyta 0).
- **IDMA z MIPS-a do DM SE:** adres `| 0x8000` do `0x112A84`, potem na każde słowo 4 zapisy bajtów do `0x112A82/83` (bajt 1, bajt 2, bajt 0, 0) i sprawdzenie zajętości (`0x112A80` bit 4). Adres sam się zwiększa. Działa w trakcie pracy DSP; ok. 2,2 mln słów/s (stereo 48 kHz potrzebuje 96 tys./s).
- Słowo 24-bitowe wpisane przez IDMA i przeniesione `MR0 = DM(...)`, `DM(0x8060) = MR0` trafia na wyjście w całości (24 bity).

## Program `pcm.s`

- Pierścień w DM `0x1000–0x1FFF`: 2048 ramek, L i P na przemian, słowa = `pcm << 8`. Odczyt przez DAG z buforem kołowym (`I5 = 0x1000`, `M5 = 1`, `L5 = 0x1000`).
- Obsługa `0x08`: potwierdzenie, `MR0 = DM(I5, M5)` → `0x8060`, `MR0 = DM(I5, M5)` → `0x8061`, licznik ramek w `DM(0x2B20)`.
- Pętla główna kopiuje licznik ramek do skrzynki `0x112DE0` (DM `0xB070`); MIPS z niego wie, ile DSP już zagrał.
- Pozostałe przerwania: samo `RTI`. Wektory i init (rejestry IO, IMASK, zegar) jak w naszych wcześniejszych programach testowych.

## Strona Linuksa: `own_audio.h`

1. **Minimalny init bloku audio** (ustalony automatyczną bisekcją, poniżej).
2. Ścieżka HDMI jak `hdmi_audio_on()` w `mstar_audio.h` (banki `0x1131`/`0x1133`) i odciszenie GPIO44.
3. Reset SE, wgranie programu przez IDMA (słowo 0 na końcu, bo od niego SE startuje), wyzerowanie pierścienia.
4. `maudio_write` zamienia próbki na słowa 24-bit i dopisuje je przez IDMA, trzymając 1536 ramek (32 ms) przed odczytem DSP.

Pomiar: 48 000 ramek/s, ton 1 kHz czysty (harmoniczne ok. 2 %, jak u oryginału), przejścia 440/660 Hz bez trzasków. Gra po zimnym starcie płytki i po wcześniejszym użyciu toru MStara (`audiotest3`).

## Minimalny init ✅

Tabela fabrycznego initu (`MDrv_AUDIO_WriteInitTable`, 192 zapisy) po zimnym starcie jest potrzebna, bo bez niej jest cisza, a IDMA nie działa. Wystarczą jednak z niej **4 zapisy**. Ustaliła to bisekcja ddmin: ok. 80 testów, każdy z restartem płytki, odtworzeniem tonu i oceną piku 1 kHz w nagraniu.

| Rejestr RIU | Zapis |
|---|---|
| `0x112C00` | `3`, potem `0` (reset bloku audio, przed resztą) |
| `0x110C80` | `0x0E` (bank `0x110C`, prawdopodobnie zegary) |
| `0x112CA4` | `0xFF` |
| `0x112C48` | bit 7 = 1 |
| `0x112CB1` | bit 2 = 1 |
| `0x112D31`, `0x112B42` | włączenie: bit 1 = 0, `0` |

`mad2_setmeminfo_se` (okno DRAM SE) nie jest potrzebne. `hdmi_audio_on` jest potrzebne: bez niego DSP liczy ramki, ale jest cisza.

## fbDOOM

`make clean; make NOSDL=1 OWN_SOUND=1` (Makefile w `fbDOOM/fbdoom`, `i_mstarsound.c` dołącza wtedy `own_audio.h`). Na płytce `/home/fbdoom_own`. Nagranie dema: głośność zmienia się w czasie tak samo jak w wersji z kodem MStara (`/home/fbdoom`), ale głośniej (średnio 56 wobec 48), a `-nosound` daje płaskie nagranie (44). fbDOOM przełącza konsolę szeregową w tryb surowy, więc po grze trzeba wpisać `^C` i `stty sane` (robi to `doomrun.sh`).

## W systemie z flasha (od 2026-10-20)

Rootfs mini systemu we flashu (`docs/plan_mini_system.md`) ma `/home/fbdoom` zbudowany statycznie z musl i `OWN_SOUND=1` oraz `freedoom_e1m1.wad` (okrojony Freedoom, BSD-3). Obraz nie zawiera więc kodu MStara. Polecenie `doom` gra z dźwiękiem na HDMI. Sprawdzone przed zapisem (z `sda3`): gra bez błędów, ton `owntest` (statycznie z musl) na HDMI.

## Do zrobienia

- Wyjście analogowe (SCART): które porty SE / AUOUT idą na DAC.
- Inne częstotliwości niż 48 kHz (przepróbkowanie po stronie MIPS-a albo w DSP).
- Sterownik ALSA w jądrze zamiast `/dev/mem`.
