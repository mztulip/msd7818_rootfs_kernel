# Pilot IR

## Jak to działa w fabrycznym oprogramowaniu (🔍)

```
odbiornik IR → sprzętowy dekoder IR (PM, bank 0x3D00) ← konfiguruje i odczytuje: PM51 (8051, firmware z flasha)
                                                                        │ skrzynka pocztowa MBX
                                                                        ▼
                                                        MIPS: MDrv_IR_* (tylko polecenia, bez rejestrów)
```

- **Po stronie MIPS** (`libdrvIR.a` z BSP K1, `drvIR.o`) sterownik IR **nie dotyka rejestrów**. Wysyła tylko polecenia przez skrzynkę pocztową do PM51 (`MDrv_MBX_SendMsg` / `RecvMsg`, polecenia `E_IR_CPUTo51_CMD_INIT/CONFIG/KEYCODE/…` w `bsp/k1/include/drvIR.h`). Kody klawiszy przychodzą z powrotem przez MBX (`_IR_MailBoxHandler`). Te same funkcje są w zrzucie U-Boota (`MDrv_IR_Init` `0x87688718`, `MDrv_IR_Config` `0x87688580`).
- **PM51** to drugi 8051 w chipie (domena zasilania/czuwania), osobny od 8051 w demodulatorze. Obsługuje IR, przyciski SAR, RTC, wybudzanie i panel przedni.
- Obsługiwane formaty (`drvIR.h`): `IR_DEC_FULL` (sprzętowe pełne dekodowanie NEC-podobnych kodów), `IR_DEC_RAW`, `IR_DEC_SW` (programowe, z czasów impulsów), `IR_DEC_RC5`/`RC5X`/`RC6`.
- Odczyt przycisków SAR też idzie w fabrycznym firmware przez PM51 (`[SAR] [MCU51]…`). Sprzęt SAR można jednak czytać bezpośrednio, patrz [przyciski_sar.md](przyciski_sar.md).

## Firmware PM51

| | |
|---|---|
| Położenie | SPI flash `0x10000`, przed nagłówkiem MBoota (`0x20000`) |
| Rozmiar | ok. 45–57 KB (stringi do `0x1B105`, niezerowe dane do ok. `0x1E0FB`) |
| Plik | `binaries/pm51/pm51_fw_raw64k.bin` (64 KB od `0x10000`), analiza Ghidry w `binaries/pm51/ghidra_out/` |
| Wektory | `LJMP 0x6D00` (reset), `LJMP 0xA545` (INT0), … |
| Moduły (stringi) | `MSVC00IR01…` (IR), `MSVC00SR01…` (SAR), `HAL_PM_*` (wybudzanie: IR, GPIO, UART1, CEC, sync), `_MDrv_PM_PowerDown2Standby/Sleep/DeepSleep`, `E_MBX_CLASS_IRKEY_NOWAIT`, `E_MBX_CLASS_SAR_NOWAIT` |
| Panel przedni | `s_pFPInterfaceCfg.m_clkPinId/m_dataPinId/m_stbPinId/m_powerKeyId/m_bEnableShowTime` + `__GetCharCode`: obsługa 3-przewodowego sterownika panelu (CLK/DATA/STB, typu TM1628/FD628) z wyświetlaczem 7-seg. i klawiszami. Czy ten tuner ma taki panel, trzeba sprawdzić na płycie ❓ |

### Start PM51 (🔍)

- Po włączeniu zasilania PM51 wykonuje kod z początku flasha: `02 0B 00` = `LJMP 0x0B00` (zgodne z `sboot/src/kronus/boot.S`, „8051 instruction: jump 0xb00”). Kod pod `0x0B00` przełącza rejestry `XDATA 0x1E03` i `0x0E53`, czyli **oddaje start MIPS-owi**, a potem kręci się w `SJMP $`.
- Uruchomienie właściwego firmware'u z flasha (`0x10000`) robi `MDrv_PM_SetSPIOffsetForMCU` / `HAL_PM_SetSPIOffsetForMCU` (`libdrvPM.a`). Zapisuje ona rejestry PM `0x0E53`, `0x0E24`, `0x0E40`/`0x0E41`, `0x1018` i `0x2BA0`. Dwa ostatnie ustawia też `boot.S`. `HAL_PM_Disable51` czyści bit 4 w `0x0E53`, więc ten rejestr to włącznik PM51.
- MBoot tej płyty linkuje z `libdrvPM.a` tylko `MDrv_PM_Init`, `MDrv_PM_PowerDown` i RTC (dopasowania w zrzucie RAM). To ścieżka przejścia w czuwanie, w której PM51 czeka na klawisz IR, żeby wybudzić płytę. Uruchomienia PM51 do normalnej pracy w MBoocie nie znaleziono.
- **Wniosek ❓:** pod Linuksem uruchomionym z MBoota PM51 prawdopodobnie **nie działa**, więc nikt nie obsługuje dekodera IR.

