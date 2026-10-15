# Przesyłanie plików na płytę przez UART

Sposób na wgranie pliku (np. nowego kernela albo programu) na działającego Linuksa na płycie, **bez sieci i bez wyjmowania pendrive'a**, tylko przez konsolę szeregową. Na płycie wystarczy BusyBox (`base64`, `md5sum`, `gunzip`).

Narzędzie: `tools/uart_upload.py` (Python 3 + `pyserial`).

## Idea

Konsola szeregowa przenosi tekst, więc plik binarny trzeba zamienić na tekst. Robi to **base64**: każde 3 bajty → 4 znaki ASCII, linie po 76 znaków. Na płycie program `base64 -d` czyta ten tekst z wejścia, czyli z konsoli, i zapisuje odkodowane bajty do pliku.

```
PC:    plik ──gzip──▶ plik.gz ──base64──▶ tekst ──UART 115200──▶
płyta: konsola ──▶ base64 -d ──▶ /tmp/plik.gz ──gunzip──▶ plik
```

## Krok po kroku (co robi skrypt)

1. **Kompresja** (ręcznie, przed wysłaniem): `gzip -9 -k vmlinux.bin`. Kernel 8,6 MB → 3,8 MB, czyli ponad 2× krócej.
2. **Przygotowanie odbiornika** (skrypt wpisuje to w konsolę płyty):
   ```sh
   stty -echo; base64 -d > /tmp/plik.gz
   ```
   - `stty -echo` wyłącza echo terminala. Inaczej płyta odsyłałaby każdy odebrany znak, co podwaja ruch i zaśmieca odczyt;
   - `base64 -d > plik` czeka na tekst na standardowym wejściu, czyli z konsoli UART.
3. **Wysyłanie:** treść base64 linia po linii (dla kernela ok. 66 tys. linii). Linie w trybie kanonicznym terminala są krótkie (76 znaków), więc mieszczą się w buforze linii tty (4096 B).
4. **Koniec danych:** `Ctrl-D` (bajt `0x04`) na początku linii oznacza koniec pliku, więc `base64 -d` kończy pracę.
5. **Weryfikacja:**
   ```sh
   stty echo; md5sum /tmp/plik.gz
   ```
   Skrypt porównuje sumę z płyty z lokalną i zgłasza `OK` albo `MD5 MISMATCH`.
6. **Rozpakowanie i zapis** (ręcznie albo kolejnym poleceniem):
   ```sh
   mount /dev/sda1 /mnt/fat
   gunzip -c /tmp/plik.gz > /mnt/fat/plik
   sync; md5sum /mnt/fat/plik; umount /mnt/fat
   ```

## Użycie

```sh
gzip -9 -k vmlinux_dev.bin
python3 tools/uart_upload.py vmlinux_dev.bin.gz /tmp/vmlinux_dev.bin.gz
python3 tools/uart_upload.py tools/board/frontpanel.c /root/frontpanel.c   # małe pliki bez gzip
```
Opcje: `-p /dev/ttyUSB0`, `-b 115200`. Na płycie musi być zalogowana powłoka (znak `#`), a port nie może być otwarty w innym programie (np. `picocom`).

## Wydajność (zmierzone)

| Plik | Rozmiar | Po gzip | base64 | Czas |
|---|---|---|---|---|
| kernel `vmlinux_*.bin` | 8,6 MB | 3,8 MB | 5,1 MB (66,7 tys. linii) | **455 s** (ok. 7,5 min) |
| źródło `.c` | ok. 10 KB | — | ok. 13 KB | ok. 7 s |

Przepustowość: 115200 bodów przy 10 bitach na bajt (8N1) to ok. 11,5 KB/s surowych danych. Po narzucie base64 (×4/3) daje to ok. 8,4 KB/s danych pliku, a z gzip efektywnie ok. 19 KB/s dla kernela.

## Uwagi

- Nie ma sterowania przepływem. Działa, bo `base64 -d` na płycie nadąża za 115200 bodów. Przy wyższej prędkości mogłoby gubić znaki, a wtedy MD5 by się nie zgodziło.
- W razie przerwania (np. utrata połączenia) płyta zostaje w `base64 -d` z wyłączonym echem. Wystarczy wysłać `Ctrl-D`, a potem `stty echo`.
- Szybsza alternatywa to sieć: płyta ma `dropbear` (SSH) i `scp`, gdy podłączy się adapter USB-Ethernet i skonfiguruje adres.
- Pliki zawsze zapisywałem pod **nową nazwą** (np. `vmlinux_dev.bin`), żeby nie nadpisać kernela startującego automatycznie.
