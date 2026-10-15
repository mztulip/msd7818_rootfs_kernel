# Przyciski panelu: SAR ADC

> **W WIWA HD95 przyciski NIE idą przez SAR ADC** (✅ na płytce). Panel ma układ **FD650**, który skanuje klawisze i steruje wyświetlaczem LED. Piny SAR 1/2 pracują jako GPIO: DIO i CLK magistrali FD650 (`docs/gpio.md`). Obsługuje to sterownik `mstar-fd650-panel.ko` (`docs/i2c.md`). Poniższy opis dotyczy płyty referencyjnej MStara (SAR z drabinką rezystorów) i blok SAR jako ADC. Zostaje jako materiał pomocniczy.

## Jak to działa (🔍 ze źródeł MBoota)

Przyciski nie są na GPIO, tylko na **SAR ADC z drabinką rezystorów**. Kilka przycisków siedzi na jednym pinie, a wciśnięcie zmienia napięcie i ADC rozpoznaje klawisz po progu.

Środowisko U-Boota ma `MS_BOARD=BD_MST124SZ.h` ✅, więc tabela klawiszy pochodzi z `mstar-mboot-master/sboot/inc/kronus/board/MST124SZ/keypad.h`:

| Włączony | Kanał | Liczba poziomów | Progi (8-bit) | Kody klawiszy |
|---|---|---|---|---|
| tak | 0 | 4 | `27 46 63 7A` | `52 07 06 5A` |
| nie | 1 | 4 | `27 46 63 7A` | `46 04 1A 13` |
| nie | 2 | — | — | — |
| tak | 3 | 8 | `10 2F 4D 71 92 AB C3 E7` | `46 1F 19 16 15 0F 04 07` |

To jest konfiguracja płyty referencyjnej MStara. Producent tunera mógł ją zmienić; numer kanału i progi trzeba potwierdzić pomiarem ❓.

Zanim zaczniesz, sprawdź na płytce panelu, czy przyciski idą przez rezystory do jednego przewodu. Jeśli jest tam osobny układ wyświetlacza LED (np. FD650/TM1650), to on obsługuje klawisze, a nie SAR.

## Rejestry (🔍 z `halSAR.o`)

Blok SAR jest w PM RIU: `0x1400` słownie → **`0xBF002800`**. Zgadza się to z `sar@2800` w `common.dtsi`.

`HAL_SAR_GetChannelADC` czyta 8-bitową wartość kanału:

| Kanał | Adres CPU (bajt) | Rejestr |
|---|---|---|
| 0 | `0xBF002834` | reg `0x141A`, młodszy bajt |
| 1 | `0xBF002835` | reg `0x141A`, starszy bajt |
| 2 | `0xBF002838` | reg `0x141C`, młodszy |
| 3 | `0xBF002839` | reg `0x141C`, starszy |
| 4 | `0xBF00283C` | reg `0x141E`, młodszy |
| 5 | `0xBF00283D` | reg `0x141E`, starszy |
| 6 | `0xBF002840` | reg `0x1420`, młodszy |
| 7 | `0xBF002841` | reg `0x1420`, starszy |

`HAL_SAR_CfgChannelBound` zapisuje górne i dolne granice kanałów do rejestrów `0x1418`/`0x1419` i `0x140A`/`0x140B` (ogólnie bloki w `0xBF002810`–`0xBF002830`).

Przypuszczenie: baza dla SAR to `0xBF000000` (PM). Kod HAL używa tu zmiennej `_gMIO_MapBase`, ustawianej w czasie działania ❓.

## Test w U-Boocie

```
md.w bf002834 4      # nic nie wciśnięte
md.w bf002834 4      # przytrzymaj przycisk i powtórz
```
Kanał 0 to młodszy bajt słowa `bf002834`, a kanał 3 starszy bajt słowa `bf002838`. Bez wciśnięcia odczyt powinien być w okolicach `0xFF` (górna granica `0xFF` w `keypad.h`).

Jeśli odczyty się nie zmieniają, możliwe że SAR nie jest uruchomiony przed startem aplikacji. MBoot ma stringi `MDrv_SAR_Init fails` i `MDrv_SAR_Enable fails`, więc inicjalizuje go sam, ale nie wiadomo w którym momencie ❓.

## W Linuksie

