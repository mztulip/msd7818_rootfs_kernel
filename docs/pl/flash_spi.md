# Flash SPI z Linuksa: odczyt i zapis

## W skrócie

Dekoder ma flash SPI 4 MB (kość Spansion S25FL032P). Od 2026-10-19 Linux (kernel `kernel_mini`) umie go **czytać i zapisywać**:
- **odczyt** idzie przez okno pamięci: cały flash jest widoczny pod adresem fizycznym `0x1FC00000`, ok. 4 MB/s;
- **zapis i kasowanie** idą przez kontroler ISP układu, do którego napisaliśmy sterownik `drivers/spi/spi-mstar-isp.c`; nad nim działa standardowy sterownik `spi-nor`, który robi z flasha partycje MTD (`/dev/mtdN`, `/dev/mtdblockN`).

Wszystko sprawdzone na płytce. Rejestry i kolejność operacji pochodzą z deasemblacji biblioteki MStara dla tego układu (`halSERFLASH.o` z `mstar-mboot-master/MstarCore/bsp/k1/lib/libdrvSERFLASH.a`, ma symbole).

## Jak tego używać

Po starcie kernela:
```
mstar-isp 1f001000.isp: ISP flash controller, window on, writes disabled
spi-nor spi0.0: s25sl032p (4096 Kbytes)
Creating 6 MTD partitions on "spi0.0"
```
`cat /proc/mtd` pokazuje partycje z DTS (`msd7816-test.dts`, węzeł `&isp` / `flash@0`). Partycje oznaczone w DTS `read-only` są tylko do odczytu na poziomie MTD.

**Zapis jest domyślnie zablokowany w samym sterowniku**, niezależnie od DTS. Każde polecenie zmieniające flash (WREN, zapis strony, kasowanie, zapis rejestru statusu) jest odrzucane i w logu pojawia się `refusing opcode 0x06, writes are disabled`. Włączenie (do restartu):
```sh
echo 1 > /sys/module/spi_mstar_isp/parameters/allow_write
```
albo na stałe `spi_mstar_isp.allow_write=1` w linii poleceń kernela.

Odczyt bez sterownika ISP, np. pełna kopia flasha z dowolnego kernela z `/dev/mem`:
```sh
memdump 0x1FC00000 0x400000 > flash.bin      # tools/board/memdump.c
```
(`dd if=/dev/mem` tu nie działa, bo `read()` z `/dev/mem` kończy się na końcu RAM; `memdump` używa `mmap`.)

## Ochrona przed zapisem: WP# i rejestr statusu

To trzeba wiedzieć, zanim cokolwiek się zapisze. Na tej płytce flash był **zabezpieczony** i pierwszy test zapisu po cichu nic nie zmienił (kasowanie „trwało” 0 s).

- Rejestr statusu kości miał wartość `0x9E`: bity `BP2..BP0` = 111 oznaczają, że cały flash jest chroniony, więc kość ignoruje kasowanie i zapis bez żadnego błędu.
- Zmienić rejestru statusu też się nie dało, bo bit `SRWD` = 1 i jednocześnie pin **WP#** kości był w stanie niskim.
- **WP# jest podłączony do PM GPIO7** (rejestr `0x0E3A` bit 2 = wyjście, `0x0E3B` bit 2 = odczyt, `0x0E3C` bit 2 = kierunek; CPU `0xBF001C74`). Znalezione przez porównanie rejestrów GPIO w MBoocie przed i po poleceniu `spi_wp 0`.

**Zdjęcie ochrony:** w MBoocie `spi_wp 0` (odpowiada `SPI write protect pass`). Podnosi WP# i ustawia status na `0x80` (bity BP wyzerowane). Rejestr statusu jest nieulotny, więc flash zostaje odblokowany także po restarcie i Linux może kasować i zapisywać nawet przy niskim WP# (WP# blokuje tylko zmianę samego rejestru statusu). `spi_wp 1` przywraca ochronę. Stan sprawdza w MBoocie `spi_rs` (`u8StatusReg`), a w Linuksie `isptool 05 r1`.

Sterownik sam ochrony nie zdejmuje ❓: jeśli MBoot albo oryginalny firmware ustawi ją z powrotem, trzeba znowu `spi_wp 0` (albo w Linuksie podnieść GPIO7 i zapisać status `0x80`).

## Wyniki testów (2026-10-19, kernel mini #3)

Test zapisu na nieużywanym bloku `0x310000` (64 KB, partycja testowa), program `tools/board/mtdtest.c`:

| Operacja | Czas | Wynik |
|---|---|---|
| kasowanie 64 KB | 0,22 s | same `0xFF` |
| zapis 64 KB (tryb bajtowy) | 0,27 s (ok. 240 KB/s) | zgodny z wzorem, dwa różne wzory |
| odczyt przez okno | 2,6 MB w 0,65 s (ok. 4 MB/s) | MD5 zgodne z kopią flasha |

Pozostałe partycje po testach są identyczne z kopią zrobioną przed testami (`flash_20261019.bin` na pendrivie, `memdump`). Przy `allow_write=0` kasowanie zwraca `Read-only file system`.

