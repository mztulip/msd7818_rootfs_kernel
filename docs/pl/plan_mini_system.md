# Mini system Linux we flashu SPI (z fbDOOM)

## O co chodzi

Dekoder ma flash SPI 4 MB. Dziś jest w nim MBoot (bootloader) i oryginalne oprogramowanie (eCos), a Linux startuje z pendrive'a. Cel: zmieścić **cały mały Linux we flashu**, zamiast oryginału, tak żeby dekoder bez pendrive'a startował prosto do Linuksa i dało się w nim zagrać w Dooma z dźwiękiem (wystarczy podłączyć klawiaturę USB).

Założenia (ustalone 2026-10-14, uzupełnione 2026-10-19):
- **System plików tylko do odczytu** (squashfs we flashu). Zmiany w plikach trwają do restartu, po restarcie zawsze jest czysty system.
- **Pendrive jako dodatek**: jeśli jest włożony, montuje się sam pod `/mnt/usb`, a z niego można uruchamiać własne programy.
- Później, opcjonalnie: mała zapisywalna partycja `/home` (JFFS2).
- Oryginalny firmware można skasować, bo pełna kopia flasha jest w `binaries/wiwa_hd95_BACKUP.BIN` (i dzisiejsza, z nowym logo, w `flash_20261019.bin` na pendrivie).

## Stan (2026-10-19)

| Etap | Stan |
|---|---|
| 1. Mały kernel (`kernel_mini`) | ✅ działa, 1,2 MB po kompresji |
| 2. Odczyt flasha w Linuksie | ✅ cały flash widoczny jako MTD |
| 3. Sterownik zapisu flasha (ISP) | ✅ działa, opis: [flash_spi.md](flash_spi.md) |
| 4. Rootfs squashfs (BusyBox + fbDOOM + WAD) | ✅ 1,97 MB, przetestowany z pendrive'a: Doom z obrazem i dźwiękiem na HDMI i SCART |
| 5. Zapis kernela i rootfs do flasha, start bez pendrive'a | ✅ **zrobione 2026-10-19**: płytka startuje z flasha, ok. 10 s od resetu do loginu |
| 6. Zapisywalne `/home` (JFFS2) | — później |

## Jak będzie startował

```
włączenie zasilania
  └─ sboot + MBoot (we flashu, bez zmian)
       ├─ wyświetla logo z flasha (0x70000)
       ├─ wyłącza watchdoga (mw.w … jak dziś)
       ├─ spi_rdc  → logofix z flasha (0x1D0000) do RAM, go 0x80F00000   (logo YUV → RGB565)
       ├─ spi_rdc  → kernel LZMA z flasha (0x80000) do RAM
       ├─ LzmaDec  → rozpakowanie kernela pod 0x80000000
       └─ go 0x80000000
            └─ Linux: sterownik ISP + spi-nor widzi flash jako partycje MTD
                 └─ root = /dev/mtdblock4 (squashfs, tylko odczyt) → BusyBox init → root zalogowany sam na UART i na ekranie
```

To te same polecenia, którymi MBoot uruchamia dziś oryginał (`spi_rdc` + `LzmaDec` + `go`), więc w MBoocie nic nie trzeba zmieniać poza zmienną `bootcmd`.

## Układ flasha

| Zakres | Rozmiar | Partycja w Linuksie | Zawartość |
|---|---|---|---|
| `0x000000–0x070000` | 448 KB | `mtd0 boot` | sboot + MBoot (bez zmian) |
| `0x070000–0x080000` | 64 KB | `mtd1 logo` | logo startowe (bez zmian) |
| `0x080000–0x1D0000` | 1,31 MB | `mtd2 kernel` | kernel LZMA (ok. 1,21 MB) |
| `0x1D0000–0x1E0000` | 64 KB | `mtd3 logofix` | `logofix.bin` (1 KB) |
| `0x1E0000–0x3E0000` | 2 MB | `mtd4 rootfs` | squashfs (1,97 MB) |
| `0x3E0000–0x400000` | 128 KB | `mtd5 env` | środowisko MBoota (bez zmian) |

W Linuksie wszystkie partycje są tylko do odczytu. Kernel i rootfs wgrywa MBoot (`spi_wrc`). Zapas: kernel ok. 100 KB, rootfs 32 KB.

