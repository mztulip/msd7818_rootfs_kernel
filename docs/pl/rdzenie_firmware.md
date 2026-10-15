# Rdzenie procesorowe w MSD7818 i ich firmware

Przegląd wszystkich programowalnych rdzeni w SoC: co robią, skąd biorą program i jak to wygląda pod oryginalnym firmware i pod naszym Linuksem. Szczegóły są w dokumentach podlinkowanych w tabeli.

Oznaczenia: ✅ potwierdzone na płytce, 🔍 z kodu, ❓ hipoteza.

## Tabela

| Rdzeń | Do czego | Program (firmware) | Kto ładuje i uruchamia | Pod Linuksem | Opis |
|---|---|---|---|---|---|
| **MIPS 34Kc** (2 wątki sprzętowe, 552 MHz) | główny procesor | sboot → MBoot (U-Boot 1.1.6) → oryginalna aplikacja eCos **albo** nasz Linux | boot ROM / sboot z flasha | ✅ Linux 6.7 z pendrive'a | `README`, `mboot_analiza` |
| **PM51** (8051 nr 1, domena zasilania) | czuwanie i wybudzanie, dekoder IR pilota, SAR, RTC, sterowanie panelem przednim | `binaries/pm51/pm51_fw_raw64k.bin`: flash `0x10000` (ok. 45–57 KB, przed MBootem) | po włączeniu wykonuje tylko krótki kod z początku flasha (`LJMP 0x0B00`), oddaje start MIPS-owi i stoi. Właściwy firmware z `0x10000` włącza MIPS (`MDrv_PM_SetSPIOffsetForMCU`) 🔍 | **prawdopodobnie nie działa** ❓: MBoot go nie uruchamia, więc nikt nie obsługuje dekodera IR | [ir_pilot.md](ir_pilot.md) |
| **8051 demodulatora** (8051 nr 2) | steruje torem DVB-T/DVB-C: AGC, synchronizacja, „lock”, skrzynka poleceń dla MIPS | `INTERN_DVBT_table` (24 448 B) i wariant DVB-C: `binaries/demod_fw/intern_dvbt_fw.bin`, `intern_dvbc_fw.bin` (z `libdrvDEMOD.a`, ta sama zawartość w aplikacji eCos) | ładuje MIPS (oryginalna aplikacja) 🔍 | nikt go nie ładuje, więc stoi; do odbioru DVB-T potrzebny byłby sterownik, który go załaduje | [demodulator_if.md](demodulator_if.md), [skanowanie_kanalow.md](skanowanie_kanalow.md) |
| **DSP audio DEC** | dekodowanie dźwięku: MPEG, MP3, AC3, AAC, PCM (xPCM)… | moduły z aplikacji eCos (tablice `pau_info`): kod systemowy **0x50** + wymienny dekoder (**0x03** mpeg przy starcie, **0x0B** xPCM do PCM, 0x07 mp3…) | MIPS przez port IDMA; dane do DRAM przez `memcpy` | ✅ ładuje i uruchamia `tools/board/mstar_audio.h` | [audio.md](audio.md), [dzwiek_uzycie.md](dzwiek_uzycie.md) |
| **DSP audio SE** (Sound Effect) | obróbka, głośność, wyjścia HDMI i DAC (SCART) | kod systemowy **0x60** + moduł **0x31** (`mpeg_ad`) | j.w. | ✅ j.w. | j.w. |

### Tor demodulatora to nie procesor

W dokumentach pojawia się „DSP demodulatora”, ale to **sprzętowy łańcuch OFDM**: ADC, mieszacze, FFT, korektor, Viterbi, Reed-Solomon, wyjście MPEG-TS. Nie wykonuje programu. 8051 demodulatora tylko ustawia jego rejestry, przez parametry z tabeli `INTERN_DVBT_DSPREG` (XDATA `0x8300 + n`). Nie ma więc dla niego firmware'u do wyciągania. 🔍