Gdy odczyt działa, wystarczy:
- najprościej: program w userspace czytający rejestr przez `/dev/mem` (fizycznie `0x1F002834`),
- docelowo: sterownik IIO dla SAR + `adc-keys` w DTS (węzeł `sar` w `common.dtsi` istnieje, ale ma `status = "disabled"` i nie ma `compatible` dla Kronusa).

## Uwaga: PM51

W fabrycznym firmware przyciski SAR odczytuje PM51 (osobny 8051 w domenie PM), który przekazuje kody klawiszy MIPS-owi przez skrzynkę pocztową (stringi `[SAR] [MCU51]…` w firmware PM51 pod `0x10000` we flashu). Sam blok SAR to zwykłe rejestry, więc pod Linuksem można go czytać bezpośrednio, bez PM51. Szczegóły w [ir_pilot.md](ir_pilot.md).

## Odczyt na płycie (2026-10-13, Linux) ✅

Po starcie Linuksa z MBoota SAR **nie jest uruchomiony**: `0x1F002800` = `0x0940`, a `0x1F002804`–`0x1F002840` = `0x0000` (bez naciskania przycisków). Przy działającym ADC kanały bez wciśnięcia powinny pokazywać ok. `0xFF`. Zanim przyciski zadziałają, trzeba odtworzyć inicjalizację SAR (`MDrv_SAR_Init/Config/Enable`, `HAL_SAR_Cfg*` z `libdrvSAR.a`).

## Uruchomienie SAR z Linuksa ✅ i test przycisków (2026-10-13)

Rejestry sterujące (z `halSAR.o`, CPU = `0x1F002800` + …):

| Reg | CPU | Bity |
|---|---|---|
| `0x1400` | `0x1F002800` | [2:0] kanał (tryb jednego kanału), 3 tryb wyzwalania, 4 tryb jednego kanału, 5 tryb pracy (1 = ciągły), **6 wyłączenie części cyfrowej**, 7 start |
| `0x1401` | `0x1F002801` | **0 wyłączenie części analogowej**, 1 ciągła praca, 2 wybór, 3 kanały wyższe |
| `0x1402` | `0x1F002804` | okres próbkowania |
| `0x1422` | `0x1F002844` | bity 0–3: kanał 0–3 jako wejście analogowe SAR |
| `0x1423` | `0x1F002845` | bity 0–3: wyłączenie wyjścia pinu (1 = wejście) |
| `0x1428` | `0x1F002850` | bit 0 maska przerwania, bit 1 kasowanie |

Po starcie SAR jest wyłączony (`0x2800` = `0x0940` albo `0x0A20`, kanały = 0). `MDrv_SAR_*` w MBoocie nie dotyka rejestrów, tylko wysyła polecenia do PM51 (klasy MBX 50/51), a PM51 nie działa.

Ręczne włączenie (działa):
```sh
devmem 0x1F002844 8 0x0F   # kanały 0-3 analogowe
devmem 0x1F002845 8 0x0F   # piny jako wejścia
devmem 0x1F002804 8 0x05   # okres próbkowania
devmem 0x1F002801 8 0x0A   # analog włączony, kanały wyższe
devmem 0x1F002800 8 0xA0   # cyfrowy włączony, praca ciągła, start
devmem 0x1F002834 16; devmem 0x1F002838 16   # odczyt kanałów
```
Wynik bez naciskania: kanały 0, 1, 2 = `0xFF`, **kanał 3 = `0x97`–`0x98`**, kanały 4–7 = `0x00`.

**Test przycisków POWER / CH+ / CH−** (`tools/board/regwatch.c`, 2 × 40–45 s naciskania): **brak jakiejkolwiek zmiany** na kanałach SAR i w bankach PM `0x0E00`–`0x11FF` (sterowanie/GPIO PM), `chiptop` (`0x101E00`) i `adc_atop`/`adc_dtop`. Przyciski najpewniej nie są podłączone do SAR, wbrew `keypad.h` płyty referencyjnej ❓. Kandydat: 3-przewodowy sterownik panelu (CLK/DATA/STB), obsługiwany w firmware PM51 (`s_pFPInterfaceCfg`, `m_powerKeyId`, patrz [ir_pilot.md](ir_pilot.md)). Klawisze takiego układu odczytuje się protokołem szeregowym, a nie z rejestru.

⚠️ **Odczyt całej przestrzeni RIU** (`0x1F000000`–`0x1F007FFF` i `0x1F200000`–`0x1F20FFFF`, bez UART i FIFO IR) **zawiesza płytę**; pomaga tylko odłączenie zasilania. `regwatch` czyta więc wyłącznie banki sprawdzone jako bezpieczne.

