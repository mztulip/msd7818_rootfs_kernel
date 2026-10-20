# Własne logo startowe (MBoot)

Logo wyświetlane przez MBoot zaraz po włączeniu zasilania to zwykły plik JPEG zapisany we flashu SPI. Ten opis pokazuje, jak je podmienić (zrobione 2026-10-14: logo WIWA → logo Linux v1) i jak wrócić do oryginału. ✅ sprawdzone na płycie.

## Jak MBoot znajduje logo

Położenie i rozmiar pliku są w zmiennych środowiska MBoota (`printenv`):

```
LOGO_SPI_ADDR=70000      # offset we flashu SPI
LOGO_BIN_SIZE=BFCE       # rozmiar JPEG-a w bajtach (hex); oryginał WIWA = 0xBFCE = 49102 B
logo_cmd=boot_logo 0 0 1 1
```

- Slot na logo to blok **`0x70000`–`0x7FFFF` (64 KB)**. Zaraz za nim, od `0x80000`, jest obraz aplikacji (nagłówek `0xDEADBEEF`), więc **JPEG nie może przekroczyć 0x10000 B**. Reszta bloku po JPEG-u to zera.
- Dekoduje sprzętowy dekoder JPD. Oryginał to **baseline JPEG (nie progresywny), 720×576, YCbCr 4:4:4** (bez podpróbkowania), i nowe logo zrobiłem w tym samym formacie. Inne warianty (4:2:0, inne rozmiary) mogą działać, ale tego nie sprawdzałem ❓.
- MBoot rozciąga 720×576 na cały ekran 1920×1080. Piksel jest więc szerszy niż wyższy (×2,67 w poziomie, ×1,875 w pionie). Obraz trzeba projektować w proporcjach 16:9 (np. 1024×576) i ścisnąć do 720×576. Robią to skrypty `mklogo.py`.
- Później `logofix` (zamiast `osd_create`) konwertuje wyświetlone logo do RGB565 dla Linuksa ([grafika_gop.md](grafika_gop.md#logo-wiwa-przy-starcie-logofix-)). Działa z każdym logo, bo czyta to, co MBoot narysował.

## Pliki

| Plik | Co to |
|---|---|
| `tools/bootlogo/png2logo.py` | **dowolny obraz → JPEG dla MBoota** (16:9 → 720×576, < 64 KB) i wartości do wpisania w MBoocie |
| `tools/bootlogo/v1/mklogo.py` | generator logo v1: Tux + „Linux on MStar MSD7818” (Pillow + `rsvg-convert` dla `tux.png`) |
| `tools/bootlogo/v1/linux_logo.jpg` | **wgrane logo**, 60 718 B (`0xED2E`), MD5 `789afb4e…` |
| `tools/bootlogo/v2_retro/` | wersja pixel-art (zachód słońca, dekoder z „HELO”, Tux, procesor MIPS, terminal z asemblerem); niewgrana |
| `tools/bootlogo/Tux.svg` | Tux (Larry Ewing, wersja SVG z Wikimedia Commons) |
| `tools/bootlogo/wiwa_logo_block_0x70000.bin` | **kopia oryginalnego bloku 0x70000** (64 KB, CRC32 `93708F6A`), wycięta z `binaries/wiwa_hd95_BACKUP.BIN` |
| `tools/serial/` | `sercmd.py` (polecenia przez UART), `catch_mboot.py` (restart i zatrzymanie w MBoocie), `linux_login.py` |

## Przygotowanie obrazu

```sh
cd tools/bootlogo/v1
rsvg-convert -h 360 ../Tux.svg -o tux.png
python3 mklogo.py        # design_16x9.png (podgląd) + linux_logo.jpg (720x576, 4:4:4, baseline)
ls -l linux_logo.jpg     # MUSI być < 65536 B; skrypt dobiera jakość JPEG
```

### Z dowolnego PNG (najprostsza droga) ✅

`tools/bootlogo/png2logo.py` (Python 3 + Pillow) robi wszystko naraz:
- traktuje obraz jako 16:9 i dopasowuje go z marginesami w kolorze `--bg`;
- ściska go do 720×576;
- zapisuje baseline JPEG 4:4:4, obniżając jakość, aż plik zmieści się w 64 KB;
- wypisuje wartości potrzebne w MBoocie.

```sh
python3 tools/bootlogo/png2logo.py moje_logo.png -o linux_logo.jpg --bg 102040
#   linux_logo.jpg: 36644 bytes, quality 92
#   MBoot: setenv LOGO_BIN_SIZE 8F24
#   CRC32 of the JPEG:              56A9312B
#   CRC32 of the 64 KB flash block: 33FB1A41
```

- Najlepiej przygotować PNG w proporcjach **16:9** (np. 1920×1080 albo 1024×576). Wtedy nie ma marginesów i nic nie jest zniekształcone. Obraz w innych proporcjach dostanie pasy w kolorze `--bg`, a przezroczystość też wypełni się kolorem `--bg`.
- `--stretch` rozciąga obraz do 720×576 bez zachowania proporcji: dla obrazu, który już jest „ściśnięty” jak oryginalne logo WIWA.
- Drobne detale i szum zwiększają rozmiar JPEG-a. Gdy jakość spada poniżej ok. 70, lepiej uprościć obraz.
- Zanotuj `LOGO_BIN_SIZE`. CRC służy do sprawdzenia pliku na PC albo w Linuksie (`crc32 linux_logo.jpg` w BusyBoxie). **MBoot tej płyty nie ma polecenia `crc32`** (jest na liście `help`, ale zwraca `Unknown command`, sprawdzone 2026-10-19), więc w MBoocie dane sprawdzamy porównaniem dwóch kopii w RAM (`cmp`).

### Podgląd na ekranie bez zapisu do flasha

Z Linuksa można wrzucić do bufora ramki obraz przeskalowany tak jak przez MBoot (1920×1080, RGB565). Wtedy widać, jak logo będzie wyglądać na telewizorze:
```sh
# na PC
python3 - <<'EOF'
from PIL import Image; import numpy as np
a=np.asarray(Image.open('linux_logo.jpg').convert('RGB').resize((1920,1080),Image.BILINEAR)).astype(np.uint16)
open('preview.raw','wb').write((((a[...,0]>>3)<<11)|((a[...,1]>>2)<<5)|(a[...,2]>>3)).astype('<u2').tobytes())
EOF
gzip -9 preview.raw
python3 tools/uart_upload.py preview.raw.gz /tmp/preview.raw.gz      # ok. 30 s
# na płycie
echo 0 > /sys/class/vtconsole/vtcon1/bind        # konsola nie nadpisuje bufora
gunzip -c /tmp/preview.raw.gz | dd of=/dev/mem bs=4096 seek=$((0x2100000/4096))
echo 1 > /sys/class/vtconsole/vtcon1/bind        # powrót konsoli
```

## Wgranie do flasha krok po kroku

1. **Plik na pendrive:** skopiuj `linux_logo.jpg` na partycję FAT. Z działającego Linuksa: `uart_upload.py`, potem `mount /dev/sda1 /mnt/fat; cp …; umount`.
2. **Konsola MBoota:** zrestartuj płytę i wciśnij Enter (albo `python3 tools/serial/catch_mboot.py reboot`). Wyłącz watchdog:
   ```
   mw.w 0xBF006040 0 1; mw.w 0xBF006010 0 1; mw.w 0xBF006014 0 1
   ```
3. **Kopia obecnego logo na pendrive.** Zanotuj też stary rozmiar:
   ```
   printenv LOGO_BIN_SIZE                         # na WIWA HD95: BFCE
   usb start
   spi_rdc 0x80B00000 0x70000 0x10000
   fatwrite usb 0 0x80B00000 logo_backup.bin 10000
   fatload usb 0 0x80B10000 logo_backup.bin       # odczyt kopii z pendrive'a
   cmp.l 0x80B00000 0x80B10000 4000               # "Total of 16384 words were the same"
   ```
   Warto też raz zrobić kopię całego flasha (4 MB): `spi_rdc 0x80000000 0 400000`, potem `fatwrite usb 0 0x80000000 flash_backup.bin 400000`.
4. **Złóż nowy blok 64 KB w RAM:** zera i JPEG na początku:
   ```
   mw.l 0x80C00000 0 4000             # 0x4000 słów = 64 KB zer
   usb start
   fatload usb 0 0x80C00000 linux_logo.jpg     # "60718 bytes read": liczba bajtów = rozmiar pliku
   ```
5. **Zapis do flasha:** `spi_wrc` sam kasuje blok, zapisuje i weryfikuje:
   ```
   spi_wrc 0x80C00000 0x70000 0x10000    # Erasing... Writing... Verifying...OK !!!
   spi_rdc 0x80D00000 0x70000 0x10000
   cmp.l 0x80C00000 0x80D00000 4000      # flash = RAM: "Total of 16384 words were the same"
   ```
6. **Nowy rozmiar w środowisku:**
   ```
   setenv LOGO_BIN_SIZE ED2E             # wartość z png2logo.py (v1: 60718 = 0xED2E)
   saveenv
   ```
7. **Test:** `reset`. Nowe logo powinno się pojawić od razu po włączeniu, a Linux startuje jak wcześniej (`logofix` zwraca `rc = 0x0`).

Uwagi:
- ⚠️ **`fatwrite` w tym MBoocie jest zawodny** (2026-10-19): zapis `logobak.bin` (64 KB) skończył się `Invalid FAT entry`, odczytem 2048 B i niezgodnym `cmp`, a na pendrivie nadpisał łańcuch klastrów innego pliku (`vmlinux_rgb.bin`); trzeba było `fsck.vfat`. Kopię bloku lepiej robić inaczej (pełna kopia flasha jest w `binaries/wiwa_hd95_BACKUP.BIN`; z Linuksa: `memdump 0x1FC70000 0x10000 > /mnt/fat/logo.bin`, `tools/board/memdump.c`). Jeśli już `fatwrite`, to krótka nazwa 8.3 i zawsze sprawdzenie `cmp`, a potem `fsck` na PC.
- CRC32 oryginalnego bloku WIWA: `93708F6A`, bloku v1: `02AB2C05` (do sprawdzenia pliku na PC/w Linuksie).
- `cmp` jest na liście `help` tak samo jak `crc32`; jeśli też okaże się niedostępne, porównanie można zrobić w Linuksie (`crc32` w BusyBoxie na pliku z pendrive'a) albo `md.l` na kilku adresach ❓.
- `spi_wrc` wymaga adresów i długości wyrównanych do bloku kasowania (64 KB). Zapisujemy zawsze cały blok `0x70000`, nigdy więcej: następny blok (`0x80000`) to oryginalne oprogramowanie.
- Gdyby nowe logo się nie dekodowało, MBoot nadal dochodzi do konsoli (Enter przy starcie), więc zawsze można wrócić do oryginału.

## Powrót do logo WIWA

Skopiuj na FAT `logo_backup.bin` z kroku 3 albo `tools/bootlogo/wiwa_logo_block_0x70000.bin` (kopia tylko lokalnie, nie ma jej w publicznym repo), a potem w MBoocie:
```
usb start
fatload usb 0 0x80C00000 logo_backup.bin   # "65536 bytes read"
spi_wrc 0x80C00000 0x70000 0x10000
spi_rdc 0x80D00000 0x70000 0x10000
cmp.l 0x80C00000 0x80D00000 4000
setenv LOGO_BIN_SIZE BFCE
saveenv
```

## Stan płyty

- 2026-10-14: wgrane logo v1 (`linux_logo.jpg`, `LOGO_BIN_SIZE=ED2E`, CRC bloku `02AB2C05`, odczyt po zapisie zgodny). Po `reset` logo wyświetla się poprawnie (potwierdzone na ekranie), autostart (`logofix` + kernel #8) działa, a GOP3 w Linuksie = `0x3F11`.