### Inne bloki, które mogą mieć własny rdzeń ❓

W wielu układach MStara dekoder wideo (MVD) ma własny mały procesor z firmware'em. W tym chipie tego nie sprawdzaliśmy. Pamięć MVD (`0x1D00000–0x3A00000` w oryginale) wykorzystujemy na sondy (`0xA3800000`), bo przy samym menu i audio jest wolna.

## Ile jest firmware'ów

Nie „cztery”, tylko:
- **PM51:** jeden blok we flashu (`0x10000`);
- **8051 demodulatora:** jeden program w dwóch wariantach (DVB-T, DVB-C), w aplikacji eCos;
- **DSP audio:** cała biblioteka modułów. Dwa kody systemowe (0x50 DEC, 0x60 SE) i kilkanaście dekoderów/efektów (mpeg, ac3, aac, mp3, wma, flac, vorbis, dts, xpcm, ra8, …), ładowanych zależnie od tego, co gra. Lista z adresami: `binaries/audio_dsp/index.json`, tabele `binaries/ghidra_stock/dsp_pau_tables.txt`;
- **MIPS:** sboot, MBoot i aplikacja eCos (albo nasz Linux).

Wszystko poza PM51 i bootloaderami siedzi w spakowanej (LZMA) aplikacji eCos we flashu pod `0x8001C`.

## Co zmieniliśmy we flashu, a co tylko w RAM

**Oryginalnej aplikacji eCos we flashu nie modyfikowaliśmy.** Badanie jej „od środka” (sondy, `docs/sondy_stock.md`) działa na kopii w RAM:
1. MBoot czyta obraz z flasha do RAM (`spi_rdc`) i rozpakowuje go (`LzmaDec`). Flash zostaje bez zmian.
2. `mw.l` nadpisują w RAM 16 B funkcji `Help` (skok) i wpisują sondę pod `0xA3800000`.
3. `go 0x80000224` uruchamia tę zmienioną kopię.

Po odłączeniu zasilania kopia znika. Normalny start z flasha uruchamia nietknięty oryginał.

Kod DSP audio, którego używa Linux, jest **kopiowany** z obrazu aplikacji do nagłówków C (`tools/board/*_fw.h`) i przy każdym uruchomieniu ładowany do DSP od nowa. Też nic we flashu.

We flashu zmieniło się tylko (za zgodą, z kopiami zapasowymi):

| Co | Gdzie | Jak przywrócić |
|---|---|---|
| logo startowe (Tux v1 zamiast WIWA) | blok `0x70000` (64 KB) | kopia oryginału w `tools/bootlogo/`, instrukcja w [logo_startowe.md](logo_startowe.md) |
| zmienne MBoota (`saveenv`): `bootcmd`/`bootlinux` (autostart Linuksa z pendrive'a, wyłączenie watchdoga, `logofix`), `LOGO_BIN_SIZE` | blok środowiska MBoota | stary `bootcmd`: `spi_rdc 0x80B00000 0x8001C 0x289977; LzmaDec 0x80B00000 0x289977 0x80000180 0x81000000; go 0x80000224;` |

Pełna kopia flasha sprzed zmian: `binaries/wiwa_hd95_BACKUP.BIN` (nie modyfikować; do prób jest `*_roboczy.BIN`).

## Przyciski i panel przedni (dla porządku)

Fizyczne przyciski WIWA HD95 (POWER, CH+, CH−) i 4-cyfrowy wyświetlacz obsługuje układ **FD650** na panelu (✅), a nie SAR ADC. Piny SAR 1/2 SoC-a pracują jako GPIO: linie DIO/CLK do FD650 ([gpio.md](gpio.md)). Pod Linuksem obsługuje to `mstar-fd650-panel.ko`. Opis SAR z drabinką rezystorów w [przyciski_sar.md](przyciski_sar.md) dotyczy płyty referencyjnej MStara.