## ✅ Rozwiązanie: przyciski i wyświetlacz są na układzie panelu FD650 (2026-10-13)

Przyciski **nie są** odczytywane przez ADC SAR. Panel przedni ma 4-cyfrowy wyświetlacz 7-segmentowy i 3 przyciski, sterowane układem typu **FD650 / TM1650**: dwuprzewodowy interfejs podobny do I2C, ale bez adresu urządzenia. Na płytce panelu od góry nie widać scalaka, więc może być od spodu albo na płycie głównej ❓. MIPS steruje nim programowo przez GPIO.

### Jak to znaleziono

W fabrycznej aplikacji eCos (`decompressed.bin`, ładowana pod `0x80000180`):
- `MDrv_Keypad_Init` (`0x80180C44`) tworzy timer co 100 ms z funkcją `0x80180B00`, która odczytuje klawisz i zamienia kody: `0x47` → `0x10`, `0x4F` → `0x0B`, `0x57` → (gaszenie wyświetlacza) `0x0C`;
- bit-banging: `0x800DC3B0` = GPIO w stan wysoki, `0x800DC3E8` = niski, `0x800DC420` = wejście, `0x800DC494` = odczyt; `0x80180458` = pętla opóźniająca;
- numery GPIO przełożone na rejestry przez tablicę `gpio_table` z `libdrvGPIO.a`/`halGPIO.o` (K1): 116 wpisów po 24 B `{OEN reg, maska, OUT reg, maska, IN reg, maska}`, adresy względem PM `0xBF000000`.

### Piny ✅

| GPIO | Funkcja | Pin | OEN (1 = wejście) | OUT | IN |
|---|---|---|---|---|---|
| 18 | **DIO** | SAR kanał 1 | `0x1423` bit 1 = `0x1F002845` | `0x1424` bit 1 = `0x1F002848` | `0x1425` bit 1 = `0x1F002849` |
| 19 | **CLK** | SAR kanał 2 | `0x1423` bit 2 | `0x1424` bit 2 | `0x1425` bit 2 |

Piny SAR 1 i 2 muszą być w trybie cyfrowym: `0x1422` (`0x1F002844`) bity 1 i 2 = 0. Moja wcześniejsza inicjalizacja SAR (`0x1F002844` = `0x0F`) przełączała je na analog, więc trzeba jej unikać. Pozostałe piny SAR: GPIO 17 = SAR 0, GPIO 20 = SAR 3. GPIO 16 = PM `0x0E1F`/`0x0E21`/`0x0E23` bit 1; fabryczna aplikacja ustawia go na 0 przy inicjalizacji panelu (`0x80180860`), znaczenie nieznane ❓.

### Protokół (wg fabrycznej aplikacji, sprawdzony)

- **START:** DIO=1, CLK=1, DIO=0, CLK=0.
- **STOP:** DIO=0, CLK=1, DIO=1.
- **Bajt:** od najstarszego bitu; DIO ustawiany przy CLK=0, zatrzaskiwany zboczem narastającym CLK. 9. takt to ACK: układ ciągnie DIO do 0.
- Opóźnienie między krokami ok. 10 µs (działa).

| Operacja | Ramka |
|---|---|
| włączenie i jasność | `S 48 XX P`, XX = `(jasność 1..7) << 4 \| 0x01` (fabrycznie `0x11`, `0x41`, `0x71`). Bez tej komendy klawisze się nie skanują |
| cyfra | `S 6E seg P` (lewa), `S 6C seg P`, `S 6A seg P`, `S 68 seg P` (prawa) |
| odczyt klawisza | `S 4F` + odczyt 8 bitów (master daje ACK=0) + `P` |

### Kody klawiszy ✅ (test z poleceniami głosowymi)

| Przycisk | Wciśnięty | Puszczony |
|---|---|---|
| **POWER** | `0x57` | `0x17` |
| **CH+** | `0x4F` | `0x0F` |
| **CH−** | `0x47` | `0x07` |

Bit 6 = przycisk wciśnięty. Bez naciśnięcia, przed pierwszym użyciem, układ zwraca `0x2E`.

### Font 7-segmentowy ✅ (z `0x8038283C`, 38 wpisów `{znak, segmenty, 0}`)