Narzędzia (`tools/board/`, kompilowane na płytce):
- `memdump ADRES DŁUGOŚĆ`: zrzut pamięci fizycznej przez `mmap` (np. całego flasha);
- `mtdtest /dev/mtdN [ziarno]`: kasuje całą partycję, zapisuje wzór i porównuje (**niszczy zawartość partycji**);
- `isptool BAJTY [rN]`: wysyła dowolne bajty SPI przez kontroler ISP z `/dev/mem`, np. `isptool 9f r3` (ID kości), `isptool 05 r1` (rejestr statusu). Nie używać, gdy sterownik w kernelu akurat pracuje.

## Jak działa sprzęt (dla programisty)

### Dwie drogi do flasha

| Droga | Adres | Co umie |
|---|---|---|
| okno pamięci (XIU) | fizycznie `0x1FC00000`–`0x1FFFFFFF` (CPU `0xBFC00000`) | tylko odczyt, cały flash |
| kontroler ISP | `0x1F001000` (CPU `0xBF001000`, PM RIU + `0x1000`) | dowolne polecenia SPI: ID, status, kasowanie, zapis |

Kiedy ISP jest odblokowany, okno pamięci nie działa. Dlatego sterownik przy każdej operacji odblokowuje ISP, wykonuje ją i blokuje z powrotem, a duże odczyty robi przez okno (przy zablokowanym ISP).

### Rejestry ISP

Rejestry 16-bit, co 4 bajty po stronie CPU. Ten sam układ ma kontroler ISP w MSC313 (`drivers/spi/spi-msc313-isp.c`), to ten sam blok MStara.

| Offset | Nazwa | Znaczenie |
|---|---|---|
| `0x00` | PASSWORD | `0xAAAA` = odblokuj, `0x5555` = zablokuj |
| `0x04` | SPI_COMMAND | tryb komend HAL: kod polecenia (`0x06` WREN, `0x02` PP, `0x05` RDSR) |
| `0x08` / `0x0C` | ADDR_L / ADDR_H | tryb komend: adres, bity 15:0 / 23:16 |
| `0x10` | WDATA | bajt do wysłania (pierwszy bajt opuszcza CS) |
| `0x14` | RDATA | odebrany bajt |
| `0x18` | CLKDIV | dzielnik zegara SPI (sterownik zostawia wartość z MBoota, `0x0004`) |
| `0x20` | CECLR | `1` = podnieś CS, koniec transakcji |
| `0x30` | RDREQ | `1` = odbierz jeden bajt |
| `0x54` | RD_DATARDY | bit 0: bajt odebrany |
| `0x58` | WR_DATARDY | bit 0: bajt wysłany |
| `0x5C` | WR_CMDRDY | bit 0: komenda przyjęta (tryb komend) |
| `0xA8` | TRIGGER | `0x3333` = tryb bajtowy włączony, `0x2222` = wyłączony |
| `0xFC` | RST | bit 2: reset kontrolera (przy blokowaniu 0, potem 1) |
| `0x1C8` | ? | bit 2 kasowany przez HAL na czas zapisu ❓ (u nas już jest 0) |

`HAL_SERFLASH_Config(pm_base, nonpm_base, …)` zapamiętuje też: nonPM + `0x1E00`, PM + `0x7800`, PM + `0x1C00`, nonPM + `0x1600` (zegary?).

### Sekwencje

**Tryb bajtowy** (używa go nasz sterownik do wszystkiego, a HAL do odczytu ID):
```
0x00 = 0xAAAA; 0xA8 = 0x3333           odblokuj, tryb bajtowy
każdy bajt do wysłania: 0x10 = bajt, czekaj na 0x58 bit 0
każdy bajt do odebrania: 0x30 = 1, czekaj na 0x54 bit 0, bajt = 0x14
0x20 = 1                               koniec transakcji (CS w górę)
0xA8 = 0x2222; 0x00 = 0x5555; 0xFC bit 2: 0, potem 1     zablokuj
```
Jedna transakcja SPI = jedno polecenie spi-mem (np. WREN to osobna transakcja, kasowanie sektora `0xD8` + 3 bajty adresu to następna, potem odpytywanie statusu `0x05`).

**Tryb komend** (HAL MStara używa go do zapisu, `HAL_SERFLASH_Write`, strony po 256 B):
```
czekaj 0x5C; 0x04 = 0x06 (WREN); 0x08 = adr[15:0]; 0x0C = adr[23:16]
czekaj 0x5C; 0x04 = 0x02 (PP); każdy bajt: 0x10 = bajt, czekaj 0x58
0x20 = 1 (start programowania); czekaj na koniec: 0x04 = 0x05 + 0x30 = 1, aż bit 0 w 0x14 = 0
```
Mógłby przyspieszyć zapis; nasz sterownik go nie używa.

## Do zrobienia

- Zdejmowanie i przywracanie ochrony (GPIO7 + rejestr statusu) w sterowniku.
- Szybszy zapis trybem komend.
- JFFS2 na zapisywalnej partycji `/home`.
- W DTS pełnego kernela (`linux_msd781x`) węzeł `&isp` / `flash@0` dalej ma błędny `macronix,mx25l8005`; poprawiony jest tylko w `kernel_mini`.
