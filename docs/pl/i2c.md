# Sprzętowe I2C (MIIC)

## Najważniejsze

- MBoot obsługuje tuner przez **sprzętowe I2C** (`MDrv_HWI2C_*`), nie przez bit-bang. W zrzucie RAM są stringi `MDrv_IIC_InitPort0: OK` i `HWI2CMUTEXP0..3`, a `tuner_init` wypisuje `MDrv_IIC_InitPort0: OK` ✅.
- Tuner **MxL5007T** jest na **porcie 0**, pod adresem `myTuner->I2C_Addr = 192`, czyli `0xC0` 8-bitowo i **`0x60` 7-bitowo** ✅ (log z `i2c_analiza`).
- W zrzucie jest też programowe I2C (`i2c_Start()::SCL or SDA could not pull low`, `!!!Acquire Send Mutex for SWI2C Failed!`). Sądząc po stringach, służy ono raczej HDMI/HDCP, a nie tunerowi ❓.

## Porty i rejestry (🔍 z `halHWI2C.o`)

`HAL_HWI2C_SetPortRegOffset`:

| Wartość `HWI2C_PORT` | Port | Offset rejestrów | Baza CPU |
|---|---|---|---|
| `0..3` (`E_HWI2C_PORT_0` … `PORT0_3`) | MIIC0 | `0x00` | `0xBF206800` |
| `4..7` | brak, funkcja zwraca błąd | | |
| `8..15` (`E_HWI2C_PORT_1` … `PORT1_7`) | MIIC1 | `0x20` słów (`0x40` bajtów) | `0xBF206840` |

To się zgadza z `kronus.dtsi` (`miic@206800`, `miic@206840`).

Rejestry używane przez HAL (offset słowny względem `0x3400` portu):

| Reg (słowo) | Adres CPU (port 0) | Kto używa | Uwagi |
|---|---|---|---|
| `0x1A` | `0xBF206834` | `HAL_HWI2C_SendData` (zapis bajtu) | ❓ rejestr danych do wysłania |
| `0x1C` | `0xBF206838` | `HAL_HWI2C_SetClk` (maska `0x83`), `HAL_HWI2C_Start` (maska `0x7C`) | Po starcie jest tu `0xF8F8` na obu portach ✅ |

Pełną listę offsetów można wyciągnąć z pozostałych funkcji `HAL_HWI2C_*` (patrz [ghidra_funkcje.md](ghidra_funkcje.md)).

## Pinmux (🔍)

`HAL_HWI2C_SelectPort` zapisuje do chiptop, rejestr `0x1E12` → **`0xBF203C24`**:

| Port | Bity | Wartość |
|---|---|---|
| MIIC0 (`PORT0_0..3`) | `[1:0]` | 0, 1, 2 lub 3 (wariant padów) |
| MIIC1 (`PORT1_x`) | `[6:4]` | `0x00`–`0x70` co `0x10` (wariant padów) |

Odczyt `md.w bf203c24 1` **po `tuner_init`** pokaże, który wariant padów wybrał MBoot. Ta sama wartość będzie potrzebna w Linuksie (pinctrl albo ręczny zapis).

`HAL_HWI2C_Init_Chip` czyści dwa bity:
- `0x1EA1` bit 7 → `0xBF203D41` bit 7: `reg_allpad_in`. Musi być 0, inaczej wszystkie pady są wejściami. Zbieżne z kodem z `boot.S`: `RREG 0xbf203d44 / and 0x7FFF` ❓.
- `0x13003` bit 7 → `0xBF226005` bit 7, w banku `0x226000` (wg `kronus.dtsi` to blok HDMI TX). Znaczenie nieznane ❓.

## Obserwacje z płyty

Po `tuner_init` zmienia się obszar `0xBF206900` (`0x0100`, `0x5E6E`, `0x22`, `0x60`) ✅. **To nie jest I2C.** `INTERN_DVBT_Power_On_Initialization` z `libdrvDEMOD.a` używa rejestrów `0x103480`–`0x103485` (CPU `0xBF206900`–`0xBF206909`) do ładowania firmware'u do 8051 w demodulatorze 🔍. Szczegóły w [demodulator_if.md](demodulator_if.md). Zbieżność `0x60` z adresem tunera jest najpewniej przypadkowa ❓.

## Dlaczego I2C nie działa w Linuksie (hipotezy ❓)

