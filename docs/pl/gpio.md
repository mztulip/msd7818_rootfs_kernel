# GPIO

## Co wiadomo

- MBoot zawiera sterownik `drvGPIO` z biblioteki K1. W zrzucie RAM znalezione są `mdrv_gpio_set_high/set_low/get_level` i `HAL_Gpio_*` (adresy w [ghidra_funkcje.md](ghidra_funkcje.md)) 🔍.
- `HAL_Gpio_Set_High` i podobne funkcje przyjmują **numer GPIO** i biorą adres rejestru z **tablicy padów** w `halGPIO.o` (sekcje danych). Tablica ma dla każdego padu rejestr OEN, rejestr OUT, rejestr IN i maski bitów 🔍. Jej odczytanie to następny krok, który da pełną mapę GPIO ❓.
- Według `kronus.dtsi` rejestry GPIO są rozrzucone po `chiptop` (`0x203C00`), `adc_atop` (`0x204A00`), `adc_dtop` (`0x204C00`) oraz w PM (komentarz `TODO - PM GPIOs`).
- Konfiguracja padów przy starcie: `mstar-mboot-master/sboot/src/kronus/padInitTbl.inc` i `drvPadConf.c`.
- SAR ma też tryb GPIO (`MDrv_SAR_Gpio_CfgDir/SetOutput/GetInput`), więc piny SAR, których nie zajmują przyciski, można użyć jako zwykłych GPIO 🔍.

## Następne kroki

1. Zrzucić tablicę padów z `halGPIO.o`: `mipsel-linux-gnu-objdump -s -j .data* halGPIO.o` (obiekty są rozpakowane przez `ar x` z `MstarCore/bsp/k1/lib/libdrvGPIO.a`).
2. Znaleźć w Ghidrze wywołania `mdrv_gpio_set_high/low` (xrefs). Argument w `a0` to numer GPIO, a funkcja wywołująca podpowie, do czego pin służy (LED, reset tunera, zasilanie anteny…).

## Tablica GPIO (2026-10-13) ✅

`gpio_table` w `halGPIO.o` (K1, sekcja `.rodata.gpio_table`, 116 wpisów, indeksy od 1) ma wpisy po 24 bajty: `{u32 rejestr OEN, u8 maska, u32 rejestr OUT, u8 maska, u32 rejestr IN, u8 maska}` (wyrównanie do 4). Adresy rejestrów są w przestrzeni PM, czyli CPU = `0xBF000000 + (reg<<1) - (reg&1)`, fizycznie `0x1F…`. `HAL_Gpio_Set_High`: `OUT |= maska; OEN &= ~maska` (OEN=0 oznacza wyjście).

Odczytane wpisy:

| GPIO | OEN | OUT | IN | Uwagi |
|---|---|---|---|---|
| 16 | `0x0E1F`/02 | `0x0E21`/02 | `0x0E23`/02 | PM; panel ustawia go na 0 przy inicjalizacji ❓ |
| 17 | `0x1423`/01 | `0x1424`/01 | `0x1425`/01 | pin SAR 0 |
| 18 | `0x1423`/02 | `0x1424`/02 | `0x1425`/02 | pin SAR 1 = **DIO panelu FD650** |
| 19 | `0x1423`/04 | `0x1424`/04 | `0x1425`/04 | pin SAR 2 = **CLK panelu FD650** |
| 20 | `0x1423`/08 | `0x1424`/08 | `0x1425`/08 | pin SAR 3 |

Dekodowanie całej tablicy: `objcopy -O binary --only-section=.rodata.gpio_table halGPIO.o gpio_table.bin`, potem `struct.unpack('<IB3xIB3xIB3x', entry)`. Piny SAR jako GPIO wymagają wyzerowania bitu w `0x1422` (tryb cyfrowy). Szczegóły panelu: [przyciski_sar.md](przyciski_sar.md).

## Znane funkcje pinów (2026-10-19, zweryfikowane na płytce) ✅

Wpisy `adc_atop` w `gpio_table` (GPIO 22–87) mają jeden rejestr 16-bit na pin: `0x20` OEN (0 = wyjście), `0x10` OUT, `0x01` IN. CPU: `0xBF000000 + reg·2`, z Linuksa `devmem 0x1F000000+reg·2 16`.

| GPIO | Rejestr | Funkcja | Stan |
|---|---|---|---|
| 44 | `0x10253C` | wyciszenie wyjścia audio SCART/analog | 0 = dźwięk, 1 = wyciszony (stan po MBoocie) |
| 47 | `0x102542` | SCART pin 16 (RGB / fast blanking) | 0 = CVBS (zostawiamy), 1 = RGB (oryginał) |

Szczegóły i sposób znalezienia: [audio.md](audio.md) (sekcja o cichym SCART). Pozostałe różnice między oryginałem a Linuksem w bankach GPIO: tryb SAR (`0x1400`, `0x142E`), PM `0x0E1C` bit 9, chiptop `0x101E22/38/3A/3E` (mux padów) — funkcja nieznana ❓.
