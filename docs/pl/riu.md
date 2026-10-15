# Adresowanie rejestrów (RIU)

Wszystkie peryferia MStara są na szynie RIU (Register Interface Unit). Rejestry są **16-bitowe**, ale w przestrzeni CPU każdy zajmuje **4 bajty**: młodszy bajt jest pod `+0`, starszy pod `+1`, a `+2` i `+3` są puste. Dlatego `md` w U-Boocie pokazuje wartości typu `0000F8F8`.

## Bazy (MIPS, KSEG1, bez cache)

| Obszar | Adres CPU | W Linux DTS | Status |
|---|---|---|---|
| PM RIU (watchdog, SAR, RTC, PM GPIO) | `0xBF000000` | `riu@1f000000` + offset < 0x200000 | ✅ watchdog `0xBF006010` działa |
| non-PM RIU (UART, MIIC, chiptop…) | `0xBF200000` | `riu@1f000000` + `0x2xxxxx` | ✅ UART0 `0xBF201300` działa |

Logi MBoota to potwierdzają: `u32PMRiuBaseAddr = BF000000`, `u32PnlRiuBaseAddr = BF200000`.

Pod `0x1F......` nie ma dostępu z U-Boota (wyjątek). Linux mapuje fizyczne `0x1F000000` sam, a z U-Boota używamy `0xBF......`.

## Przeliczanie adresów z kodu MStara

Kod i nagłówki MStara podają **numer 16-bitowego rejestru** (adres „słowny”), np. `0x1E12`, `0x341C`, albo w pełnej formie `0x101E12`. Prefiks `0x10....` oznacza obszar non-PM, a `0x00....` obszar PM.

Funkcje `HAL_*_ReadByte` z bibliotek K1 (🔍) liczą adres **bajtu** tak:

```
adres_CPU = baza + (reg << 1) - (reg & 1)
```

- parzysty `reg` → młodszy bajt rejestru `reg` pod `baza + reg*2`,
- nieparzysty `reg` → starszy bajt pod `baza + (reg-1)*2 + 1`.

Przykłady:

| W kodzie | Obszar | Adres CPU | Uwagi |
|---|---|---|---|
| `0x1E12` | non-PM | `0xBF203C24` | chiptop, pinmux I2C |
| `0x1EA1` (bit 7) | non-PM | `0xBF203D41` (bit 7) = bit 15 słowa `0xBF203D40` | `reg_allpad_in` |
| `0x3400` | non-PM | `0xBF206800` | MIIC port 0 |
| `0x1400` | PM | `0xBF002800` | SAR (`RIUBASE_PM_SAR` w `sboot/src/kronus/c_pm_riubase.h`) |

Uwaga na pułapkę z `i2c_analiza`: definicje typu `#define IIC0_CTRL 0x3420` pochodzą z nagłówków innych chipów. Do adresu `0xBF203420` przepisano je bez mnożenia ×2, a poprawnie trzeba liczyć `0xBF200000 + 0x3420*2`.

## Zapisy z U-Boota

- `mw.w` / `md.w` zapisują i czytają 16 bitów; to zalecana forma dla rejestrów RIU.
- Adres musi być wyrównany do rozmiaru dostępu. `mw BF206902 34` (32 bity pod nieparzystym słowem) dało wyjątek `AdES` ✅.