## Kernel `kernel_mini`

Gałąź `kernel_mini` w repo `linux_msd781x`, w osobnym katalogu roboczym `linux_mini/` (git worktree), więc pełny kernel #13 w `linux_msd781x/` zostaje nietknięty. Konfiguracja: `arch/mips/configs/wiwa_hd95_mini_defconfig`.

**Co ma:** konsola szeregowa, framebuffer `mstar-gopfb` z konsolą na ekranie (font TER16x32), USB (pendrive i klawiatura), I2C (tuner), panel przedni, `/dev/mem` (dźwięk przez DSP), vfat, ext2, squashfs, MTD + sterownik flasha `spi-mstar-isp` (+ `spi-nor`).

**Czego nie ma:** modułów, sieci, ALSA, DRM, V4L, RTC, watchdoga, opcji debugowania, swapu, cgroups i namespace'ów. Kompilowany z `-Os`.

**Rozmiar:** 1,21 MB po LZMA (pełny kernel #13: 2,87 MB). To prawie sam kod (`.text` 2,7 MB, po LZMA 985 KB). Duże bufory i pusty 1 MB na DTB to zera i po kompresji nic nie ważą. Rdzeń, bez którego Linux nie działa (planista, pamięć, VFS, `lib`, `arch`), to ok. 1,6 MB surowego kodu; minimalny kernel 6.7 na MIPS ma po LZMA ok. 500–600 KB, reszta to nasze sterowniki (USB z warstwą SCSI, tty/fbcon, HID, framebuffer, FAT).

Dalsze możliwe cięcia (odłożone, bo budżet się mieści): `LD_DEAD_CODE_DATA_ELIMINATION` (−50…100 KB), opcje `EXPERT` jak `BASE_FULL`, `MULTIUSER`, `EPOLL` (−40…70 KB), ext2 (−16 KB), `SMP` (−40 KB, ale bez drugiego rdzenia), `PRINTK` (−100 KB, ale bez komunikatów kernela). Realne minimum: ok. 850–950 KB.

**Budowanie** (w `linux_mini/`, GCC 12.2 jak dla pełnego kernela):
```sh
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- -j6
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- dtbs
cp vmlinux vmlinux_dtb
mipsel-linux-gnu-objcopy --update-section .appended_dtb=arch/mips/boot/dts/mstar/msd7816-test.dtb vmlinux_dtb
mipsel-linux-gnu-objcopy -O binary vmlinux_dtb vmlinux_mini.bin
```
Start zawsze przez `go 0x80000000` (opcja `CONFIG_BOOT_RAW` stawia na początku obrazu skok do właściwego wejścia).

Linia poleceń kernela jest w DTS (`bootargs`). Wersja do flasha ma `root=/dev/mtdblock4 rootfstype=squashfs ro`. Do testów z pendrive'a robi się kopię z innym DTB (niżej).

## Rootfs (`rootfs_mini/`)

Osobna konfiguracja Buildroota, budowana do `rootfs_mini/output` (główny Buildroot i jego rootfs na pendrivie zostają bez zmian). Instrukcja budowania: `rootfs_mini/README.md`.

**Zawartość:**
- BusyBox, statycznie z musl (1,2 MB przed kompresją);
- `/home/fbdoom`: fbDOOM statycznie z musl, z dźwiękiem przez **nasz własny program DSP** (`OWN_SOUND=1`, `own_audio.h`, bez kodu MStara; 494 KB; od 2026-10-20, wcześniej 581 KB z kodem DSP MStara), dźwięk tylko na HDMI;
- `/home/freedoom_e1m1.wad`: okrojony Freedoom (jedna mapa E1M1, BSD-3, `tools/doom/mkminiwad.py`) z `FREEDOOM_COPYING.txt` i plikami `FREEDOOM_CREDITS*.txt` obok (wymóg licencji); do 2026-10-20 był tu `doom1.wad`; polecenie `doom` uruchamia grę;
- **root bez hasła, logowany automatycznie** na konsoli szeregowej i na ekranie (`tty1`), od 2026-10-20 (niżej: „Logowanie”);
- `/tmp` i `/run` w RAM (tmpfs), reszta tylko do odczytu;
- `S20usb`: montuje pierwszą partycję FAT pendrive'a pod `/mnt/usb`, jeśli jest.

**Rozmiar obrazu:** 2 011 136 B (od 2026-10-20; wcześniej 2 064 384 B; squashfs, XZ, blok 1 MB, partycja ma 2 097 152 B). Największy jest WAD (`freedoom_e1m1.wad` 4,1 MB, po kompresji ok. 1,32 MB; dawny `doom1.wad` 4,2 MB / 1,37 MB). Blok 1 MB zamiast domyślnych 128 KB oszczędza ok. 50 KB. `post_build.sh` usuwa `lib/libc.a` (3,2 MB, Buildroot kopiuje go przy statycznym musl), skrypty sieci, crona i `seedrng`.

Od 2026-10-20 **obraz nie zawiera kodu MStara**: fbDOOM gra przez nasz program DSP, a WAD to Freedoom (BSD-3, notka licencji w `/home`). Dawny obraz (z kodem DSP MStara i `doom1.wad`) i jego pliki leżą w `rootfs_mini/old_overlay_mstar/`; tych nie wolno publikować.

**Logowanie (od 2026-10-20).** `BR2_TARGET_GENERIC_ROOT_PASSWD=""` w `rootfs_mini/wiwa_mini_defconfig` daje w `/etc/shadow` `root::` (brak hasła). W `overlay/etc/inittab` getty na `console` i `tty1` ma `-n -l /usr/bin/autologin` (bez pytania o nazwę, zamiast `/bin/login` uruchamia skrypt), a `overlay/usr/bin/autologin` robi `exec /bin/login -f root` (zalogowany bez pytania). Po starcie od razu jest powłoka `#`, w logu `login[..]: root login on 'console'` i `'tty1'`. Po wylogowaniu (`exit`) init uruchamia getty od nowa i root loguje się znowu. `tools/serial/linux_login.py` rozpoznaje autologowanie (komunikat `root login on` i znak `#`); w systemie z pendrive'a dalej loguje się normalnie (login i hasło).

## Test bez zapisu do flasha (zrobione 2026-10-19)

Rootfs squashfs można sprawdzić z pendrive'a, zanim trafi do flasha:
1. Obraz na trzecią partycję pendrive'a (`sda3`, wcześniej nieużywany swap): `dd if=rootfs.squashfs of=/dev/sda3 bs=64k`.
2. Kernel z DTB, w którym `bootargs` ma `root=/dev/sda3 rootfstype=squashfs ro` (`linux_mini/vmlinux_sqtest.bin`, robiony przez `dtc` z gotowego DTB i podmianę `.appended_dtb`, bez przebudowy kernela).
3. Z MBoota: `logofix` jak zwykle, `fatload usb 0 0x80000000 vmlinux_sqtest.bin`, `go 0x80000000`.

Wynik: start do logowania w 2,6 s, `Mounted root (squashfs filesystem) readonly`, pendrive zamontowany pod `/mnt/usb`, 34 MB wolnej RAM, `doom` z obrazem i dźwiękiem na HDMI i SCART.

## Zapis do flasha (zrobione 2026-10-19) ✅

**Wykonane 2026-10-19:** trzy obrazy zapisane `spi_wrc` z pendrive'a, każdy z `Verifying...OK` i zgodnym `cmp.l` (rootfs 524 288, logofix 16 384, kernel 311 296 słów). Ręczny start (`run bootcmd`) i autostart po `reset` działają: logo, `logofix` (`rc = 0x0`), `CRC check success`, kernel, `Mounted root (squashfs filesystem) readonly on device 31:4`, `wiwa login:` po ok. 10 s; `doom` z obrazem i dźwiękiem z rootfs we flashu. Oryginalny firmware (`0x80000–0x3E0000`) jest skasowany.

**Autostart (zapisany `saveenv`).** Jedna linia `setenv bootcmd …` ma ok. 230 znaków, a MBoot ucina linię polecenia po ok. 128 znakach (cicho, bez błędu), więc autostart jest podzielony na zmienne:
```
wdoff     = mw.w 0xBF006040 0 1;mw.w 0xBF006010 0 1;mw.w 0xBF006014 0 1;
flashlogo = spi_rdc 0x80F00000 0x1D0000 0x10000;go 0x80F00000;
flashboot = spi_rdc 0x80B00000 0x80000 0x130000;LzmaDec 0x80B00000 0x1285DA 0x80000000 0x81000000;go 0x80000000;
bootcmd   = run wdoff;usb start;run flashlogo;run flashboot;
bootlinux = (bez zmian) start systemu z pendrive'a: Enter przy starcie, potem  run wdoff; run bootlinux
```
Przy nowym kernelu zmienia się tylko `flashboot` (rozmiar w `LzmaDec`). `usb start` w `bootcmd` jest konieczne: kernel sam nie włącza PHY USB (węzeł PHY w DTS jest wyłączony) i bez inicjalizacji przez MBoot port jest martwy (w `dmesg` nic przy wkładaniu pendrive'a); dodane 2026-10-19. Bez pendrive'a `usb start` tylko zgłasza błąd i start idzie dalej. Rootfs od 2026-10-19 wieczorem: napis `MSD7818 mini Linux`, host `msd7818`, `S20usb` montuje pendrive w tle (do 15 s).

**Aktualizacja rootfs 2026-10-20** (nasz dźwięk, Freedoom, root bez hasła z autologowaniem): nowy obraz najpierw sprawdzony z pendrive'a (`dd` na `sda3`, start `vmlinux_sqtest.bin`: autologowanie, gra bez błędów, ton testowy `owntest` na HDMI), potem w MBoocie `fatload usb 0 0x80C00000 rootfs.img`, `spi_wrc 0x80C00000 0x1E0000 0x200000` (`Verifying...OK`), `spi_rdc 0x81000000 0x1E0000 0x200000`, `cmp.l 0x80C00000 0x81000000 80000` (524 288 słów zgodnych), `reset`: `CRC check success`, rootfs z `mtdblock4`, root zalogowany sam. Kernel, logofix i środowisko bez zmian. Pierwsza wersja WAD-u nie miała płaszczyzny `FLOOR7_2` (tło ramki widoku) i gra kończyła się błędem; poprawione w `mkminiwad.py` przed zapisem. Uwaga z tej sesji: pendrive przełożony z PC do płytki czasem nie wstaje przy pierwszym starcie (`error -71` w `dmesg`, potem urządzenie `FC 3281B` bez nośnika); pomaga kolejny restart płytki z pendrive'em w gnieździe.

Poniżej opis przygotowania (stan sprzed zapisu).

Stan sprzed zapisu: obrazy były gotowe i kernel z LZMA sprawdzony w MBoocie z RAM (bez zapisu). Sam zapis **skasował oryginalny firmware** (`0x80000–0x3E0000`). MBoot, logo i środowisko zostają nietknięte. Kopie flasha: `binaries/wiwa_hd95_BACKUP.BIN` (oryginał) i `flash_20261019.bin` na pendrivie (stan z nowym logo, zrobiona `memdump` przed testami zapisu).

### Format obrazu kernela dla `LzmaDec` (zweryfikowane na płytce) ✅

MBoot `LzmaDec IN ROZMIAR OUT BUFOR` (argumenty z pomocy: `u32LzmaBufAddr`, `u32AppFileLen`, `u32AppDramAddr`, `u32LzmaDecBufAddr`) robi to (kod w `RAM87_DU.RAM` od `0x87613308`):
1. czyta nagłówek „LZMA alone” (13 B: właściwości, słownik, rozmiar po rozpakowaniu) i rozpakowuje pod `OUT`;
2. **ostatnie 4 bajty rozpakowanych danych to oczekiwana suma** (little-endian);
3. liczy **zwykłe CRC32** (jak `zlib.crc32`, funkcja `0x876364F8`) z rozpakowanych danych bez tych 4 bajtów i porównuje.

Przy zgodności wypisuje `CRC check success !!`. Przy niezgodności wypisuje `Please reset and upgrading!`, **zapisuje środowisko MBoota** i resetuje płytkę. Przy pierwszej próbie (obraz bez sumy) to nadpisało `LOGO_BIN_SIZE` starą wartością `BFCE` i logo wyświetlało się ze śmieciami (naprawione `setenv LOGO_BIN_SIZE FA26; saveenv`). Oryginał ma tę samą budowę: CRC32 `D814DE68` na końcu rozpakowanych 7 324 916 B.

Budowa `kernel.lzma` (w `linux_mini/`, z `vmlinux_flash.bin` z docelowym DTB):
```python
import lzma, struct, zlib
raw = open('vmlinux_flash.bin', 'rb').read()
payload = raw + struct.pack('<I', zlib.crc32(raw))           # CRC32 na końcu
c = bytearray(lzma.compress(payload, format=lzma.FORMAT_ALONE,
    filters=[{"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME, "dict_size": 8 << 20}]))
c[5:13] = struct.pack('<Q', len(payload))                     # znany rozmiar, jak w oryginale
open('kernel.lzma', 'wb').write(c)
```
Test z RAM: `fatload usb 0 0x80B00000 kernel.lzma`, `LzmaDec 0x80B00000 0x1285DA 0x80000000 0x81000000` → `CRC check success`, `go 0x80000000` → kernel wykrywa flash i 6 partycji i kończy na `Unable to mount root fs on /dev/mtdblock4` (bo rootfs jeszcze nie jest we flashu). Watchdog trzeba przedtem wyłączyć (`mw.w` jak w `bootcmd`).

### Pliki do zapisu (`flash_images/`, dopełnione `0xFF` do bloków 64 KB)

| Plik | Rozmiar | Offset we flashu | Zawartość |
|---|---|---|---|
| `kernel.img` | `0x130000` | `0x80000` | `kernel.lzma` 1 213 914 B (`0x1285DA`), kernel #4 z `root=/dev/mtdblock4` |
| `logofix.img` | `0x10000` | `0x1D0000` | `logofix.bin` 1008 B |
| `rootfs.img` | `0x200000` | `0x1E0000` | `rootfs.squashfs` 2 064 384 B (wersja z 2026-10-19: **kod MStara, nie publikować**; od 2026-10-20 2 011 136 B bez kodu MStara, `flash_images/rootfs_20261020.img`) |

### Kroki

1. Z Linuksa wgrać trzy pliki na FAT pendrive'a (`uart_upload.py` albo pendrive w PC).
2. W MBoocie dla każdego pliku (przykład dla rootfs):
   ```
   usb start
   fatload usb 0 0x80C00000 rootfs.img
   spi_wrc 0x80C00000 0x1E0000 0x200000       # Erasing... Writing... Verifying...OK
   spi_rdc 0x81000000 0x1E0000 0x200000
   cmp.l 0x80C00000 0x81000000 80000            # 0x80000 słów = 2 MB
   ```
   tak samo `kernel.img` → `0x80000`, `0x130000` (`cmp.l … 4C000`) i `logofix.img` → `0x1D0000`, `0x10000` (`cmp.l … 4000`).
3. Nowy autostart (stary start z pendrive'a zostaje jako `run bootlinux`):
   ```
   setenv bootcmd mw.w 0xBF006040 0 1\;mw.w 0xBF006010 0 1\;mw.w 0xBF006014 0 1\;spi_rdc 0x80F00000 0x1D0000 0x10000\;go 0x80F00000\;spi_rdc 0x80B00000 0x80000 0x130000\;LzmaDec 0x80B00000 0x1285DA 0x80000000 0x81000000\;go 0x80000000\;
   saveenv
   ```
   Rozmiar w `LzmaDec` (`0x1285DA`) trzeba zmienić przy każdym nowym kernelu.
4. Restart bez pendrive'a: logo, potem Linux z rootfs we flashu.

Gdyby coś poszło nie tak: Enter przy starcie daje konsolę MBoota (MBoot nie jest zapisywany), `run bootlinux` startuje system z pendrive'a. Powrót do oryginału: z kopii flasha zapisać `0x80000–0x3E0000` przez `spi_wrc` i przywrócić oryginalny `bootcmd`.

## Historia decyzji

- 2026-10-14: plan z initramfs (cały system w RAM, jeden obraz LZMA).
- 2026-10-19: zamiast initramfs **squashfs czytany prosto z flasha** (nie zajmuje RAM, kernel i rootfs osobno). Okazało się, że cały flash jest widoczny w pamięci pod `0x1FC00000`, więc odczyt działał od razu (`mtd-rom`), a potem powstał pełny sterownik ISP z zapisem.