1. W DTS jest `clocks = <&mpll_24m>; // TODO, clkgen2!`, więc zegar może być zły albo wyłączony.
2. Nikt nie ustawia pinmuxu `0xBF203C24`. Jeśli kernel startuje po MBoocie bez `tuner_init`, pady mogą nie być przełączone na I2C.
3. Sterownik `msc313e-i2c` (Infinity, ARM) może mieć inny układ rejestrów niż MIIC w Kronusie. Warto porównać z offsetami z `halHWI2C.o`.

## Proponowany test w U-Boocie

```
tuner_init
md.w bf203c24 1       # wariant pinmux
md bf206800 20        # rejestry portu 0
md bf206900 10        # obszar zmieniany przez tuner_init
```
Potem porównać to samo w Linuksie przez `devmem` (adres fizyczny `0x1F......`).

## Odczyt na płycie (2026-10-13, Linux, bez `tuner_init`) ✅

- Pinmux `0xBF203C24` = `0x0000`: pady MIIC nie są przełączone, bo MBoot bez `tuner_init` ich nie ustawia. Potwierdza to hipotezę 2 z sekcji „Dlaczego I2C nie działa w Linuksie”.
- `reg_allpad_in` (`0xBF203D40`) = `0x0000`, czyli w porządku.
- MIIC0 `0xBF206800`–`0xBF20683C`: same zera poza `+0x38` = `0xF8F8`, tak jak w U-Boocie.

## ✅ Sprzętowe I2C działa: ręczna transakcja z MxL5007T (2026-10-13, MBoot)

### Rdzeń I2C Kronusa = kontroler w stylu Philips SIO1 (8xC552), nie `msc313e`

Rejestry bajtowe portu 0 (z `halHWI2C.o`, potwierdzone na płycie). Port 1 = te same offsety + `0x20` słów (CPU +`0x40`).

| Reg | CPU | Nazwa | Opis |
|---|---|---|---|
| `0x3400` | `0xBF206800` | RESET | `1` → `0` = reset kontrolera (`HAL_HWI2C_Reset`) |
| `0x341A` | `0xBF206834` | S1DAT | dane do wysłania / odebrane |
| `0x341C` | `0xBF206838` | S1CON | bit 7 = CR2, 6 = **ENS1** (włącz), 5 = **STA**, 4 = **STO**, 3 = **SI** (flaga zakończenia kroku; zapis 0 kasuje), 2 = **AA** (wysyłaj ACK), 1:0 = CR1:CR0 (zegar) |
| `0x341D` | `0xBF206839` | S1STA | kod stanu (tylko odczyt) |

Funkcje HAL: `SetClk` = maska `0x83` (wartości `1`, `2`, `3`, `0x81`, `0x82`, `0x83`), `Start` = `0x64` w masce `0x7C`, `Stop` = `0x50` w masce `0x7C`, `Clear_INT` = `0x40` w masce `0x78`, `NoAck` = `0x40` w masce `0x74`, `Is_INT` = S1CON bit 3. `WaitINT` po fladze sprawdza S1STA.

Kody S1STA (tryb master, standard Philips): `0x08` START, `0x10` powtórzony START, `0x18` SLA+W ACK, `0x20` SLA+W NACK, `0x28` dane ACK, `0x30` dane NACK, `0x38` utrata arbitrażu, `0x40` SLA+R ACK, `0x48` SLA+R NACK, `0x50` odebrano + ACK, `0x58` odebrano + NACK, `0xF8` bezczynny.

**Sterownik `i2c-msc313e` (Infinity) ma zupełnie inny układ rejestrów (STARTSTOP/WDATA/RDATA/INT_STAT) i na Kronusie nie zadziała.** Potrzebny jest osobny, prosty sterownik dla tego rdzenia.

### Pinmux ✅

`tuner_init` zmienia w chiptop tylko `0xBF203C24` (`0x1E12`): `0x0000` → **`0x0001`**, czyli port 0, wariant padów 1. Pozostałe zmiany (`0xBF203C70` = `0x0100`, `0xBF203C74` = `0x0001`, `0xBF203C7C` = `0x0002`) robi inicjalizacja demodulatora. `0xBF226005` (`0x113003`) przed i po = `0x00`.

### Sekwencja odczytu rejestru MxL5007T (wykonana w MBoot po `tuner_init`)

Wartość S1CON: `0x41` = ENS1 + CR=1. Każdy zapis S1CON z SI=0 kasuje flagę i uruchamia następny krok.

