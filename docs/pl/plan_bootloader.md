# Plan: własny bootloader z menu (odpowiednik GRUB-a)

Ustalony 2026-10-19. **Etapy 1–3 napisane 2026-10-20. Etapy 1 i 2 sprawdzone na płytce 2026-10-20 (test z RAM, bez zapisu do flasha); etap 3 (ekran, przyciski panelu) jeszcze nie** (kod: `tools/baremetal/bootsel/`, wyniki niżej w „Test z RAM”). Kontekst: od 2026-10-19 płytka startuje z flasha ([plan_mini_system.md](plan_mini_system.md)), a system z pendrive'a uruchamia się tylko ręcznie (Enter przy starcie, `run wdoff; run bootlinux`).

## Cel

Po włączeniu zasilania nasz program pokazuje menu i pozwala wybrać, co uruchomić:
1. system z flasha (domyślnie, po kilku sekundach);
2. kernel z pendrive'a, jeśli jest pendrive z odpowiednim plikiem;
3. ewentualnie konsolę MBoota (powrót do niej).

Wybór klawiszem z konsoli UART albo przyciskiem panelu przedniego (POWER / CH+ / CH−), później może też klawiaturą USB.

## Dlaczego nie sam skrypt MBoota

MBoot (U-Boot 1.1.6 MStara) nie ma instrukcji warunkowych: brak `if`/`test`, brak interpretera hush (`mboot_comnmands`, napisy w `RAM87_DU.RAM`). Linia polecenia jest ucinana po ok. 128 znakach. `LzmaDec` przy złej sumie CRC zapisuje środowisko i resetuje płytkę. Logiki „jest pendrive → stąd, nie ma → z flasha” nie da się więc zapisać w samym `bootcmd`.

## Wariant A: własny mały bootloader (wybrany)

MBoot robi tylko to, co już umie: watchdog, USB, logo, ewentualne wczytanie pliku z pendrive'a, a potem uruchamia nasz program. Decyzję podejmuje nasz program.

### Przebieg startu

```
MBoot bootcmd:
  run wdoff
  usb start
  mw.l 0x81000000 0 1                         ← „brak kernela z pendrive'a”
  fatload usb 0 0x81000000 vmlinux.bin        ← opcjonalnie; bez pendrive'a/pliku zwykły błąd i dalej
  run flashlogo                               ← logofix z flasha (logo YUV → RGB565)
  spi_rdc <adres bootloadera> …; go <adres>   ← nasz bootloader
  run flashboot                               ← awaryjnie, gdyby bootloader wrócił do MBoota
```

Nasz bootloader:
- sprawdza, czy pod `0x81000000` leży kernel (pierwsza instrukcja to `j kernel_entry`, bo nasze kernele mają `CONFIG_BOOT_RAW`);
- wypisuje menu na UART i na ekranie (bufor GOP3 `0x2100000`, RGB565 po `logofix`, prosty font 8×16);
- czeka np. 3 s na klawisz z UART (`0xBF201300`) albo przycisk panelu ([przyciski_sar.md](przyciski_sar.md)), po czasie wybiera pozycję domyślną;
- **system z flasha:** sam rozpakowuje kernel LZMA z okna pamięci flasha (`0xBFC80000…`, [flash_spi.md](flash_spi.md)) pod `0x80000000`, sprawdza CRC32 i skacze pod `0x80000000`. Dekoder z LZMA SDK (domena publiczna, kilka KB). `LzmaDec` MBoota nie jest wtedy potrzebny;
- **pendrive:** kopiuje kernel z `0x81000000` pod `0x80000000` i skacze pod `0x80000000`;
- **konsola MBoota:** wraca (`rc`), MBoot wykonuje dalszą część `bootcmd`; do zastanowienia, jak wtedy zatrzymać się w konsoli zamiast `run flashboot`.

