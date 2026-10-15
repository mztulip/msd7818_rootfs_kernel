# Skąd pochodzi kod DSP audio i dlaczego go nie publikujemy

Dotyczy nagłówków `tools/board/dsp_fw.h`, `dec03_fw.h`, `se31_fw.h`, `xpcm_fw.h` i `audio_init_table.h`. Bez nich nie działa dźwięk z Linuksa (`mstar_audio.h`, `wavplay`, fbDOOM z dźwiękiem). Opis działania samego dźwięku jest w [audio.md](audio.md), a rdzeni procesorowych w [rdzenie_firmware.md](rdzenie_firmware.md).

## Skąd: z naszego własnego dekodera, nie z internetu

Kodu DSP nie pobieraliśmy z sieci. W 100% pochodzi z flasha naszego egzemplarza WIWA HD95. Droga:

### 1. Zrzut flasha

Na początku projektu cały 4 MB flash został skopiowany na pendrive z poziomu MBoota (`uboot_flashbackup`):
```
spi_rdc 80000000 0 400000
fatwrite usb 0 80000000 backup.bin 400000
```
Wynik: `binaries/wiwa_hd95_BACKUP.BIN`.

### 2. Rozpakowanie aplikacji

Pod offsetem `0x8001C` we flashu jest spakowana LZMA aplikacja eCos, czyli całe oryginalne oprogramowanie dekodera (menu, odtwarzacz, telewizja). Wynika to z `bootcmd` MBoota: `spi_rdc 0x80B00000 0x8001C 0x289977; LzmaDec …; go 0x80000224`. Binwalk ją rozpakował do `extractions/wiwa_hd95_BACKUP.BIN.extracted/8001C/decompressed.bin` (7 324 916 B). To ten sam kod, który MBoot ładuje do RAM pod `0x80000180`.

### 3. Analiza w Ghidrze

`decompressed.bin` załadowany do Ghidry pod `0x80000180` i zdekompilowany (`binaries/ghidra_stock/all.c`, 11 497 funkcji). Dalej:
- **Funkcja ładująca kod do DSP.** Znaleziona po napisach: `"K1 DEC system"`, `"K1 SE system"`, `"xpcm"`, `"mpeg_ad"`, `"mp3"` i komunikatach `HAL_AUDSP_DspLoadCode`. To `HAL_AUDSP_DspLoadCode` pod `0x800be2cc`.
- **Tabele `pau_info`.** Ta funkcja wybiera wpis z tablic opisujących moduły DSP. Wpis ma 13 słów: dla każdego z czterech segmentów (CM, PM, X1, X2) adres w DSP, długość i adres źródła w obrazie, na końcu wskaźnik na nazwę. Przykład: wpis „xpcm” mówi, że CM ma 0x1A88 B, trafia pod adres DSP 0x200 i leży w obrazie pod `0x80604a7a`. Adresy tabel:

  | Tabela | Adres |
  |---|---|
  | „K1 SE system” (0x60) | `0x804F6FB8` |
  | „K1 DEC system” (0x50) | `0x804F6FEC` |
  | dekodery SE (0x3x) | `0x804F725C` + n·0x34 |
  | dekodery DEC | `0x804F7500` + typ·0x34 |

  Spis: `binaries/ghidra_stock/dsp_pau_tables.txt`.
- **Wycięcie kodu.** Bajty wycięte z `decompressed.bin` według tych tabel. Pierwszy wycinek (`binaries/audio_dsp/`) miał długości ×3, bo zostały wzięte za liczbę 24-bitowych słów, a to są bajty. Wyszło to przy ładowaniu do DSP.
- **Tablica inicjalizacji audio.** Pochodzi z `MDrv_AUDIO_WriteInitTable` (`0x80008758` → `0x8000d110`), która przechodzi tablicę pod `0x80371030`: wpisy po 8 B (u32 rejestr, maska, wartość), koniec na `0xFFFF` z maską 0.

### 4. Nagłówki C

Wycięte bajty zamienione na tablice C (`static const uint8_t xpcm_cm[6792] = {…}`), wkompilowywane w programy.

**`tools/board/extract_audio_fw.py` robi kroki 2–4 automatycznie** z dowolnego zrzutu flasha (`python3 extract_audio_fw.py backup.bin tools/board`). Jego wynik jest bajt w bajt identyczny z naszymi nagłówkami (sprawdzone 2026-10-15).

### Co faktycznie było z internetu

Tylko materiały pomocnicze, nie sam kod DSP:
- `mstar-mboot-master/`: źródła MBoota MStara z GitHuba. Z nich wzięły się m.in. nazwy funkcji i kolejność startu;
- biblioteki BSP Kronusa (`mstar-mboot-master/MstarCore/bsp/k1/lib/*.a`) z symbolami. `tools/match_libs.py` dopasował je do funkcji w zrzutach, stąd nazwy typu `MDrv_SOUND_Init`;
- ogólna wiedza o tym, jak MStar organizuje audio (DEC/SE, `pau_info`, IDMA). Na płytce weryfikowały ją dekompilacja i sondy ([sondy_stock.md](sondy_stock.md)).

## Dlaczego nie publikujemy kodu DSP ani binarek z dźwiękiem

To nie jest porada prawna, tylko podsumowanie, czym się kierujemy.

- **Autor i prawo autorskie.** Kod DSP napisał MStar (dziś MediaTek) jako część SDK dla producentów dekoderów. WIWA dostała go w ramach umowy z MStarem i wkompilowała w swój firmware. Prawo autorskie chroni program od chwili napisania, bez żadnej umowy. Brak licencji czy EULA przy dekoderze oznacza tylko, że nabywca nie dostał dodatkowych praw.
- **Co wolno jako właściciel egzemplarza.** Kupując dekoder, kupuje się ten egzemplarz z oprogramowaniem: można go używać, odsprzedać razem z urządzeniem i badać. Art. 75 polskiej ustawy o prawie autorskim i dyrektywa 2009/24/WE wprost dopuszczają dekompilację w celu interoperacyjności. To chroni naszą analizę, dokumentację i własne sterowniki.
- **Czego nie wolno.** Wrzucenie kodu (albo binarek z wkompilowanym kodem) do sieci to „udostępnianie publiczne”, czyli osobne prawo autora, które z zakupu nie wynika. Przepis o dekompilacji wyraźnie nie pozwala rozpowszechniać samego oryginalnego kodu.
- **Jak robią to inni.** `b43-fwcutter` wycina firmware Broadcoma u użytkownika, tak jak nasz `extract_audio_fw.py`. `linux-firmware` zawiera tylko pliki z wyraźną zgodą producenta (plik `WHENCE`).
- **Ryzyko w praktyce.** Pozew za hobbystyczne repo jest mało prawdopodobny. Realne jest zgłoszenie DMCA do GitHuba, po którym znika całe repo, nie jeden plik. Część dekoderów w tym firmware'ie (np. Dolby AC3) ma do tego licencje firm trzecich. My używamy tylko kodu systemowego, MPEG i PCM.

Decyzja na 2026-10-15: kodu DSP i binarek z dźwiękiem nie publikujemy. Publiczne repo ma `extract_audio_fw.py`, a każdy generuje nagłówki ze swojego backupu flasha, który i tak jest potrzebny do przywrócenia oryginału. Gdyby kiedyś binarki z dźwiękiem miały trafić do sieci, to w osobnym repo albo release, żeby ewentualne zgłoszenie nie zdjęło głównego repo.