```
mw.b bf206800 1 ; mw.b bf206800 0     # reset kontrolera
mw.b bf206838 41                       # ENS1, zegar CR=1
mw.b bf206838 65                       # START (+AA)           -> S1STA 08
mw.b bf206834 c0 ; mw.b bf206838 45    # adres 0x60, zapis     -> 18 (ACK)
mw.b bf206834 fb ; mw.b bf206838 45    # MxL: polecenie odczytu -> 28
mw.b bf206834 d9 ; mw.b bf206838 45    # numer rejestru         -> 28
mw.b bf206838 65                       # powtórzony START      -> 10
mw.b bf206834 c1 ; mw.b bf206838 45    # adres 0x60, odczyt    -> 40 (ACK)
mw.b bf206838 41                       # odbiór bajtu z NACK   -> 58
md.b bf206834 1                        # = 0x14
mw.b bf206838 55                       # STOP                  -> F8
```
Stan sprawdza się przez `md.b bf206838 2` (bajt 0 = S1CON, bajt 1 = S1STA).

**Wynik: rejestr `0xD9` = `0x14`, czyli MxL5007T v4** (wg `drivers/media/tuners/mxl5007t.c`).

### ✅ To samo z Linuksa, bez `tuner_init` (2026-10-13)

Na świeżo uruchomionym Linuksie (pinmux = `0x0000`) wystarczy **jeden zapis pinmuxu**, żeby I2C zadziałało. Zegar ani `0x113003` nie wymagają ustawiania. Adresy fizyczne `0x1F……`:

```sh
C=0x1F206838; D=0x1F206834
devmem 0x1F203C24 16 0x0001                    # pinmux: port 0, wariant 1
devmem 0x1F206800 8 1; devmem 0x1F206800 8 0   # reset kontrolera
devmem $C 8 0x41                               # ENS1, CR=1
devmem $C 8 0x65                               # START          -> 08
devmem $D 8 0xC0; devmem $C 8 0x45             # SLA+W          -> 18
devmem $D 8 0xFB; devmem $C 8 0x45             #                -> 28
devmem $D 8 0xD9; devmem $C 8 0x45             #                -> 28
devmem $C 8 0x65                               # rep. START     -> 10
devmem $D 8 0xC1; devmem $C 8 0x45             # SLA+R          -> 40
devmem $C 8 0x41                               # bajt + NACK    -> 58
devmem $D 8                                    # = 0x14 (MxL5007T v4)
devmem $C 8 0x55                               # STOP           -> F8
```

Wniosek: dlaczego I2C nie działało w Linuksie, wyjaśnione. Sterownik `msc313e` nie pasuje do tego rdzenia, a pinmux był wyłączony. Do Linuksa potrzeba: (1) sterownika magistrali I2C dla rdzenia SIO1 (~200 linii, `i2c_algorithm.master_xfer` na powyższych krokach, czekanie na SI = bit 3 S1CON), (2) ustawienia pinmuxu `0x1E12` = 1 (w sterowniku albo w DTS), (3) w DTS węzła `miic@206800` z nowym `compatible` i dziecka `tuner@60` (`maxlinear,mxl5007t`, jeśli taki binding istnieje, albo dostępu przez `i2c-dev` + `i2ctransfer`).

## ✅ Sterownik Linuksa `i2c-mstar-miic` (2026-10-13)

Plik: `linux_msd781x/drivers/i2c/busses/i2c-mstar-miic.c`, opcja `CONFIG_I2C_MSTAR_MIIC` (Kconfig/Makefile w `drivers/i2c/busses/`). Obsługuje `compatible = "mstar,msd7816-miic"`, czyli oryginalną wartość z `kronus.dtsi`; zmiana na `mstar,msc313e-i2c` została cofnięta.

- `master_xfer` realizuje kroki z sekcji o ręcznej transakcji: START/powtórzony START, adres, bajty z ACK, ostatni bajt odbioru z NACK, STOP. Czeka na SI (`S1CON` bit 3, timeout 10 ms) i mapuje kody S1STA na błędy (`-ENXIO` przy NACK adresu, `-EAGAIN` przy utracie arbitrażu, `-EIO`, `-ETIMEDOUT` z resetem kontrolera).
- Funkcje: `I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL`, więc działają `i2cdetect`, `i2cget`, `i2ctransfer`.
- Pinmux ustawia sam sterownik przez syscon `chiptop` (rejestr `0x24` = `0x1E12`, port 0: bity `[1:0]`, port 1: bity `[6:4]`).