### Adresowanie

PM51 widzi rejestry PM RIU w XDATA pod ich numerem bajtowym (np. `0x0E53`, `0x3D80`). Rejestry podaje do funkcji jako 32-bitowy adres w `R4:R5:R6:R7` (`MOV R7,#lo; MOV R6,#0x3D; MOV R5,#0; MOV R4,#0`), dlatego skrypt `Export8051.java` ich nie wyłapuje. Trzeba ich szukać po wzorcu bajtów `7F xx 7E 3D`.

Funkcje pomocnicze w firmware PM51: `0x738F` = zapis 16 bitów (wartość z `XDATA 0x42B9`), `0x8FDE` = zapis bajtu, `0x7E14` = odczyt bajtu.

## Sprzętowy dekoder IR: rejestry

Bank PM `0x3D00`–`0x3DFF` (`RIUBASE_IR` w `sboot/src/kronus/c_pm_riubase.h`) → **CPU `0xBF007A00`–`0xBF007BFF`** (wzór z [riu.md](riu.md): `0xBF000000 + reg*2`).

Rejestry używane przez firmware PM51 (🔍, znaczenie pól ❓):

| Reg | CPU | Użycie w PM51 |
|---|---|---|
| `0x3D01`, `0x3D05`, `0x3D07` | `0xBF007A01`, `…7A09`, `…7A0D` | sterowanie (grupa `0x3D0x`) |
| `0x3D80`/`0x3D81` | `0xBF007B00`/`…7B01` | sterowanie (grupa `0x3D8x`) |
| `0x3D82`–`0x3D98` (co 2) | `0xBF007B04`–`0xBF007B30` | ciąg 12 zapisów 16-bitowych przy inicjalizacji (`0x9764`–`0x9892`); wyglądają na granice czasów (nagłówek, bity 0/1, powtórzenie), por. `IR_TimeBnd` w `drvIR.h` |
| `0x3D9E`, `0x3DA0`/`0x3DA1` | `0xBF007B3C`, `…7B40`/`…7B41` | konfiguracja |
| `0x3DA3`, `0x3DA4` | `0xBF007B45`, `…7B48` | |
| `0x3DA7` | `0xBF007B4D` | odczyt, maska `0x30` (status / FIFO?) |
| `0x3DA8`, `0x3DA9` | `0xBF007B50`, `…7B51` | dane (kod klawisza?) |
| `0x3DAD` | `0xBF007B59` | odczyt, testowane bity 0, 1, 4 (flagi) |
| `0x3DB0` | `0xBF007B60` | |

## Możliwe drogi pod Linuksem

| Droga | Opis | Trudność |
|---|---|---|
| **A. Bezpośredni sterownik dekodera** | odtworzyć inicjalizację IR z firmware'u PM51 (funkcje wokół `0x9764`, `0xA989`, `0xAB53`), potem odczyt kodu i flag z `0x3DA7`–`0x3DAD`; w Linuksie sterownik `rc-core` | średnia |
| **B. Uruchomić PM51 i rozmawiać przez MBX** | sekwencja z `HAL_PM_SetSPIOffsetForMCU`, protokół z `libdrvMBX.a` + `drvIR.o` (oba z symbolami) | średnia–wysoka; PM51 i tak przejmuje IR, SAR i panel |
| **C. Pin odbiornika IR jako GPIO** | jeśli pin odbiornika da się przełączyć na GPIO z przerwaniem, wystarczy standardowy `gpio-ir-recv` w kernelu | niska, ale niewiadomo, czy pin ma przerwania ❓ |