`0`=D7 `1`=14 `2`=CD `3`=5D `4`=1E `5`=5B `6`=DB `7`=15 `8`=DF `9`=5F `A`=9F `B/b`=DA `C`=C3 `c`=C8 `D/d`=DC `E`=CB `F`=8B `H`=9E `h`=9A `I`=82 `i`=80 `L`=C2 `n`=98 `N`=97 `O/o`=D8 `P`=8F `R/r`=C8 `S`=5B `T/t`=CA `U`=D6 `V`=D0 `-`=08 spacja=00. **Kropka = `0x20`** (bit, którego brakuje w `8`). `8.8.8.8.` z jasnością 7 i tekst `HELO` wyświetlają się poprawnie ✅.

### Narzędzie: `tools/board/frontpanel.c`

```sh
gcc -O2 -o frontpanel frontpanel.c          # na płycie
./frontpanel cmd 0x48 0x11                  # włącz układ (potrzebne do skanowania klawiszy)
./frontpanel keys 30                        # odpytywanie co 100 ms przez 30 s, wypisuje zmiany kodu
./frontpanel disp "12.34" 7                 # tekst (do 4 znaków, '.' = kropka po znaku), jasność 1..7
./frontpanel raw 0xdf 0xdf 0xdf 0xdf        # surowe segmenty
```

## Sterownik Linuksa `mstar-fd650-panel`

Plik: `linux_msd781x/drivers/input/keyboard/mstar-fd650-panel.c`, opcja `CONFIG_KEYBOARD_MSTAR_FD650` (Kconfig/Makefile w `drivers/input/keyboard/`).

- **Przyciski:** urządzenie wejściowe „Front panel keys” (`/dev/input/eventN`), odpytywane co 100 ms (`input_setup_polling`). Mapowanie: `0x17` → `KEY_POWER`, `0x0F` → `KEY_CHANNELUP`, `0x07` → `KEY_CHANNELDOWN`; zdarzenie wciśnięcia i puszczenia z bitu 6. Mapowanie można zmienić przez `EVIOCSKEYCODE`.
- **Wyświetlacz** (sysfs, katalog urządzenia `/sys/devices/platform/1f000000.riu/1f002840.front-panel/`):
  - `text`: do 4 znaków z fontu płytki, `.` zapala kropkę poprzedniego znaku, nieznany znak = `-`;
  - `brightness`: `0` = wyłączony (układ dalej skanuje klawisze), `1`–`7`. Domyślnie `1`.
- Przy starcie: piny SAR 1 i 2 w tryb cyfrowy, układ włączony, pusty wyświetlacz. Gdy układ nie odpowiada (brak ACK), probe kończy się błędem.

Węzeł DTS (`msd7816-test.dts`):
```dts
&riu {
	front-panel@2840 {
		compatible = "mstar,msd7816-fd650-panel";
		reg = <0x2840 0x10>;   /* rejestry 0x1422-0x1425: tryb pinów SAR i GPIO */
	};
};
```
Offsety od `reg`: `+0x04` tryb analogowy pinów SAR, `+0x05` OEN, `+0x08` OUT, `+0x09` IN. DIO = bit 1, CLK = bit 2.

Przykład użycia:
```sh
P=/sys/devices/platform/1f000000.riu/1f002840.front-panel
echo "12.34" > $P/text
echo 7 > $P/brightness
cat /proc/bus/input/devices      # znajdź eventN dla "Front panel keys"
hexdump -C /dev/input/eventN     # zdarzenia przycisków (albo evtest)
```

### Test sterownika jako modułu ✅ (2026-10-13)

`insmod mstar-fd650-panel.ko` (kernel z modułami, patrz [i2c.md](i2c.md): wbudowane sterowniki powodowały drganie HDMI). Wyświetlacz przez sysfs działa (`echo I2C > text`), a zdarzenia z `/dev/input/event0` (32-bit MIPS: 16 B `{sec, usec, type, code, value}`) przy teście z poleceniami głosowymi to: POWER → `KEY_POWER`, CH+ → `KEY_CHANNELUP`, CH− → `KEY_CHANNELDOWN`.

Do poprawy: przytrzymany przycisk czasem daje serię wciśnięć i puszczeń co 100 ms (drgania styków albo chwilowe kody „puszczony”). Potrzebny filtr, np. puszczenie dopiero po 2 kolejnych odczytach bez bitu 6.
