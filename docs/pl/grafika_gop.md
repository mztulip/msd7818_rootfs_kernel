# Grafika: GOP (Graphics Output Path)

## Stan obecny

- MBoot konfiguruje wyjście HDMI i warstwę graficzną **GOP** (GOP3) już przy wyświetlaniu logo. Linux używa `simple-framebuffer` (sterownik `simpledrm`) na tym samym buforze: `0x82100000`, 1920×1080, RGB565, stride 3840 (`msd7816-test.dts`) ✅.
- Zamiast `osd_create 1 1` przed startem kernela uruchamiany jest `logofix` (sekcja [Logo WIWA przy starcie](#logo-wiwa-przy-starcie-logofix-)). `osd_create` ustawiał format **ARGB4444**, a nie RGB565, i psuł logo.
- Od 2026-10-14 jest sterownik `mstar-gopfb` z podwójnym buforowaniem i vsync ([sekcja niżej](#podwójne-buforowanie-sterownik-mstar-gopfb--2026-10-14)). Nie da się jeszcze zmienić rozmiaru obrazu (skalowanie GOP). Pomysł wykorzystania skalowania dla Dooma: [akceleracja_grafiki.md](akceleracja_grafiki.md).
- W DTS bufor ma zarezerwowane `fb-memory@2100000`, 4 MB. Jedna klatka 1080p RGB565 to 4 147 200 B, czyli prawie całość. Wolny eksperymentalny obszar to `tsukasa@3c00000` (4 MB).

## Źródło wiedzy

Biblioteka `mstar-mboot-master/MstarCore/bsp/k1/lib/libapiGOP.a` (Kronus) ma symbole. Analizowane funkcje 🔍:

| Funkcja | Co robi |
|---|---|
| `HAL_GOP_Write16Reg` / `Read16Reg` | Przeliczenie adresu i przełączanie banków |
| `HAL_GOP_BANK_SEL`, `HAL_GOP_Get_BANK` | Rejestr wyboru banku |
| `GOP_GWIN_TriggerRegWriteIn`, `HAL_GOP_GetGOPACK` | Zatrzaśnięcie zmian na vsync i potwierdzenie |
| `MDrv_GOP_GWIN_SetGwinInfo` | Adres bufora, pitch, okno |
| `MDrv_GOP_GWIN_SetStretchWin` | Okno rozciągania |
| `MDrv_GOP_GWIN_Set_HSCALE` / `Set_VSCALE` | Współczynniki skalowania |

Niezależne potwierdzenie: sterownik DRM `linux_msd781x/drivers/gpu/drm/mstar/mstar_gop.c` (dla SigmaStar SSD20x, ARM) ma **te same offsety**. Okno GWIN odpowiada ich `ssd20xd_gop1_data` (hstart `0x10`, hend `0x14`, vstart `0x18`, vend `0x20`, pitch `0x24`), a okno rozciągania leży pod `0xc0`/`0xc4`/`0xc8`/`0xd0`. Ich offsety to offsety bajtowe ARM, czyli numer słowa ×4; po przeliczeniu wychodzą dokładnie rejestry poniżej.

## Adresowanie

Rejestry GOP są pod `0x1F00`–`0x1FFF` (non-PM), czyli **CPU `0xBF203E00`–`0xBF203FFC`**. To jedno okno 128 rejestrów, a jego zawartość zależy od **wybranego banku**.

W bibliotece adres to `(bank << 16) | reg`, a adres CPU liczy się jako `0xBF200000 + reg*2` (patrz [riu.md](riu.md)).

| Bank | Zawartość (wg kolejności MStara; potwierdzone dla 0, 1, 0xC, 0xD) |
|---|---|
| `0x0` | GOP0: sterowanie, okno rozciągania, skalowanie |
| `0x1` | GOP0: okna GWIN (4 okna po `0x40` bajtów rejestrowych) |
| `0x2` | GOP0: ST |
| `0x3`–`0x5` | GOP1 |
| `0x6`–`0x8` | GOP2 |
| `0x9`–`0xB` | GOP3 |
| `0xC` | DWIN (przechwytywanie obrazu) |
| `0xD` | MIXER (mieszanie warstw, timing wyjścia) |

## Rejestr sterujący `0x1FFE` → `0xBF203FFC` 🔍

| Bity | Znaczenie |
|---|---|
| `[3:0]` | numer banku |
| `9` | **force write**: zapisy działają natychmiast (MBoot używa tego przy inicjalizacji) |
| `10` | **trigger**: zatrzaśnij zapisane rejestry na najbliższym vsync |
| `12` | ACK GOP0: `1` = czeka na vsync, `0` = zmiany zastosowane |
| `13` / `14` / `15` | ACK GOP1 / GOP2 / GOP3 |

Procedura z `GOP_GWIN_TriggerRegWriteIn`: zapisz `bank | 0x400`, potem `bank`, a potem czekaj, aż bit ACK spadnie do 0.

## Bank 1: okno GWIN0 (GOP0) 🔍

Okno `n` = rejestr + `n*0x40`. Adresy CPU podane dla GWIN0.

| Reg | CPU | Zawartość | Jednostka |
|---|---|---|---|
| `0x1F00` | `0xBF203E00` | sterowanie: bit 0 enable, bity `[7:4]` format (wg sterownika SSD20x) | |
| `0x1F02` + `0x1F04` | `0xBF203E04` + `0xBF203E08` | **adres bufora w DRAM** (32 bity: młodsze 16 + starsze) | bajty / 8, adres fizyczny |
| `0x1F08` | `0xBF203E10` | H start | ❓ jednostka (piksele albo słowa) |
| `0x1F0A` | `0xBF203E14` | H end | ❓ |
| `0x1F0C` | `0xBF203E18` | V start | linie |
| `0x1F10` | `0xBF203E20` | V end | linie |
| `0x1F12` | `0xBF203E24` | **pitch** (stride) | bajty / 8 |
| `0x1F18` + `0x1F1A` | `0xBF203E30` + `0xBF203E34` | przesunięcie pionowe (WinY × pitch) | bajty / 8 |
| `0x1F1C` | `0xBF203E38` | przesunięcie poziome (WinX × Bpp) | bajty / 8 |
| `0x1F20` + `0x1F22` | `0xBF203E40` + `0xBF203E44` | rozmiar bufora (wys. × pitch) | bajty / 8 |

Adres jest liczony od początku DRAM (`0x80000000` w KSEG0 = fizyczne `0x0`). Dla obecnego bufora `0x2100000 / 8 = 0x420000`, więc spodziewamy się **`0x0000` pod `0xBF203E04` i `0x0042` pod `0xBF203E08`**. To dobry test, czy MBoot używa GOP0 i GWIN0.

## Bank 0: rozciąganie i skalowanie (GOP0) 🔍

| Reg | CPU | Zawartość |
|---|---|---|
| `0x1F50` bit 10 | `0xBF203EA0` | czyszczony przy ustawianiu skali V ❓ (tryb filtra?) |
| `0x1F60` | `0xBF203EC0` | szerokość okna rozciągania / 2 |
| `0x1F62` | `0xBF203EC4` | wysokość okna rozciągania |
| `0x1F64` | `0xBF203EC8` | X okna rozciągania |
| `0x1F68` | `0xBF203ED0` | Y okna rozciągania |
| `0x1F6A` | `0xBF203ED4` | **skala H** = `ceil(src × 4096 / dst)`, `0x1000` = 1:1 |
| `0x1F6C` | `0xBF203ED8` | **skala V**, ten sam wzór |
| `0x1F70` | `0xBF203EE0` | faza początkowa H (0) |
| `0x1F72` | `0xBF203EE4` | faza początkowa V (0) |

Okno rozciągania to rozmiar **źródła** (bufora). GOP powiększa go do wyjścia według współczynników skali. Bufor 960×540 na wyjściu 1920×1080 daje skalę `0x0800` w obu osiach.

## Co z tego wynika dla sterownika Linuksa

Timing wyjścia (HDMI/VE/XC) **zostaje taki, jak ustawił MBoot**. Jego zmiana wymaga zupełnie innych bloków i to osobny, dużo trudniejszy temat. Natomiast w samym GOP da się zrobić:

1. **Double/triple buffering + vsync.** `yres_virtual = N × yres`, a `fb_pan_display` zapisuje adres bufora (`0x1F02`/`0x1F04`) i ustawia trigger (bit 10). Z oczekiwaniem na ACK (bit 12) dostajemy `FBIO_WAITFORVSYNC`. Koniec z migotaniem.
2. **Mniejsza rozdzielczość, sprzętowo skalowana do wyjścia**, np. 960×540 albo 640×360. Ustawiamy okno GWIN, pitch, okno rozciągania i skalę. 4 razy mniej pikseli to szybszy Doom, a 960×540 RGB565 (1 MB) mieści 3 bufory w istniejącym `fb-memory`.
3. Opcjonalnie format 32-bit (ARGB8888), jeśli GOP0 go obsługuje. Kody formatów trzeba ustalić z `MDrv_GOP_GWIN_SetWinFmt` / `MDrv_GOP_GetRegForm`.

Do wyboru są dwie drogi:
- **mały sterownik fbdev** (`kronus-gopfb`): przejmuje stan po MBoocie, dodaje pan, vsync i skalowanie. To ok. 300 linii, a `fbDOOM` działa na nim bez zmian.
- **port `drivers/gpu/drm/mstar`**: offsety pasują, ale sterownik zależy od ARM i całego potoku Infinity (op2, top, mop). Dużo więcej pracy.

## Testy do zrobienia na płycie

W U-Boocie, **po** `osd_create` (z bootcmd):
```
md bf203ffc 1              # stan rejestru banku
mw.w bf203ffc 1            # bank 1 (GWIN)
md bf203e00 20             # spodziewane: E04=0000, E08=0042, E24=pitch/8 (3840/8 = 0x1E0)
mw.w bf203ffc 0            # bank 0
md bf203ec0 10             # okno rozciągania i skala (ED4/ED8 = 0x1000?)
```
Jeśli adres nie wynosi `0x420000`, MBoot używa innego GOP. Wtedy trzeba sprawdzić banki 3/4, 6/7 i 9/A.

Test przesuwania obrazu (pan) w Linuksie przez `devmem`. Adresy fizyczne to `0x1F......` zamiast `0xBF......`:
```sh
devmem 0x1F203FFC 16 0x0201      # bank 1 + force write
devmem 0x1F203E04 16 0x0F00      # adres = 0x420F00*8 → przesunięcie o 0xF00*8/3840 = 8 linii
devmem 0x1F203FFC 16 0x0001      # wyłącz force write
```
Jeśli obraz przesunie się o 8 linii, double buffering będzie działał tak samo. Aby przywrócić, zapisz `0x0000` pod `0x1F203E04`.

Uwaga: nieostrożne zapisy mogą zepsuć obraz do resetu, ale nic nie uszkodzą.

## Wyniki testów na płycie (2026-10-13, Linux, `devmem`, adresy fizyczne `0x1F……`) ✅

Konsola szeregowa: `/dev/ttyUSB0`, **115200** bodów (nie 19200).

- **Bufor Linuksa jest na GOP3, nie na GOP0.** Bank `0x9` = sterowanie GOP3, bank `0xA` = okno GWIN GOP3.
  - bank A: `0x1F00` = `0x3F21` (bit 0 = włączone), `0x1F02`/`0x1F04` = `0x0000`/`0x0042` → adres `0x420000 × 8 = 0x2100000`, `0x1F0A` = `0x01E0`, `0x1F10` = `0x0438` (1080), `0x1F12` = `0x01E0` (pitch 3840/8), `0x1F14` = `0x0003`.
  - bank 9: okno rozciągania `0x1F60` = `0x03C0` (×2 = 1920), `0x1F62` = `0x0438` (1080), skala H `0x1F6A` = `0x1000`, `0x1F6C` = `0x0000`.
  - GOP0 (banki 0/1) ma okno rozciągania 720×576 w X=100, ale GWIN0 jest pusty (warstwa logo MBoota, nieaktywna).
  - Układ okna GOP3 (wg `GOP_SetGop23GwinInfo` i odczytów): `0x1F02`/`0x1F04` adres/8, `0x1F0C` V start, `0x1F10` V end, `0x1F12` pitch/8, `0x1F18`/`0x1F1A` przesunięcie (0).
- Rejestr `0x1FFE` (CPU `0x1F203FFC`) w spoczynku = `0x7000`: bity 12–14 (ACK GOP0–2) = 1, bit 15 (ACK GOP3) = 0.
- **Zmiana wyświetlanego adresu na vsync działa:**
  1. `0x1FFE` = `0x700A` (bank A), zapis `0x1F02`/`0x1F04`,
  2. wyzwolenie: `0x1FFE` = `0x7409`, potem `0x7009`, czyli bit 10 **z bankiem sterującym GOP3 (9)**, nie z bankiem okna (A); z bankiem A nic się nie dzieje,
  3. bit 15 na chwilę = 1 (`0xF009`), potem 0, czyli zmiana zastosowana na vsync,
  4. **nie zmieniać banku, zanim bit 15 spadnie do 0**, bo zmiana zostanie anulowana (tak było przy pierwszej próbie powrotu).
- Przesunięcie o 200 linii (`0x437700`) przesunęło obraz; dolne linie pokazały wtedy szum, bo czytały pamięć za 4-megabajtowym `fb-memory`. Drugi bufor 1080p wymaga powiększenia rezerwacji (≥ 8,3 MB) albo mniejszej rozdzielczości ze skalowaniem.
- **Zapisy przez `/dev/fb0` nie były widoczne,** bo konsola (fbcon) nadpisuje bufor. Po `echo 0 > /sys/class/vtconsole/vtcon1/bind` zapisy bezpośrednio do pamięci (`dd … of=/dev/mem bs=256 seek=135168…`) są widoczne od razu. Przywrócenie konsoli: `echo 1 > …/bind`.
- Inne odczyty z tej sesji: PM51 wyłączony (`0x0E53` = `0x00`), `0x0E24` = `0xBABE`, `0x1018` = `0x000A` (zgodnie z `boot.S`); SAR nieuruchomiony (kanały = 0, `0x2800` = `0x0940`); pinmux I2C `0xBF203C24` = 0; MIIC0 `+0x38` = `0xF8F8`.

## Podwójne buforowanie: sterownik `mstar-gopfb` ✅ (2026-10-14)

### Test bez sterownika: `tools/board/gopflip.c`

Program przełącza GOP3 między buforem `0x2100000` (Linux fb) a `0x3C00000` (zarezerwowany `tsukasa`, mieści jedną klatkę 1080p) przez `/dev/mem`:
- `gopflip vsync 120` → **okres 20,000 ms, 50 Hz**: wyjście HDMI to 1080p50, a oczekiwanie na ACK (bit 15 `0x1FFE`) jest dokładnym sygnałem vsync;
- `gopflip single` (602 kl./s, rysuje w widocznym buforze): pasek się rwie;
- `gopflip double` (dokładnie 50 kl./s, przełączanie na vsync): pasek równy, potwierdzone na ekranie.

### Sterownik

`linux_msd781x/drivers/video/fbdev/mstar-gopfb.c` (`CONFIG_FB_MSTAR_GOP=y`, compatible `mstar,msd7816-gop3-fb`) zastępuje `simple-framebuffer`/simpledrm:
- przejmuje GOP3 po MBoocie i `logofix`: ustawia format RGB565, adres bufora 0 i pitch; ostrzega, gdy zastał format 2 (`osd_create`);
- `/dev/fb0`: 1920×1080 RGB565, **`yres_virtual` = 2160** (liczba buforów = rozmiar `memory-region` / klatka, parametr `buffers`, domyślnie 2);
- `FBIOPAN_DISPLAY`: zapis adresu w banku A i trigger w banku 9. Funkcja nie czeka na vsync, ale przed zmianą banku czeka, aż poprzedni trigger się wykona (zmiana banku przed ACK anuluje zapis);
- `FBIO_WAITFORVSYNC`: czeka na zaległe przełączenie, a gdy żadnego nie ma, wyzwala pusty trigger i czeka na najbliższy vsync;
- fbcon nie używa panningu (bez `FBINFO_HWACCEL_YPAN`), tylko przerysowuje.

DTS (`msd7816-test.dts`):
```
fb_memory: fb-memory@2100000 { reg = <0x2100000 0x900000>; };   /* 2 × 0x3F4800 */

&riu {
	gop3-fb@203e00 {
		compatible = "mstar,msd7816-gop3-fb";
		reg = <0x203e00 0x200>;
		memory-region = <&fb_memory>;
		width = <1920>;
		height = <1080>;
	};
};
```
Węzeł `framebuffer@2100000` (simple-framebuffer) został usunięty, a w konfiguracji jest `# CONFIG_DRM_SIMPLEDRM is not set`. Węzeł `gop@203e00` z `kronus.dtsi` jest w bloku `#if 0`. Ma `interrupts-extended = <&intc_irq 20 …>`, więc to prawdopodobnie przerwanie vsync, które przyda się zamiast odpytywania ❓.

### Wyniki na płycie (kernel `vmlinux_gop.bin`, entry `0x805e87e4`)

- `dmesg`: `mstar-gopfb 1f203e00.gop3-fb: fb0: 1920x1080 RGB565, 2 buffers at 0x02100000`, konsola od startu na `mstar-gop3`.
- `tools/board/fbflip.c` (to samo przez `/dev/fb0`): `fbflip vsync` → 19,95 ms (50,1 Hz). `fbflip single` daje 403 kl./s i rwie się, `fbflip` (podwójny) daje równy pasek.
- Obraz HDMI **nie drga**, mimo że sterownik jest wkompilowany (`=y`). Moduły I2C i panelu ładują się, tuner odpowiada `0x14`.
- Pamięć dla Linuksa: 40 MB (`free -m`).
- fbDOOM i chocolate-doom nie używają jeszcze panningu (rysują w widocznym buforze).

Użycie w programie:
```c
ioctl(fd, FBIOGET_VSCREENINFO, &var);          // yres_virtual = 2 * yres
uint16_t *mem = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
// rysuj w buforze back = mem + back * xres * yres
var.yoffset = back * var.yres;
ioctl(fd, FBIOPAN_DISPLAY, &var);              // przełączenie na najbliższym vsync
ioctl(fd, FBIO_WAITFORVSYNC, &zero);           // po powrocie stary bufor jest wolny
```

## Logo WIWA przy starcie (`logofix`) ✅

### Problem

Przy starcie MBoot pokazuje logo WIWA poprawnie. Po `osd_create 1 1` w `bootlinux` w lewej części ekranu pojawiał się zielony prostokąt, a reszta logo miała złe kolory.

### Przyczyna (sprawdzone na płycie)

1. Logo leży w **tym samym buforze**, którego później używa Linux: `0x2100000`, 1920×1080, pitch 3840, GOP3. Format GWIN to **9 = YUV 4:2:2**, kolejność bajtów **Y0 V Y1 U** (YVYU), pełny zakres (JPEG YCbCr). Ustalone przez zrzut co 16. linii bufora (`md.l`) i porównanie z JPEG-iem z flasha (`0x70000`, 720×576, przeskalowany przez MBoot do 1920×1080).
2. `osd_create 1 1` (oprócz komunikatów `HDMITx`/`DAC eTiming`) zmienia w GOP tylko dwa rejestry: bank A `0x1F00` `0x3F91` → `0x3F21` (format, bity `[7:4]`, 9 → 2) oraz bank 9 `0x1F12` `0x0006` → `0x0004`. Pikseli nie konwertuje, a dodatkowo czyści część bufora, stąd zielony prostokąt (zera w YUV dają zieleń).
3. **Kody formatów GOP** (enum `EN_GOP_COLOR_TYPE` w `MstarCore/bsp/*/include/apiGOP.h`, sprawdzone pasami testowymi): `1` = RGB565, **`2` = ARGB4444**, `5` = ARGB8888, `6` = ARGB1555, `9` = YUV422. Pasy szarości RGB565 (`2104`, `4208`, `8410`, `C618`) w formacie 2 wyglądały jak ciemnoniebieski, niebieski, czerwony i fioletowy, czyli dokładnie jak ARGB4444. W formacie 1 były szare.
4. Kernel maniekx86 był do tego dopasowany. `drm_fb_blit()` w `drivers/gpu/drm/drm_format_helper.c` zawsze konwertował RGB565 → xRGB4444, a DTS deklaruje `r5g6b5`. Linux miał więc w praktyce tylko 4096 kolorów (`fbset`: `rgba 5/11,6/5,5/0`, a w pamięci tekst konsoli to `0x0AAA`).

### Rozwiązanie

- `tools/baremetal/logofix/`: mały program bare-metal (ok. 1 KB, ładowany i uruchamiany pod `0x80F00000` przez `go`), który zastępuje `osd_create 1 1`:
  1. sprawdza, czy GOP3 jest w formacie 9 (inaczej zwraca `rc = 1` i nic nie zmienia);
  2. konwertuje YVYU → RGB565 do bufora pomocniczego `0x2800000`, z ditheringiem Bayera 4×4 (bez niego gradienty logo dają koncentryczne „pierścienie” z zielono-różowym odcieniem);
  3. na vsync przełącza GOP3 na bufor pomocniczy i **format 1**, ustawia bank 9 `0x1F12` = `0x0004` (jak `osd_create`);
  4. kopiuje obraz z powrotem do `0x2100000` i na vsync przełącza adres z powrotem. Logo przez cały czas jest wyświetlane poprawnie.
- Kernel: w `drm_fb_blit()` usunięta wymuszona konwersja do 4444, więc działa zwykły `memcpy` w RGB565. **Ten kernel wymaga `logofix`.** Po starym `osd_create` miałby złe kolory, tak samo jak stary kernel po `logofix` (błękitny tekst konsoli).
- Narzędzia piszące bezpośrednio do fb (`rfscan`) i fbDOOM używają RGB565, więc ich kolory są teraz poprawne.

Budowanie i ręczne uruchomienie:
```sh
sh tools/baremetal/logofix/build.sh          # mipsel-linux-gnu-gcc, wynik logofix.bin (1008 B)
```
```
usb start
fatload usb 0 0x80F00000 logofix.bin
go 0x80F00000                                 # ## Application terminated, rc = 0x0
```
Bez pendrive'a program można też wpisać do RAM poleceniami `mw.l a0f00000 …` (adres **nieskeszowany** `0xA0F00000`, żeby `go` nie wykonał starych danych z cache). Uwaga: po resecie MBoot wypełnia RAM wzorem `0x5A5AA5A5`, więc program trzeba wgrać ponownie.

Sprawdzenie stanu GOP3 (MBoot):
```
mw.w bf203ffc 700a; md.w bf203e00 1; mw.w bf203ffc 7000     # 3F91 = YUV (logo), 3F11 = RGB565, 3F21 = ARGB4444
```
Z Linuksa: `devmem 0x1F203FFC 16 0x700A; devmem 0x1F203E00 16; devmem 0x1F203FFC 16 0x7000`.

Autostart (zapisany `saveenv` 2026-10-14, kernel #8 z RGB565):
```
setenv bootlinux usb start\;fatload usb 0 0x80F00000 logofix.bin\;go 0x80F00000\;fatload usb 0 0x80000000 vmlinux.bin\;go 0x805e86a4\;
saveenv
```
Sprawdzone: po resecie logo jest poprawne przez cały start, Linux ma GOP3 = `0x3F11`, a tekst konsoli w pamięci ma kolor `0xAD55` (szary RGB565; stary kernel dawał `0x0AAA`).

Test pasami (MBoot, format 1): `mw.w 0xA2100000 f800 65400` (czerwony, 216 linii), kolejne pasy co `0xCA800` bajtów.