## Droga B szczegółowo: fabryczny firmware PM51 + skrzynka MBX (🔍)

Wszystko poniżej pochodzi z deasemblacji `libdrvPM.a`, `libdrvMBX.a` i `libdrvIR.a` (BSP K1) oraz z firmware'u PM51. Nic nie było testowane na płycie.

### 1. Uruchomienie PM51 (`HAL_PM_SetSPIOffsetForMCU`)

Rejestry PM (baza `0xBF000000`, adres CPU wg [riu.md](riu.md)):

| Krok | Rejestr | CPU | Operacja |
|---|---|---|---|
| 1 | `0x0E53` | `0xBF001CA5` | `&= ~0x01`, potem `&= ~0x10` (zatrzymanie i reset PM51) |
| 2 | `0x0E24` | `0xBF001C48` | `= 0` (wyzerowanie flagi gotowości) |
| 3 | `0x0E41` bit 6, `0x0E40` bit 7 | `0xBF001C81`, `0xBF001C80` | `= 0` |
| 4 | `0x1018` bit 3 | `0xBF002030` | `= 0` |
| 5 | `0x2BA0` bit 0 | `0xBF005740` | `= 1` |
| 6 | `0x1018` bity 0, 2 → 0; bit 1 → 1 | `0xBF002030` | |
| 7 | `0x1014`, `0x1010`, `0x1016` (16 bit) | `0xBF002028`, `…2020`, `…202C` | `= 0` |
| 8 | `0x1012` (16 bit) | `0xBF002024` | `= 0x00FF` |
| 9 | `0x10F9` | `0xBF0021F1` | `= offset` (argument funkcji; dla firmware'u pod `0x10000` prawdopodobnie `1`, czyli jednostki 64 KB ❓) |
| 10 | `0x10F8` | `0xBF0021F0` | `= 1` |
| 11 | `0x0E54` (16 bit) | `0xBF001CA8` | `= 0x829F` |
| 12 | `0x0E53` | `0xBF001CA5` | `\|= 0x01`, odczekać 120 µs, `\|= 0x10` (start) |
| 13 | `0x0E24` (16 bit) | `0xBF001C48` | **czekać, aż PM51 wpisze `2`** (firmware PM51 odwołuje się do `0x0E24`) |

Kroki 4–8 to najpewniej mapowanie okna SPI flash dla kodu 8051. Te same rejestry (`0x1018`, `0x2BA0`) ustawia `sboot/src/kronus/boot.S`.

### 2. Sprzętowa skrzynka MBX (MIPS ↔ PM51)

Bank non-PM `0x1033xx`. Rejestry grup leżą pod `(0x19C0 + id_grupy) × 4` od `0xBF200000`, gdzie `id_grupy` pochodzi z tablicy `_u16MbxGroupIdMBXHAL[nadawca][odbiorca]`:

| Kierunek | Grupa | RIU | CPU |
|---|---|---|---|
| MIPS (HK) → PM51 | `0x10` | `0x1033A0`–`0x1033AF` | `0xBF206740`–`0xBF20675F` |
| PM51 → MIPS (HK) | `0x18` | `0x1033B0`–`0x1033BF` | `0xBF206760`–`0xBF20677F` |
| (CP ↔ HK, nieużywane) | `0x08` | `0x103390` | `0xBF206720` |

Firmware PM51 odwołuje się do `0x103390/9E`, `0x1033A0/AE` i `0x1033B0/BE`, co zgadza się z tą tabelą ✅🔍.

Układ grupy (16 bajtów RIU = 8 rejestrów 16-bit), odpowiada strukturze `MBX_Msg` z `drvMBX.h`:

| Bajt RIU | Bajt CPU | Pole |
|---|---|---|
| 0 | +0 | ctrl: bit 0 = wiadomość wysłana / zajęta, bit 2 kasowany przy wysyłce, bity 1 i 2 badane przy odbiorze (typ: normal/instant?) |
| 1 | +1 | `u8MsgClass` |
| 2 | +4 | `u8Index` (polecenie) |
| 3 | +5 | `u8ParameterCount` |
| 4–13 | +8, +9, +12, +13, … | `u8Parameters[10]` |
| 14 | +28 | `u8S0` |
| 15 | +29 | status: bit 7 = odczytane przez odbiorcę, bity 4/5/6 = potwierdzenie / błąd (ustawiane w `MHAL_MBX_RecvEnd`), bity 3:0 zerowane przy wysyłce |

Przerwanie do drugiego procesora (`MHAL_MBXINT_Fire(src, dst)`, baza `0xBF200000`), zawsze jako impuls: ustaw bit, potem skasuj:

| Kierunek | Rejestr RIU | CPU | Bit |
|---|---|---|---|
| MIPS → PM51 | `0x100540` | `0xBF200A80` | bit 2 |
| AEON → PM51 | `0x100540` | `0xBF200A80` | bit 0 |
| PM51 → MIPS | `0x100548` | `0xBF200A90` | bit 0 (z tego rejestru korzysta też firmware PM51) |
| MIPS → AEON | `0x100544` | `0xBF200A88` | bit 2 (PM51 → AEON: bit 0) |

### 3. Wiadomości IR (`drvIR.o`)

Identyfikatory: `E_MBX_CPU_PM = 0`, `E_MBX_CPU_MIPS = 2`, `E_MBX_ROLE_PM = 2`, klasy `E_MBX_CLASS_IRKEY = 34` i `E_MBX_CLASS_IRKEY_NOWAIT = 49`.

| Kierunek | Klasa | Indeks | Parametry | Znaczenie |
|---|---|---|---|---|
| MIPS → PM51 | 49 | `0` (`CMD_INIT`) | 4 bajty: **adres fizyczny struktury konfiguracji IR w DRAM** | `MDrv_IR_Init`; PM51 sam czyta strukturę z RAM |
| MIPS → PM51 | 49 | `1` (`CMD_CONFIG`) | | `MDrv_IR_Config` |
| MIPS → PM51 | 49 | `2` (`CMD_KEYCODE`) | 0 | `MDrv_IR_GetKeyCode`: pytanie o ostatni klawisz |
| PM51 → MIPS | 34 | (odpowiedź) | `[0]` = kod, `[1]` = powtórzenie | odpowiedź na polecenia (timeout 5000 ms, wynik `13` = timeout) |
| PM51 → MIPS | 49 | sprawdzany indeks ❓ | `[0]` = kod klawisza, `[1]` = flaga powtórzenia | **asynchroniczne zdarzenie klawisza**, `_IR_MailBoxCallbackHandler` → callback |

Pozostałe polecenia z `drvIR.h`: `3` = SETCLBK, `4` = LIBVER, `5` = STATUS, `6` = ENABLE, `7` = INFO.

### 4. Co trzeba zrobić pod Linuksem

1. Zarezerwować w DTS mały, fizycznie ciągły bufor na strukturę konfiguracji IR (`IR_RegCfg` / `IR_TimeBnd` z `drvIR.h`), bo PM51 czyta ją z DRAM po adresie fizycznym.
2. Uruchomić PM51 sekwencją z pkt 1.
3. Sterownik (jądro albo na próbę program przez `/dev/mem`):
   - wysyłka: wypełnić grupę `0xBF206740`, ustawić ctrl bit 0, impuls bitu 2 w `0xBF200A80`,
   - odbiór: przerwanie albo odpytywanie grupy `0xBF206760` (ctrl bit 0), odczytać klasę, indeks i parametry, ustawić status bit 7 / ctrl bit 0 = 0 (`MHAL_MBX_RecvEnd`),
   - po `CMD_INIT` oczekiwać zdarzeń klasy 49 z kodem klawisza i zgłaszać je do `rc-core` / `input`.

### Ryzyka i niewiadome ❓

- **Wykonywanie z flasha:** PM51 wykonuje kod bezpośrednio z SPI flash, a Linux ma sterownik flasha (`&isp` w DTS). Fabryczny firmware też używa obu naraz, więc sprzęt pewnie arbitruje, ale trzeba to sprawdzić.
- **UART:** firmware PM51 ma komunikaty diagnostyczne (`[SAR] [MCU51]…`). Jeśli drukuje je na ten sam UART, będą się mieszać z konsolą Linuksa.
- **Zarządzanie zasilaniem:** PM51 steruje wybudzaniem i czuwaniem, a firmware może reagować na klawisz zasilania (`m_powerKeyId`) wyłączeniem zasilania płyty.
- Wartość argumentu `offset` i zawartość struktury konfiguracji IR (kody nagłówka/klienta pilota) trzeba potwierdzić. Firmware PM51 może też mieć własną domyślną konfigurację.
- Kolejność inicjalizacji MBX po stronie PM51 (czy oczekuje najpierw wiadomości klasy SYSTEM/PM, np. `MDrv_PM_Init`).

## Testy do zrobienia na płycie

1. W U-Boocie, naciskając przyciski pilota:
   ```
   md bf007a00 10
   md bf007b00 20
   ```
   Jeśli coś się zmienia (zwłaszcza `…7B4C`–`…7B5C`), dekoder działa nawet bez PM51.
2. Odczyt `0x0E53` (PM51 włączony?): `md bf001ca4 1`; bit 4 w bajcie pod `0xBF001CA5`.
3. Sprawdzić na płycie, czy jest panel przedni ze sterownikiem 3-przewodowym, i skąd idzie sygnał z odbiornika IR (który pin SoC).

## Odczyt na płycie (2026-10-13, Linux) ✅

- PM51 **nie działa**: `0x0E53` (bajt pod `0xBF001CA5`) = `0x00`, bity 0 i 4 wyzerowane.
- `0x0E24` (`0xBF001C48`) = `0xBABE`. To nie jest `2` (gotowość PM51), tylko „magiczna” wartość wpisana przez kod startowy.
- `0x1018` (`0xBF002030`) = `0x000A`, zgodnie z `boot.S`.

### Bank IR po starcie Linuksa (odczyt `devmem 0x1F007A00 + 4*i`, 16 bit) ✅

| Reg | Wartość | | Reg | Wartość |
|---|---|---|---|---|
| `0x3D00` | `0x001F` | | `0x3D90` | `0x01A4` |
| `0x3D02` | `0x04A0` | | `0x3D92` | `0x0578` |
| `0x3D04` | `0x00C0` | | `0x3D94` | `0x0348` |
| `0x3D06` | `0x0041` | | `0x3D96` | `0x0AF0` |
| `0x3D08` | `0x103C` | | `0x3D98` | `0x0690` |
| `0x3D0A` | `0xFFFF` | | `0x3D9E` | `0x008C` |
| `0x3D10` | `0x0001` | | `0x3DA0` | `0x9F30` |
| `0x3D14` | `0x01FF` | | `0x3DA2` | `0x0F00` |
| `0x3D80` | `0x01BF` | | `0x3DA4` | `0xFF00` |
| `0x3D82` | `0x2BF2` | | `0x3DA6` | `0x3804` |
| `0x3D84` | `0x1A5E` | | `0x3DA8` | `0x000C` |
| `0x3D86` | `0x15F9` | | `0x3DAC` | `0x0600` |
| `0x3D88` | `0x0D2F` | | `0x3DB0` | `0x0020` |
| `0x3D8A` | `0x0C35` | | `0x3DB2` | `0xFFFF` |
| `0x3D8C` | `0x0753` | | `0x3DB6`–`0x3DBA` | `0xFFFF` |
| `0x3D8E` | `0x02BC` | | pozostałe | `0x0000` |

`0x3D80`–`0x3D98` mają niezerowe, malejące wartości, które pasują do progów czasów (nagłówek, bity 0/1). Albo coś skonfigurowało dekoder przed Linuksem, albo to wartości domyślne po resecie ❓.

Przez ok. 20 s próbkowania całego banku (8 zrzutów) żaden rejestr się nie zmienił. Nie wiadomo jeszcze, czy w tym czasie były naciskane przyciski pilota.

Powtórka (2026-10-13): szybkie próbkowanie rejestrów `0x3D00`–`0x3D0A`, `0x3D10`, `0x3D14`, `0x3D9E`–`0x3DBA` przez 30 s z naciskaniem przycisków pilota. **Żadnej zmiany.** Pod Linuksem (bez PM51) dekoder IR nie rejestruje kodów, albo odczyty trafiają w rejestry, które się nie zmieniają. Możliwe przyczyny ❓: dekoder wyłączony (bity sterujące), pad odbiornika IR nieprzełączony na funkcję IR, wyłączony zegar bloku albo zbyt wolne odpytywanie (każdy `devmem` to osobny proces).