Obsługa USB i FAT zostaje w MBoocie, więc nasz program nie potrzebuje własnych sterowników USB ani systemu plików. Każdy kernel ma swój DTB z linią poleceń (`root=/dev/mtdblock4` dla flasha, `root=/dev/sda2` dla systemu z pendrive'a), więc bootloader nie musi jej przekazywać.

### Pamięć i flash

| Co | Gdzie |
|---|---|
| bootloader we flashu | blok `0x1D0000` razem z `logofix` (wolne ok. 63 KB) albo osobny blok 64 KB |
| bootloader w RAM | np. `0x80F00000` (tam gdzie `logofix`) albo `0x80F10000` |
| kernel z pendrive'a (wczytany przez MBoot) | `0x81000000` (do 8 MB, kernel #13 ma 8,9 MB, więc raczej `0x80900000`… do ustalenia, żeby nie zachodził na cel `0x80000000–0x808xxxxx`) |
| kernel docelowy | `0x80000000` |
| bufor roboczy LZMA | np. `0x82000000` |

Rozmiary trzeba dobrać tak, żeby wczytany kernel z pendrive'a, cel `0x80000000` i bufor LZMA na siebie nie zachodziły (pełny kernel #13 ma 8,9 MB nieskompresowany). Pamięć podręczną przed skokiem do kernela trzeba opróżnić (D-cache zapis, I-cache unieważnienie; MBoot ma włączone L2).

### Etapy

1. **Bootloader tylko z UART, start z flasha.** Program bare-metal (jak `tools/baremetal/logofix/`), dekoder LZMA, sprawdzenie CRC32, skok do kernela. Test z RAM: `fatload` bootloadera z pendrive'a i `go`, bez zapisu do flasha.
   *Gotowe, gdy:* bootloader uruchamia system z flasha tak jak dziś `run flashboot`.
2. **Pendrive i menu na UART.** `fatload … vmlinux.bin` w MBoocie, wykrywanie kernela pod adresem wczytania, menu z wyborem klawiszem i czasem oczekiwania.
   *Gotowe, gdy:* z pendrive'em i bez niego da się wybrać i uruchomić oba systemy.
3. **Menu na ekranie i przyciski panelu.** Tekst w buforze GOP, odczyt przycisków SAR.
   *Gotowe, gdy:* da się wybrać system bez konsoli szeregowej.
4. **Zapis do flasha i nowy `bootcmd`** (za zgodą). Zachować `flashboot` jako awaryjny start, gdyby bootloader nie wystartował.
   *Gotowe, gdy:* płytka po włączeniu pokazuje menu.
5. Opcjonalnie: klawiatura USB w menu (wymagałaby obsługi USB HID w bootloaderze, więc raczej później albo wcale), zapamiętywanie ostatniego wyboru.

## Wariant B: prawdziwy U-Boot jako drugi etap (alternatywa)

Mainline U-Boot uruchamiany przez MBoot dałby pełne skrypty (`if`, `bootmenu`), własne ładowanie z USB i z flasha. Wymaga przeniesienia sterowników płytki: konsola (16550, gotowa), USB (FOTG210 jest w U-Boocie), flash (nasz `spi-mstar-isp` do przeniesienia), ewentualnie ekran. To praca na kilka dłuższych sesji. Do rozważenia, jeśli wariant A okaże się za mało elastyczny.

## Implementacja (2026-10-20): `tools/baremetal/bootsel/`

| Plik | Co robi |
|---|---|
| `lzma.c`, `lzma.h` | własny dekoder LZMA („LZMA alone”) napisany według specyfikacji formatu, licencja MIT. Wyjście jest jednocześnie słownikiem, potrzebuje tylko tablicy prawdopodobieństw (~30 KB) |
| `test_lzma.c` | test dekodera na PC: `gcc -O2 -o test_lzma test_lzma.c lzma.c` |
| `bootsel.c` | menu na UART0, wykrywanie kernela wczytanego z pendrive'a, start z flasha albo z pendrive'a |
| `bootsel.ld`, `build.sh` | linkowanie pod `0x80F00400`; `build.sh` robi `bootsel.bin` i `bootblk.img` (64 KB: `logofix.bin` @0, `bootsel.bin` @`0x400`, do zapisu pod `0x1D0000`) |

**Sprawdzone na PC:** dekoder rozpakowuje nasz `kernel.lzma` z flasha bit w bit (wynik = `vmlinux_flash.bin` + CRC32), także pliki z nieznanym rozmiarem w nagłówku (znacznik końca), dane losowe i różne `lc/lp/pb`. `bootsel.bin`: 3,9 KB kodu, 29 KB `.bss` (RAM `0x80F00400–~0x80F09000`), bez odwołań do biblioteki C.

**Działanie `bootsel`:**
- kernel z pendrive'a rozpoznaje po pierwszej instrukcji `j kernel_entry` (`CONFIG_BOOT_RAW`) pod `0x81000000` (`bootcmd` czyści to miejsce przed `fatload`);
- menu: `1` (albo Enter) = flash, `2` = pendrive (tylko gdy wczytany), po 3 s domyślnie flash; odliczanie na UART;
- flash: dekoduje LZMA z okna `0x9FC80000` (flash `0x80000`, przez cache) do `0x82000000`, sprawdza CRC32 z ostatnich 4 bajtów; dopiero potem wyłącza przerwania (`di`), opróżnia L1 D/I, kopiuje pod `0x80000000`, opróżnia cache i skacze z `a0..a3 = 0`;
- pendrive: to samo kopiowanie z `0x81000000` (14 MB);
- błąd (zły nagłówek, LZMA, CRC, brak kernela): powrót do MBoota z `rc` 2–5, więc dalsza część `bootcmd` może zrobić `run flashboot`.

**Do sprawdzenia na płytce:** czy `di` + opróżnienie L1 wystarcza (MBoot ma włączony L2), czas dekodowania LZMA na MIPS-ie (na PC ok. 0,1 s), czy kernel startuje poprawnie po `bootsel`, czy przyciski panelu reagują (FD650 przed Linuksem: MBoot go nie inicjalizuje) i czy rysowanie bez cache nie jest za wolne (ok. 210 tys. pikseli ramki).

### Który rootfs montuje kernel (ustalone 2026-10-20)

Zasada: **rootfs jest na tym samym nośniku co kernel**. Nie trzeba tego przekazywać z `bootsel`: każdy kernel ma wbudowany DTB z linią poleceń (`CONFIG_MIPS_CMDLINE_FROM_DTB`, argumenty od bootloadera są ignorowane, a `bootsel` i tak daje `a0..a3 = 0`):

| Pozycja menu | Kernel | `root=` z DTB |
|---|---|---|
| 1 flash | `linux_mini` (LZMA pod `0x80000`) | `/dev/mtdblock4 rootfstype=squashfs ro` (squashfs pod `0x1E0000`) |
| 2 pendrive | `vmlinux.bin` z FAT pendrive'a (drzewo `linux_msd781x`) | `/dev/sda2 rootwait rw` |

Stały układ pendrive'a (taki jak dotąd): **partycja 1 FAT32 z `vmlinux.bin`, partycja 2 ext2 z rootfs**. Ograniczenia:
- `vmlinux.bin` na pendrivie musi mieć DTB z `root=/dev/sda2` (kernel z `linux_msd781x`). Kernel z `linux_mini` wgrany na pendrive zamontowałby rootfs z flasha;
- pendrive z `vmlinux.bin`, ale bez partycji 2, zatrzyma start na `rootwait` (kernel czeka bez końca);
- `sda` zakłada, że to jedyna pamięć USB (jedno gniazdo USB, więc w praktyce tak jest).

Gdyby kiedyś trzeba było wybierać rootfs niezależnie od kernela (np. kernel z flasha + rootfs z pendrive'a), `bootsel` może przekazać linię poleceń: w `arch/mips/mstar/mstar.c` (`prom_init`) skopiować napis z `a1` do `arcs_cmdline`, gdy `a0` ma umówioną wartość, i włączyć `CONFIG_MIPS_CMDLINE_DTB_EXTEND` (napis od bootloadera jest dopisywany po `bootargs` z DTB, a ostatnie `root=` wygrywa). Stare kernele dalej działałyby ze swoim DTB. Na razie niepotrzebne.

### Etap 3 (napisany 2026-10-20, nietestowany): menu na ekranie i przyciski panelu

Gotowe: `mkfont.py` generuje `font.h`, czyli czcionkę 16×32 (DejaVu Sans Mono Bold, licencja Bitstream Vera) dla znaków ASCII 32…126. Jeden znak to 32 wiersze `uint16_t`, bit 15 to lewy piksel, razem ok. 6 KB w binarce.

Napisane w `bootsel.c` (`bootsel.bin` 12,8 KB, `.bss` 29 KB, RAM `0x80F00400…0x80F0A900`; wygląd menu sprawdzony renderem na PC tą samą czcionką). Na ekranie: ramka 688×304 px na środku logo, tytuł, pozycje 1/2 (wybrana na niebiesko; bez pendrive'a „(no vmlinux.bin on USB)”), podpowiedź, linia stanu (odliczanie, „Decoding the flash kernel...”, „Starting the kernel...”, błędy na czerwono). Na wyświetlaczu panelu: `FL 3`/`USb3` (wybór + odliczanie), `boot` przy starcie, `Err` przy błędzie. UART działa jak wcześniej (`1`, `2`, Enter = wybrana pozycja). Kopia kernela pod `0x80000000` ma limit 15 MB (`KERNEL_MAX`), żeby nie nadpisać samego `bootsel`.

Ustalenia:
- **ekran:** rysowanie przez KSEG1 `0xA2100000` (bez cache, tak jak `logofix`), pitch 3840, RGB565. Ciemna ramka na środku logo, tytuł „MSD7818 boot menu”, pozycje menu z podświetleniem wybranej, podpowiedź z klawiszami, odliczanie, komunikaty („decoding…”, błędy);
- **przyciski:** FD650 obsługiwany bit-bangiem tak jak w `tools/board/frontpanel.c`: strona PM `0xBF002000`, `SAR_ANA` 0x844 (bity 1, 2 = 0), `OEN` 0x845, `OUT` 0x848, `IN` 0x849, DIO = 0x02, CLK = 0x04. Opóźnienia ok. 10 µs z CP0 Count (2760 taktów), bo nie ma `nanosleep`. Na starcie `S 0x48 0x11 P`, odczyt klawisza `S 0x4F` + 8 bitów + `P`, odpytywanie co ok. 50 ms;
- **klawisze:** id = kod & 0x3F, bit 6 = wciśnięty: 0x0F CH+ i 0x07 CH− przesuwają wybór, 0x17 POWER uruchamia. Każdy klawisz (panel albo UART) zatrzymuje odliczanie. Można też pokazać wybór i odliczanie na wyświetlaczu LED panelu;
- **pamięć:** `TMP_ADDR` przeniesiony z `0x82000000` na `0x82A00000`, bo stary bufor LZMA zachodzi na framebuffer `0x2100000…0x2A00000` (2 bufory gopfb). `0x82A00000 + 16 MB` kończy się pod `0x83A00000`, czyli przed końcem RAM.

### Test z RAM (bez zapisu do flasha)

Na FAT pendrive'a: `bootsel.bin`; do opcji 2 wystarczy dotychczasowy `vmlinux.bin` (kernel #13, rootfs z `sda2`). Plik kernela z pendrive'a to `vmlinux.bin` (wybór użytkownika 2026-10-20): każdy pendrive z `vmlinux.bin` daje w menu opcję 2, domyślnie i tak startuje flash. MBoot wczytuje ten plik przed menu przy każdym starcie z pendrive'em (8,9 MB dla #13, kilka sekund). W MBoocie (Enter przy starcie):
```
run wdoff
usb start
mw.l 0x81000000 0 1
fatload usb 0 0x81000000 vmlinux.bin
run flashlogo
fatload usb 0 0x80F00400 bootsel.bin
go 0x80F00400
```
Oczekiwane: menu na UART, na ekranie i na panelu, po 3 s `1` → system z flasha (`msd7818 login:`); z `2` → system z pendrive'a (`buildroot login:`). Bez `vmlinux.bin` menu pokazuje tylko opcję 1.

**Wyniki na płytce (2026-10-20, ✅ sprawdzone na płytce, tylko UART):**

| Test | Wynik |
|---|---|
| 1. bez klawisza, pendrive z `vmlinux.bin` | ✅ menu z opcjami 1 i 2, odliczanie `3 2 1`, `flash kernel, 0x003c7908 bytes, decoding... CRC ok`, start kernela z flasha (#4, `root=/dev/mtdblock4`), powłoka po 8,7 s od `go` |
| 2. klawisz `2` na UART | ✅ odliczanie zatrzymane, kopia z `0x81000000`, start kernela #13 z pendrive'a (`root=/dev/sda2`, `buildroot login:`) |
| 3. przyciski panelu | ❓ niesprawdzone: w próbie żaden klawisz nie dotarł (odliczanie doszło do końca, start z flasha), ale użytkownik nie miał podłączonego monitora i nie wiadomo, czy przycisk był wciśnięty w ciągu 3 s |
| 4. bez `fatload vmlinux.bin` | ✅ menu tylko z opcją 1 (`no kernel from the USB stick`), start z flasha |

Dekodowanie LZMA (3,9 MB wyniku) z CRC32 trwa na płytce ok. **1,5 s** (od `decoding...` do `CRC ok`). `di` + opróżnienie L1 przed skokiem wystarcza: oba kernele startują poprawnie, mimo że MBoot ma włączony L2. Na ekranie i na wyświetlaczu panelu nic jeszcze nie oglądane.

Uwaga: pierwszy start po przełożeniu pendrive'a z PC czasem wisi (znany problem z enumeracją USB, błąd -71); po ponownym włączeniu zasilania działa.

### Docelowy autostart (etap 4, po testach)

`bootblk.img` zapisany pod `0x1D0000` (zamiast samego `logofix.img`), potem (linie poniżej 128 znaków):
```
setenv usbload mw.l 0x81000000 0 1\;fatload usb 0 0x81000000 vmlinux.bin\;
setenv bootcmd run wdoff\;usb start\;run usbload\;run flashlogo\;go 0x80F00400\;run flashboot\;
```
`flashlogo` wczytuje cały blok 64 KB, więc `bootsel` jest już w RAM pod `0x80F00400`. `run flashboot` zostaje jako start awaryjny, gdy `bootsel` wróci z błędem.