Właściwości DTS (`msd7816-test.dts`):
```dts
&miic0 {
	#address-cells = <1>;
	#size-cells = <0>;
	mstar,chiptop = <&chiptop>;
	mstar,port = <0>;
	mstar,pad-mode = <1>;
	/* opcjonalnie: mstar,clock-rate = <1>;  surowe bity CR w S1CON (domyślnie 1) */
	status = "okay";
};
```
Z `&miic0` usunięto węzły `temp-sensor@4c` (`gmt,g781`) i `rtc@68` (`dallas,ds1340`), bo na tej płycie ich nie ma (skan). `miic1` (port 1, `/dev/i2c-1`) też się rejestruje, ale nie ma ustawionego pinmuxu, więc jest bezużyteczny; można mu dać `status = "disabled"`.

### Budowanie i uruchomienie (jak zrobiono w teście)

```sh
cd linux_msd781x
./scripts/config --enable CONFIG_I2C_MSTAR_MIIC
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- olddefconfig
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- -j6
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- dtbs
cp vmlinux vmlinux_i2c
mipsel-linux-gnu-objcopy --update-section .appended_dtb=arch/mips/boot/dts/mstar/msd7816-test.dtb vmlinux_i2c
mipsel-linux-gnu-objcopy -O binary vmlinux_i2c vmlinux_i2c.bin
readelf -h vmlinux_i2c | grep Entry       # 0x805dcfc0 dla tego builda
```
Plik `vmlinux_i2c.bin` trafił na FAT pendrive'a obok dotychczasowego `vmlinux.bin`, który nadal startuje automatycznie. W teście przesłano go przez UART (gzip + base64, ok. 7,5 min); szybciej jest skopiować go na pendrive na PC. Start ręczny z MBoota:
```
mw.w 0xBF006040 0 1; mw.w 0xBF006010 0 1; mw.w 0xBF006014 0 1   # watchdog off
osd_create 1 1
usb start
fatload usb 0 0x80000000 vmlinux_i2c.bin
go 0x805dcfc0
```

### Wynik na płycie ✅

```
# i2cdetect -l
i2c-1   i2c   1f206840.miic   I2C adapter
i2c-0   i2c   1f206800.miic   I2C adapter
# i2cdetect -y 0          -> tylko 60
# i2ctransfer -y 0 w2@0x60 0xfb 0xd9 r1@0x60
0x14
```
Pinmux po starcie = `0x0001` (ustawiony przez sterownik). Przykłady użycia tunera: [mxl5007t.md](mxl5007t.md).

## ⚠️ Drganie obrazu HDMI przy sterowniku wbudowanym w kernel → używać modułów (2026-10-13)

Objaw: z `i2c-mstar-miic` **wbudowanym** w kernel (`=y`) obraz HDMI (konsola „Welcome to Buildroot”) drga. Diagnostyka (bisekcja, każdy test oceniany wzrokowo na monitorze):

| Kernel / działanie | Drga? |
|---|---|
| oryginalny `vmlinux.bin` z pendrive'a (#1, 2025-09-03) | nie |
| `vmlinux_i2c.bin` / `vmlinux_dev.bin` (sterownik I2C `=y`) | **tak**, także po `unbind` obu portów |
| to samo drzewo, sterowniki wyłączone (`vmlinux_nodrv.bin`, #6) | nie |
| na niedrgającym kernelu przez `devmem`: pinmux `0x1E12`=1, reset+S1CON portu 0, reset+S1CON portu 1, piny i sterowanie panelu | nie (każde osobno) |
| kernel z modułami (`vmlinux_mod.bin`, #7) bez modułów | nie |
| `insmod i2c-mstar-miic.ko pinmux_16bit=1` (zapis 16-bit) | nie |
| `insmod i2c-mstar-miic.ko pinmux_16bit=0` (syscon `regmap`, 32-bit) | nie |
| + `insmod mstar-fd650-panel.ko` | nie |

Wniosek: przyczyną nie są wartości rejestrów, tylko **wczesna inicjalizacja przy starcie** (sterownik wbudowany, probe ok. 1,5 s). Dokładny mechanizm nieznany ❓, np. kolejność względem `simpledrm`/fbcon albo stanu HDMI TX zaraz po MBoocie. **Zalecenie: oba sterowniki budować jako moduły i ładować po starcie.**

Kernel z modułami: `CONFIG_MODULES=y`, `CONFIG_MODULE_UNLOAD=y`, `CONFIG_I2C_MSTAR_MIIC=m`, `CONFIG_KEYBOARD_MSTAR_FD650=m` (`.config.modules`), obraz `vmlinux_mod.bin`, punkt wejścia **`0x805e8404`**. Moduły (`drivers/i2c/busses/i2c-mstar-miic.ko`, `drivers/input/keyboard/mstar-fd650-panel.ko`, 8–11 KB) przesyła się przez UART w kilka sekund ([przesylanie_uart.md](przesylanie_uart.md)); bez przebudowy kernela wystarczy `make … modules`.

```sh
insmod /root/i2c-mstar-miic.ko            # opcjonalnie pinmux_16bit=1
insmod /root/mstar-fd650-panel.ko
i2cdetect -l                              # i2c-0 = 1f206800.miic (tuner), i2c-1 = 1f206840.miic
```

Parametr `pinmux_16bit` (bool, domyślnie 0) wybiera zapis pinmuxu: `regmap` syscon albo bezpośredni `readw`/`writew`. Oba działają.

Uwaga: pierwszy `i2ctransfer` zaraz po `insmod` zwrócił raz `ENXIO` (NACK adresu), a kolejne działały ❓.

### Automatyczne ładowanie modułów (opis, **nie wprowadzone**)

Na rootfs (Buildroot, BusyBox init) wystarczy skrypt startowy, np. `/etc/init.d/S90mstar`:
```sh
#!/bin/sh
case "$1" in
start)
	insmod /root/i2c-mstar-miic.ko
	insmod /root/mstar-fd650-panel.ko
	;;
stop)
	rmmod mstar_fd650_panel 2>/dev/null
	rmmod i2c_mstar_miic 2>/dev/null
	;;
esac
```
oraz `chmod +x /etc/init.d/S90mstar`. `rcS` uruchamia skrypty `S??*` w kolejności nazw. Lepiej trzymać moduły w `/lib/modules/$(uname -r)/` i użyć `modprobe` (wymaga `depmod`). Można też dodać skrypt do `buildroot/overlay/etc/init.d/`, żeby trafiał do każdego nowego obrazu rootfs. Moduły muszą pasować do kernela (`vermagic 6.7.0-rc6+ SMP mod_unload MIPS32_R2 32BIT`); po przebudowie kernela z inną konfiguracją trzeba je przebudować.

### Kernel z modułami jako domyślny ✅ (2026-10-13)

- Na FAT pendrive'a `vmlinux.bin` = `vmlinux_mod.bin` (#7, MD5 `ff0a37e9024c4c4e94fef4c5caf27a0d`). Poprzedni kernel zachowany jako **`vmlinux_orig.bin`** (#1, punkt wejścia `0x805db8a0`, MD5 `3ed2a29d…`).
- W MBoocie zmieniono i zapisano (`saveenv`, SPI flash `0x3E0000`/`0x3F0000`):
  ```
  bootlinux=osd_create 1 1;usb start;fatload usb 0 0x80000000 vmlinux.bin;go 0x805e8404;
  ```
  Poprzednio było `go 0x805db8a0`. `bootcmd` (watchdog off + `run bootlinux`) bez zmian.
- Sprawdzone: `reset` bez przerywania startuje #7.
- **Powrót do starego kernela** (z konsoli MBoota):
  ```
  setenv bootlinux osd_create 1 1\;usb start\;fatload usb 0 0x80000000 vmlinux_orig.bin\;go 0x805db8a0\;
  saveenv
  ```
- **Aktualizacja 2026-10-14:** domyślny jest teraz kernel #8 (`vmlinux_rgb.bin`, skopiowany jako `vmlinux.bin`, MD5 `226e9d5f…`, punkt wejścia **`0x805e86a4`**) z prawdziwym RGB565 i `logofix` zamiast `osd_create` ([grafika_gop.md](grafika_gop.md#logo-wiwa-przy-starcie-logofix-)):
  ```
  bootlinux=usb start;fatload usb 0 0x80F00000 logofix.bin;go 0x80F00000;fatload usb 0 0x80000000 vmlinux.bin;go 0x805e86a4;
  ```
  #7 jest zachowany jako `vmlinux_mod.bin` (`go 0x805e8404`, wymaga `osd_create 1 1`). Moduły `.ko` z #7 ładują się bez zmian w #8. Sprawdzone: obraz stabilny, tuner odpowiada `0x14`.
- **Uwaga:** każdy nowy build kernela ma inny punkt wejścia (`readelf -h vmlinux | grep Entry`). Przy podmianie `vmlinux.bin` trzeba zawsze poprawić `go` w `bootlinux`, bo inaczej płyta nie wystartuje (ratunek: konsola MBoota i ręczny `fatload`/`go`).
