# Audio: czy MIPS może generować dźwięk?

**Krótko:** sprzętowo tak, ale nie ma na to gotowej, krótkiej drogi. Blok audio Kronusa to rozbudowany procesor dźwięku MStara z dwoma DSP, które potrzebują własnego firmware'u. MBoot go nie inicjalizuje, a Linux nie ma dla niego sterownika. Dźwięk z MIPS wymaga odtworzenia sekwencji inicjalizacji audio z fabrycznego firmware'u. Nic z tego nie było testowane na płycie.

## Blok audio Kronusa: „Vivaldi” / MAD

- `kronus.dtsi` (w bloku `#if 0`) opisuje `vivaldi@225400` z 6 bankami `0x225400`–`0x225E00` (RIU `0x112A00`–`0x112F00`, CPU `0xBF225400`–`0xBF225FFF`) i przerwaniami `se_dsp2up` oraz `dec_dsp2up` 🔍.
- Fabryczny firmware (eCos, `extractions/wiwa_hd95_BACKUP.BIN.extracted/8001C/decompressed.bin`) zawiera pełny stos audio MStara. Widać to po stringach:
  - dwa DSP: **DEC DSP** (dekodowanie MPEG/AC3…) i **SE DSP** (efekty, miksowanie, wyjścia): `DEC DSP IDMA …`, `SE DSP IDMA …`, `*MAD Auto-Recovery DSP1*`,
  - firmware DSP ładowany przez CPU: `[HAL_AUDSP_DspLoadCode]`, `Audio DSP2 code is same(%x), no need to reload`,
  - odtwarzanie PCM z RAM: `***** Audio DMAReader Buffer empty !! *****`,
  - wyjścia: DAC (AUOUT0–3), I2S, SPDIF, HDMI (`MM_SetHDMISpdifOutput`).
- MBoot na tej płycie **nie zawiera** kodu audio. W zrzucie RAM są tylko stringi HDMI TX i video DAC, a w BSP K1 nie ma `libapiAUDIO`. Gdy startuje Linux, blok audio jest więc nieskonfigurowany 🔍.
- Sterownik `linux_msd781x/sound/soc/mstar/msc313-bach.c` dotyczy innego, prostszego bloku („BACH” z kamerowych chipów Infinity) i **nie pasuje** do Kronusa.

## Tor dźwięku (wg MStara)

```
PCM w DRAM → DMA Reader → wejście SE DSP → miksowanie/głośność → DAC (AUOUT, wyjście analogowe) / I2S / SPDIF / HDMI
                                ↑ firmware DSP ładowany przez CPU
```
Wyjście z DMA Readera przechodzi przez SE DSP. Bez uruchomionego firmware'u DSP dźwięk najpewniej nie dotrze do wyjść ❓. Nie wiadomo, czy istnieje tryb obejścia DSP.

## Wzorzec rejestrów z pokrewnego chipu (Keres) 🔍❓

Kronus nie ma biblioteki audio w BSP MBoota, ale ma ją Keres (`mstar-mboot-master/MstarCore/bsp/keres/lib/libapiAUDIO.a`, z symbolami), pokrewny chip MStara do dekoderów. Funkcje:
- `HAL_AUDIO_DmaReader_Init`, `HAL_AUDIO_DmaReader_WritePCM`: odtwarzanie PCM,
- `HAL_AUDSP_DspLoadCode`, `HAL_MAD_LoadCode`, `HAL_AUDIO_SeSystemLoadCode`: ładowanie firmware'u DSP,
- `HAL_SOUND_EnableTone`: generator tonu w DSP,
- `HAL_AUDIO_WritePreInitTable`: tabela startowa.

Rejestry DMA Readera w Keres (`HAL_AUDIO_WriteMaskReg`, adresy w banku audio `0x112Bxx`):

| Reg | CPU (jeśli Kronus ma ten sam układ) | Użycie |
|---|---|---|
| `0x112B80` | `0xBF225700` | sterowanie (start/stop, bity `0x0001`, `0x0010`, `0x0100`; wartości `0x6402`, `0x2402`) |
| `0x112B82` | `0xBF225704` | rozmiar / poziom zapisu |
| `0x112B84` | `0xBF225708` | adres bufora |
| `0x112B86` | `0xBF22570C` | `0x2000` |
| `0x112B88` | `0xBF225710` | `0xFFF4` |
| `0x112B8A` | `0xBF225714` | `0x0012` |
| `0x112B8C` | `0xBF225718` | odczyt poziomu bufora |
| `0x112CB0`, `0x112C6B`, `0x112CA4`, `0x112C20` | | wybór źródła / ścieżki |

Bufor PCM leży w obszarze pamięci DSP (`HAL_AUDIO_GetDspMadBaseAddr`). Czy Kronus ma identyczny układ, trzeba sprawdzić w fabrycznym firmware ❓.

## Możliwe drogi

| Droga | Co trzeba | Trudność |
|---|---|---|
| **Pełny sterownik audio** (DMA Reader + DSP) | wyciągnąć firmware DEC/SE DSP z `decompressed.bin`; odtworzyć inicjalizację (`MApi_AUDIO_Initialize`, tabela PreInit, ścieżki, głośność, odciszenie) na podstawie fabrycznego firmware'u i biblioteki Keres | wysoka |
| **Generator tonu DSP** (`EnableTone`) | jak wyżej (DSP musi działać) | wysoka, ale prosty test po uruchomieniu DSP |
| **HDMI audio** | źródłem jest wewnętrzne I2S z bloku audio, więc ta sama zależność | wysoka |
| **GPIO / PWM → głośnik / buzzer** | wolny pin GPIO (patrz [gpio.md](gpio.md)) i przełączanie go z MIPS | **niska**, ale nie przez gniazdo audio i z jakością „beepera” |

## Następne kroki badawcze

1. W `decompressed.bin` znaleźć funkcje po stringach (`HAL_AUDSP_DspLoadCode`, `DMAReader`) i porównać je z `halAUDSP.o` / `halAUDIO.o` z Keres. Można użyć `tools/match_libs.py` na `decompressed.bin`, choć dopasowanie między chipami będzie częściowe.
2. Ustalić, skąd fabryczny firmware bierze kod DSP (tablica w `decompressed.bin` albo osobny obszar flasha) i jaki ma rozmiar.
3. Zdobyć wzorcową konfigurację: zrzut rejestrów audio (`0xBF225400`–`0xBF225FFF`) w czasie, gdy fabryczny firmware gra dźwięk. Fabryczny firmware startuje po U-Boocie, więc trzeba by to zrobić przez JTAG albo przez konsolę diagnostyczną fabrycznego firmware'u, jeśli taka istnieje ❓.

## Analiza oryginalnego systemu (2026-10-14) 🔍

Cel: **fbDOOM z dźwiękiem przez HDMI** (mały monitor HDMI z głośnikami). Potrzebne: inicjalizacja bloku audio, firmware DSP (DEC + SE), odtwarzanie PCM przez DMA Reader i dźwięk w nadajniku HDMI.

### Oryginalny system uruchomiony z MBoota (bez zapisu do flasha)
⚠️ **Przed uruchomieniem oryginalnego systemu wyjmij pendrive z Linuksem.** 2026-10-14, po jednym uruchomieniu oryginału, w `vmlinux.bin` na partycji FAT jeden blok 4 KB (offset `0x37F000`) zawierał starą zawartość pliku sprzed podmiany (identyczną z `vmlinux_gop.bin`). Płyta restartowała się w kółko, bo kernel był uszkodzony. Oryginał obsługuje USB (odtwarzacz, nagrywanie) i najpewniej zapisał do tablicy FAT. Naprawa: na PC skopiować `vmlinux_gopmiu.bin` na `vmlinux.bin` i sprawdzić MD5 po ponownym zamontowaniu.
```
spi_rdc 0x80B00000 0x8001C 0x289977
LzmaDec 0x80B00000 0x289977 0x80000180 0x81000000
go 0x80000224
```
- Wstaje do „przewodnika instalacji”. Bez pilota nie da się zatwierdzać (przyciski panelu: CH+/CH− przewijają, POWER usypia). Z pilotem można by puścić WAV/MP3 z pendrive'a: system ma odtwarzacz (AAC, MP3, WMA, OGG, WAV…).
- Konsola UART: znak zachęty `>>`, to konsola demo SDK MStara. Działa tylko `Help` (lista `HelpAudio`, `HelpGOP`, `HelpXC`… jest, ale tych modułów nie wkompilowano: „unknow command”).

### Dopasowanie bibliotek nie działa
`tools/match_libs.py` z `libapiHDMITX.a` (K1) i `libapiAUDIO.a` (Keres) daje 0 trafień: oryginał jest skompilowany inaczej (eCos, bez PIC). Funkcje trzeba nazywać po napisach i kolejności wywołań.

### Projekt Ghidry
`binaries/ghidra_stock/` (`stock.gpr`): `decompressed.bin`, MIPS LE 32, baza `0x80000180`, pełna auto-analiza (ok. 15 min). Skrypty:
- `tools/ghidra/ExportByStrings.java`: dekompiluje funkcje odwołujące się do napisów pasujących do wyrażeń; wynik `binaries/ghidra_stock/strings_funcs.c` i `strings_refs.txt` (97 funkcji audio/HDMI),
- `tools/ghidra/ExportCallTree.java`: dekompiluje funkcje razem z wołanymi (do głębokości N), wynik `binaries/ghidra_stock/audio_init_calltree.c`.
```sh
JAVA_HOME=/usr/lib/jvm/java-27-openjdk /opt/ghidra/support/analyzeHeadless binaries/ghidra_stock stock \
  -process decompressed.bin -noanalysis -scriptPath tools/ghidra \
  -postScript ExportCallTree.java out.c 3 80005b80
```

### Kluczowe funkcje (adresy w oryginale)

| Adres | Funkcja (wg napisów / kolejności wywołań jak w Keres `MApi_AUDIO_Initialize`) |
|---|---|
| `0x80005b80` | `MApi_AUDIO_Initialize` |
| `0x800085e0` | `MDrv_AUDIO_SHM_Init` |
| `0x800086a0` | `MDrv_AUDIO_GET_INIT_FLAG` |
| `0x800f38f8` | `MsOS_DelayTask` |
| `0x800087c8` | `MDrv_AUDIO_Init` |
| `0x80008758` | `MDrv_AUDIO_WriteInitTable` |
| `0x800087e4` | `MDrv_AUDIO_SetPowerOn` |
| `0x800088a8` | `MDrv_AUDIO_TriggerSifPLL` |
| `0x8000989c` | `MDrv_MAD_SetMemInfo` (`HAL_MAD_SetMemInfo[DSP_DEC]` w `0x80010280`) |
| `0x8000a4a4` | `MDrv_MAD2_SetMemInfo` (`HAL_MAD2_SetMemInfo[DSP_SE]` w `0x800138e0`) |
| `0x8000a610` / `0x8000ae0c` | `MDrv_SIF_Init` / `MDrv_SOUND_Init` |
| `0x800be2cc` | `HAL_AUDSP_DspLoadCode` (wybór i ładowanie firmware DSP) |
| `0x800be010` | weryfikacja kodu DSP („Dsp code verify error”) |
| `0x8000ff94`, `0x800100fc` | IDMA DEC DSP (odczyt/zapis pamięci DSP) |
| `0x80013b4c`, `0x80013cb8` | IDMA SE DSP |
| `0x8000f018` | DMA Reader (odtwarzanie PCM; „Audio DMAReader Buffer empty”) |
| `0x8000cd58` | `HAL_AUDIO_AllocateVars` |
| `0x80064a70`, `0x80064b4c` | HDMI TX: częstotliwość / kanały / typ audio |
| `0x8019ef98` | `MM_SetHDMISpdifOutput` |

Keres używa innego procesora audio (R2, `.data.mst_codec_r2` 118 KB), więc jego firmware nie pasuje. Firmware DSP Kronusa trzeba wyciągnąć z oryginału przez tablice używane w `HAL_AUDSP_DspLoadCode` ❓.

### Dalsze kroki
1. Zejść z `MApi_AUDIO_Initialize` do funkcji HAL i zebrać sekwencję zapisów rejestrów (`0xBF22xxxx`).
2. Z `HAL_AUDSP_DspLoadCode` znaleźć tablice firmware'u DSP (adresy + długości) i wyciąć je do plików.
3. DMA Reader: rejestry i bufor PCM, format (częstotliwość, 16 bit stereo).
4. HDMI TX audio: `0x80064b4c` + `MApi_HDMITx_SetAudioOnOff/Frequency/Configuration` (`libapiHDMITX.a` K1 ma symbole) → włączyć audio w nadajniku.
5. Prototyp w przestrzeni użytkownika (`/dev/mem`): inicjalizacja + DSP + sinus 1 kHz przez DMA Reader → HDMI. Potem sterownik ALSA i dźwięk w fbDOOM.

### Tablica inicjalizacji audio ✅ (wyciągnięta)
`MDrv_AUDIO_WriteInitTable` (`0x80008758` → `0x8000d110`) przechodzi tablicę pod **`0x80371030`**. Wpis ma 8 B: `u32 reg`, `u8 maska`, `u8 wartość`. Prefiks `0xA` w rejestrze oznacza bank `0x00xxxx` (PM), `0xB` bank `0x10xxxx`, a bez prefiksu bank `0x11xxxx` (audio). `0xFFFF` z maską 1 to opóźnienie (`wartość` ms), `0xFFFF` z maską 0 kończy tablicę. Zapis bajtu (`0x8000ced4`): `*(0xBF000000 + reg·2 − (reg&1)) = (stare & ~maska) | (wartość & maska)`, z wyjątkiem pominięcia `reg 0x2d2a` z maską 8.
Pełna lista (192 wpisy, z adresami CPU) jest w `binaries/ghidra_stock/audio_init_table.txt`.

Pozostałe funkcje HAL z tego poziomu:
- `MDrv_AUDIO_Init` → `0x8000d44c`: `0x112C00` = `0xFF`/maska 3, 2 ms, potem 0;
- `MDrv_AUDIO_SetPowerOn(1)` → `0x8000d334`: `0x112D31` bit 1 = 0, `0x112B42` = 0.

### Firmware DSP ✅ (wycięty)
`HAL_AUDSP_DspLoadCode` (`0x800be2cc`) wybiera po typie wpis `pau_info`, 13 słów:
`{cm_addr, cm_len, cm_buf, pm_addr, pm_len, pm_buf, x1_addr, x1_len, x1_buf, x2_addr, x2_len, x2_buf, nazwa}`. Długości są w słowach DSP po **3 bajty** (DSP 24-bitowy):
- `cm` i `pm` są wpisywane przez IDMA (`0x800bde84`), a potem weryfikowane (`0x800be010`); bit `0x112A7E`.0 wybiera DSP (0 = DEC, 1 = SE);
- `x1` i `x2`, jeśli są, kopiuje `memcpy` (`0x80238364`) do pamięci DSP w DRAM, pod adres `baza_DSP + adres·3` (`0x8000eb4c` zwraca bazę dla DEC albo SE).

Tablice: `0x804F7500` (DEC, typy `0x00–0x15`), `0x804F725C` (SE dekodery `0x30–0x3C`), `0x804F7020` (SE efekty `0x40–0x49`), `0x804F6FEC` (`0x50` **K1 DEC system**), `0x804F6FB8` (`0x60` **K1 SE system**), `0x804F6F84` (`0xA0`), SIF `0x804F6D7C`/`0x804F6E80` (puste).

Wycięte do `binaries/audio_dsp/` (`<typ>_<nazwa>_<segment>.bin`, opis w `index.json`), 26 zestawów, m.in.:

| Typ | Nazwa | Segmenty (adres DSP, bajty) |
|---|---|---|
| `0x60` | K1 SE system | cm `0x0` 16218, pm `0xBFF` 20349, x1 `0x8000` 26280, x2 `0x10000` 2304 |
| `0x50` | K1 DEC system | cm `0x0` 2340, pm `0xFF0` 72, x1 `0x8000` 13689 |
| `0x0B` | xpcm (DEC) | cm `0x200` 20376, pm `0xFFF` 2007 |
| `0x3A` | XPCM2 (SE) | cm `0x780` 8037, pm `0x161F` 2016, x1 `0xA000` 6561 |
| `0x03`/`0x07` | mpeg / mp3 (DEC) | … |

Uwaga: po pierwszym załadowaniu `0x50`/`0x60` kod nadpisuje wpis innymi długościami/wskaźnikami (`0x2F4`/`0x8063DE32`, `0x1506`/`0x8055690A`), czyli to wersje do ponownego ładowania ❓. Pełna tabela: `binaries/ghidra_stock/dsp_pau_tables.txt`.

### Ładowanie kodu DSP (IDMA) i pamięć DSP w DRAM ✅ (z kodu)
- **IDMA** (`0x800bde84`, wpis słowa 24-bit): wybór DSP `0x112A7E`.0. Dla DEC: sterowanie `0x2A40` = 0, adres `0x2A04` (16 bit, adres w pamięci DSP), dane: `0x2A02` ← bajt 1, `0x2A03` ← bajt 2, `0x2A02` ← bajt 0, `0x2A03` ← 0, potem czekanie na gotowość (`0x800bde18`/`0x800bddb4`). Dla SE ten sam układ pod `0x2AC0`, `0x2A84`, `0x2A82`, `0x2A83`.
- **Baza pamięci DSP w DRAM** (`HAL_AUDIO_SetDspBaseAddr` `0x8000eb20`, tablice `0x808B9D1C` = baza, `0x808B9D28` = drugi parametr). Aplikacja (`0x8013da28`) ustawia:
  - DEC (indeks 0): **`0x03B00000`**,
  - SE (indeks 1): **`0x03A00000`**.

  W Linuksie ten obszar (ok. `0x3A00000–0x3C00000`) trzeba zarezerwować w DTS. Teraz należy do Linuksa; za nim jest `tsukasa@3c00000`.
- **`HAL_MAD_SetMemInfo` (DEC)** (`0x80010280`): `0x2A22`.7 i `0x2A40`.0 = bit 27 bazy; `0x2A10` = `0x2A24` = `(baza >> 11) & 0xFFFF`; okna pamięci przez `0x2A2A` (indeks 2…5), `0x2A26` (początek: `0`, `0x20`, `0x130`, `0x354`), `0x2A28` (górny bajt: `0x1F`, `0x0F`, `0xD7`, `0x0F`); na końcu impuls bitów 6 i 7 w `0x2A20`. Dla SE: `HAL_MAD2_SetMemInfo` (`0x800138e0`), do odczytania ❓.

### DMA Reader: zapis PCM ✅ (z kodu, `0x8000f018` = `HAL_AUDIO_DmaReader_WritePCM(buf, bajty)`)
- **Poziom bufora:** `0x112B80` |= `0x10`, odczyt 16 bit `0x112B8C`, potem `0x112B80` &= ~`0x10`. Wartość w jednostkach 8 B.
- **Niedobór** (poziom < 7): `0x2B82` = 0, `0x2B80` = `0x6400` (z maską `0xFFFE`, bit 0 bez zmian), 1 ms, `0x2B80` = `0x2400`, wskaźnik zapisu wraca na początek bufora, komunikat „Audio DMAReader Buffer empty”.
- **Zapis**, gdy poziom < `(0x20000 − 2·bajty) / 8`: bufor pierścieniowy w DRAM **128 KB** (początek `*0x806FC6B4`, koniec `*0x806FC6AC`, wskaźnik `*0x806FC6B0`). Każda próbka 16 bit (LE) trafia do słowa 4 B: `[0x00, lo, hi, 0x00]` (próbka w bitach 8–23).
- **Zgłoszenie danych:** `0x2B82` = `(bajty >> 2) & 0xFFFF`, czyli liczba dopisanych słów 4 B (2 B próbki → 4 B w buforze).
- Jeśli był reset przy włączonym odtwarzaniu: `0x2B80` bit 0 = 1, 1 ms, bit 8 = 1 (start).
- Do ustalenia: inicjalizacja (`HAL_AUDIO_DmaReader_Init`: adres bufora `0x2B84`, częstotliwość, ścieżka do SE DSP) ❓.

### Mapa pamięci oryginalnego systemu (log UART przy starcie) ✅
Pełny log: `binaries/ghidra_stock/stock_boot_uart.log`.

| Obszar | Początek | Koniec | Rozmiar |
|---|---|---|---|
| XC (skaler) | `0x03D00000` | `0x04000000` | 3 MB |
| **MAD (audio, DSP DEC + SE)** | **`0x03A00000`** | **`0x03D00000`** | **3 MB** |
| MVD (dekoder wideo) | `0x01D00000` | `0x03A00000` | 29 MB |
| VE | `0x01B60000` | `0x01D00000` | |
| GOP DMA | `0x01B5F000` | `0x01B60000` | |
| PM51 | `0x01B5D800` | `0x01B5E000` | |
| GOP GWIN | `0x016CE800` | `0x01B59800` | |
| pula eCos | `0x00000000` | `0x016CE800` | |

Log potwierdza: `HAL_MAD_SetMemInfo[DSP_DEC] = 0x03b00000`, `HAL_MAD2_SetMemInfo[DSP_SE] = 0x03a00000`. Dla Linuksa trzeba więc zarezerwować `0x3A00000–0x3D00000` (obecnie `tsukasa@3c00000` pokrywa tylko część).

### Pierwsza próba tonu (`tools/board/audiotest.c`, 2026-10-14) ❌ cisza
Kroki: tablica init (192 wpisy), `Init`, `PowerOn`, DMA Reader 48 kHz (bufor `0x3C30000`, `0x2B84` = `0x7860`), audio w HDMI TX (`0x1133` reg 5 = `0x10E6`, ACR `0x0005`, AIF `0x7005`), sinus 1 kHz przez 8 s. DMA Reader **zużywa dane we właściwym tempie** (poziom 1017–15878 × 8 B), ale z monitora HDMI nic nie słychać. Nie wiadomo jeszcze, czy ten monitor w ogóle gra dźwięk z tej płyty (test z oryginałem w toku). Możliwe przyczyny: bez uruchomionego DSP SE dane nie trafiają na wyjście, brak konfiguracji I2S do HDMI TX, inna trasa niż dla „PCM do wyjścia” ❓.

### Stan na 2026-10-14 (wieczór)
- `audiotest` (tablica init + `Init` + `PowerOn` + DMA Reader 48 kHz + HDMI audio on): DMA Reader **pobiera dane we właściwym tempie**, ale **cisza na HDMI (monitor) i na SCART (CRT)**. Samo uproszczone init nie wystarcza. Najpewniej brakuje uruchomionego SE DSP (miksowanie i trasy wyjść) i/lub odciszenia/głośności (`MDrv_SOUND_Init`, `SetOutConnectivity`).
- Telewizja: w Polsce jest tylko DVB-T2, więc oryginał nie znajduje kanałów i nie da się nim potwierdzić dźwięku przez HDMI na kanale. Zostaje odtwarzacz multimediów z innego pendrive'a.
- `MApi_AUDIO_Initialize` woła ok. 50 funkcji (pierwszy poziom w `/tmp`, do odtworzenia z `binaries/ghidra_stock/all.c`). Wrappery: `0x800088a8` → `0x80014464` (TriggerSifPLL), `0x8000989c` → `0x80010280` (MAD SetMemInfo), `0x8000a4a4` → `0x800138e0` (MAD2 SetMemInfo), `0x8000a610` → `0x8001445c` (SIF Init), `0x8000ae0c` → `0x8001580c` (SOUND Init), `0x80009880` → `0x8001375c` (MAD Init?), `0x80008870` → `0x8000fb4c` (DecoderLoadCode?), `0x800044fc(n)` = `SetDecCmd`/tryby (`0x80009db8`, `0x8000a4c0`).

### Następny krok: wzorcowy zrzut rejestrów z działającego oryginału
Plan: z MBoota załadować oryginał do RAM (`spi_rdc` + `LzmaDec`), **przed `go`** nadpisać w RAM funkcję wypisującą pomoc konsoli demo (`0x80142468`, „press "HelpAudio"…”) małym kodem MIPS, który przez `printf` oryginału wypisze rejestry audio (`0x1F225400–0x1F225FFF`, czyli CPU `0xBF225400…`) i HDMI TX (`0xBF226200…`, `0xBF226600…`). Potem `go 0x80000224` i w konsoli `>>` polecenie `Help`. Flash nietknięty. **Pendrive z Linuksem wyjęty.** Porównanie ze stanem po `audiotest` pokaże brakujące ustawienia.

### Oryginał gra MP3 z pendrive'a: dźwięk na HDMI i SCART jednocześnie ✅
Odtwarzacz multimediów oryginału (pilot z telefonu) gra `zniwa_1.mp3` **jednocześnie przez HDMI (monitor) i SCART (CRT)**. Sprzęt i monitor są w porządku. Logi UART: `binaries/ghidra_stock/stock_uart_mp3_session.log`, `stock_uart_mp3_start.log` (montowanie FAT `/Usb1/C`, `HAL_AUDIO_AllocateVars`, `MM_SetHDMISpdifOutput sysspdif 1`, sprawdzenie licencji „Hash-key Support DD / DD+ / HE-AAC”, „This is Demo Mode!!”).

### Zrzuty rejestrów z działającego oryginału ✅ (`tools/baremetal/stockdump`)
Łatka w RAM: funkcja pomocy konsoli demo (`0x80142468`, 204 B) zastąpiona kodem, który przez `printf` oryginału (`0x80244f74`) wypisuje zakresy rejestrów: audio `0xBF225400–0xBF225FFF` i HDMI TX `0xBF226200–0xBF2263FF`, `0xBF226600–0xBF2267FF`. Koniec to linia `00000000:`. Kod, zakresy i napisy są w tych 192 B. Pierwsza wersja trzymała dane w miejscu napisów pomocy (`0x8037F074`), ale odtwarzacz ten obszar nadpisuje. W skrypcie linkera `SUBALIGN(4)`, bo inaczej `ld` wyrównuje sekcję do 16 B i przesuwa kod.
```
(MBoot) spi_rdc 0x80B00000 0x8001C 0x289977
        LzmaDec 0x80B00000 0x289977 0x80000180 0x81000000
        <mw.l z tools/baremetal/stockdump/mw_cmds.txt>      # adresy 0xA0..., czyli zapis bez cache
        go 0x80000224
(>>)    Help
```
Zrzuty:
- `binaries/ghidra_stock/stock_regs_after_boot.txt`: po starcie (także 0x110C, clkgen 0x100B, 0x1033, PM 0x34; pierwsza wersja łatki);
- `binaries/ghidra_stock/stock_regs_playing_mp3.txt`: **podczas grania MP3**.

Różnice start → granie (71 rejestrów), najważniejsze:
- **HDMI TX:** wartości podczas grania pokrywają się z tym, co ustawia `audiotest` (`0x11330A` = `0x10E6`, `0x113128` = `0x7005`, `0x113110` = `0x0005`, N `0x11310E` = `0x1800`, `0x113122` = 1, `0x113304` = `0x20`, `0x113306` = 2). Dodatkowo `0x11330C` = `0x2100` (po starcie `0x1002`), `0x11310A` = `0x220A`, `0x11310C` = 1, `0x113120` = `0x6305`.
- **Audio:** zmienia się głównie sterowanie i skrzynki DSP: `0x112A04` (`0x0FF2`→`0x0FF9`), `0x112A08`, `0x112A0C`, `0x112A0E`, `0x112A16`, `0x112A2C`, `0x112C38–0x112C3E`, `0x112D00`/`0x0A`/`0x0E` (bit 15 kasowany), `0x112D60–0x112DBE`, `0x112F04–0x112FFE`. Czyli **dźwięk idzie przez uruchomione DSP**, a samo DMA Reader + trasa nie wystarcza (zgodne z ciszą w `audiotest`).

Następny krok: zrzut tych samych zakresów w Linuksie po `audiotest` (`tools/board/riudump.c`, ten sam format) i porównanie. Potem wgranie i uruchomienie DSP (SE system + DEC system) według `MApi_AUDIO_Initialize`.

### Porównanie: oryginał (MP3) vs Linux po `audiotest` (`riudump`)
Pełna lista: `binaries/ghidra_stock/diff_stockplay_vs_linux_audiotest.txt` (105 różnic). Wnioski:
- Oryginał przy MP3 **nie używa DMA Readera** (`0x2B80–0x2B8C` = 0). MP3 dekoduje DSP DEC, a SE miksuje i wysyła na wyjścia.
- W Linuksie **pamięć obu DSP jest nieustawiona**: oryginał ma `0x112A10` = `0x2A24` = `0x7600` (baza DEC `0x3B00000` >> 11), okna `0x2A26`/`0x2A28`/`0x2A2A`, a dla SE `0x112A90` = `0x2AA4` = `0x7400` (`0x3A00000` >> 11), okna `0x2AA6`/`0x2AA8`/`0x2AAA`.
- `0x112Dxx` (skrzynki CPU↔DSP) i `0x112Fxx` są w oryginale wypełnione (pracujące DSP), w Linuksie puste.
- Trasa: oryginał `0x2C62/63` = `0x8780`, `0x2C6A/6B` = `0x0700`; `audiotest` (trasa DMA Readera) ma `0xC680`/`0x8300`.

### Sekwencja startu DSP (z dekompilacji) 🔍
- **`MAD_SetMemInfo` (DEC)** `0x80010280`: opisane wyżej.
- **`MAD2_SetMemInfo` (SE)** `0x800138e0`: `0x2AA0` &= ~`0xFF`; `0x2AA2`.7 i `0x2AC0`.0 = bit 27 bazy; `0x2A90` = `0x2AA4` = `(baza >> 11) & 0xFFFF`; `0x2AA0` = 0; okna: `0x2AAA` = 2 → `0x2AA6` = `0x150`, `0x2AA8` hi = `0x0F`; `0x2AAA` = 4 → `0x2AA6` = 0, `0x2AA8` hi = `0x2F`; na końcu (`0x80013884`) `0x2AA0` bit 6 = 1, bit 7 = 1, 1 ms, bit 7 = 0, bit 6 = 0.
- **`MDrv_SOUND_Init`** `0x8001580c`: `0x800157b0()`, `DspLoadCode(0x31 mpeg_ad → SE)`, potem **start SE** (`0x8000faac`): `0x2A80` = 3 (reset), `DspLoadCode(0x60 K1 SE system)`, czekanie (100 × 2 ms) aż `0x112DFC` == `0xE3` („DSP2 alive”, inaczej „DSP2 Re-Active”), potem `0x2DDC` hi = `0xF3`.
- **`MDrv_MAD_Init`** `0x8001375c`: `0x2A00` = 2, 2 ms, `0x2A00` = 3, `0x800136e4(3)` = `DspLoadCode(3 mpeg → DEC)` + `0x8000dc1c(1)`.
- **`MDrv_AUDIO_DecoderLoadCode`** `0x8000fb4c` (start DEC): `0x2A00` |= 3, `DspLoadCode(0x50 K1 DEC system)`, czekanie aż `0x112DBD` == `0xE3` („DSP1 alive”), `0x80014bdc(0x44, 1, 0)`, `0x2D9C` hi = `0xF3`.
- Skrzynki: DEC odpowiada w `0x112DBD`, polecenie w `0x2D9C` (górny bajt); SE odpowiada w `0x112DFC`, polecenie w `0x2DDC` (górny bajt). `0x80014bdc(cmd, val, x)` to ogólna funkcja ustawień (`0x2DC0`, `0x2DC2`, `0x2DCA`, `0x2D28`, …).

### DSP pod Linuksem: oba żyją, ale dalej cisza (`tools/board/audiotest2.c`)
- `audiotest2` odtwarza `MApi_AUDIO_Initialize` w części DSP: `SetMemInfo` DEC i SE (bazy przeniesione do `tsukasa`: SE `0x3C00000`, DEC `0x3D00000`), `SOUND_Init` (reset SE, `K1 SE system`), `MAD_Init`, `DecoderLoadCode` (`K1 DEC system`). Firmware jest w `tools/board/dsp_fw.h` (generowany; **długości w bajtach**). Kolejność ładowania systemu: `pm`, potem `cm` od słowa 8 (`cm + 0x18`), `x2`/`x1` przez `memcpy` do DRAM (baza + adres·3), na koniec słowa 1–7 i **słowo 0 (wektor startowy)**.
- Wynik: **SE i DEC odpowiadają `0xE3` („alive”)** w `0x112DFC` / `0x112DBD`. Ton przez DMA Reader: dalej cisza na HDMI i SCART, także po ustawieniu `0x2C82` = `0x30`, `0x2C86` = `0x40`, `0x2CC8` = `0x38E0`, pakietów HDMI jak w oryginale i tras `0x2C62` = `0x8780`, `0x2C6A` = `0x0700`.
- Uwaga: `binaries/audio_dsp/*.bin` (pierwszy wycinek) mają długości liczone ×3, czyli za długie. Właściwe długości (w bajtach) są w `dsp_fw.h` i w tablicach `pau_info`.

### Oryginał gra WAV (`test1k.wav`, 1 kHz, 48 kHz stereo) ✅ — **bez DMA Readera**
Zrzut: `binaries/ghidra_stock/stock_regs_playing_wav.txt`. Rejestry DMA Readera `0x112B80–0x2B8E` = 0, tak samo jak przy MP3. **PCM z WAV idzie przez DSP DEC** (dekoder `xpcm`, typ `0x0B`), który czyta dane z bufora strumienia w pamięci DSP. Zmieniają się `0x112A08` (`0x0200`), `0x112A0C` (`0x0A00`), `0x112A0E` (`0x000F`), `0x112A16` (`0x8770`): prawdopodobnie wskaźniki/poziomy bufora ES ❓. Ustawienia wyjść (`0x2C62`, `0x2C6A`, `0x2C82`, `0x2C86`, `0x2CC8`, HDMI) są takie same jak przy MP3. W firmware jest komunikat „MAD only support 2ch,48kHz,16bits” (LPCM), czyli dekoder PCM przyjmuje 16 bit, 2 kanały, 48 kHz.

**Wniosek:** droga „DMA Reader → wyjście” nie jest używana przez producenta. Do dźwięku z Linuksa trzeba odtworzyć ścieżkę odtwarzacza: DEC z dekoderem `xpcm`, bufor ES w DRAM DSP, polecenia startu w skrzynce DEC (`0x2D9C…`), wskaźnik zapisu. Następny krok: znaleźć w `all.c` kod odtwarzacza multimediów dla WAV/LPCM (np. przez „LPCM Audio data header”, `E_AUDIOSTREAM_LPCM`, `MApi_AUDIO_SetCommAudioInfo`, `MDrv_MAD_SetDecCmd`) i zrzucić też pamięć DSP podczas grania WAV, żeby zobaczyć bufor.

### Sonda pamięci podczas WAV (`tools/baremetal/stockprobe`) ✅
Większy program (C, 1 KB) ładowany przez MBoot pod `0xA3800000` (pamięć MVD, wolna przy samym audio), a w funkcji pomocy tylko skok (`lui t9; ori t9; jr t9`). `Help` wypisuje rejestry i przeszukuje DRAM w poszukiwaniu próbek `test1k.wav` (1 kHz, 48 kHz, 16 bit stereo, amplituda 12000, próbka 1 = 1566 = `0x061E`). Skan czyta **przez cache (KSEG0)**: przy odczycie bez cache dźwięk się rwał (zabierał pasmo pamięci odtwarzaczowi). Wynik: `binaries/ghidra_stock/stock_probe_wav.txt`.
- **Bufor PCM dekodera = początek pamięci DSP DEC** (`0x3B00000`, pierwsze 64 KB). Próbki są zapisane **tak jak w pliku WAV** (16 bit LE, L R na przemian). Pełny blok 64 KB: dwie zgodności na okres sinusa.
- Trafienia w puli eCos (`0x81490000…`) to bufory odczytu pliku.

### Protokół zasilania dekodera (z kodu + rejestrów podczas WAV) 🔍
- `HAL_MAD_GetCommInfo` (`0x8001194c`): `0x7F` → `baza_DEC + 0x112D6A·8`, `0x8B` → `baza_DEC + 0x112D66·8` (adres żądanych danych), `0x8C` → `0x112D68·8` (rozmiar), `0x8E` → `0x112D6A·8`.
- `HAL_MAD_SetCommInfo` (`0x80011268`): `0x8A` → `0x112D84` = rozmiar/8 („dane gotowe”), `0x8D` → `0x112D94` = rozmiar/8, flagi `0x112D86` (bity 5, 6, 7), `0x112D8E` bit 4.
- `0x80006e98(rozmiar)` = SetCommInfo(`0x8A`, rozmiar) + `0x80008f90(0, 0xE3)` (polecenie do DEC). `0x80006ec8` = SetCommInfo(`0x8D`), `0x80006eec` = GetCommInfo(`0x8E`). Wołane pośrednio (tablica funkcji API).
- Migawka podczas WAV: `0x2D66` = 4, `0x2D68` = `0xB5`, `0x2D6A` = `0x0E00` (·8 = `0x7000`), `0x2D6C` = `0x200` (·8 = 4 KB), `0x2D6E` = `0x11F9`, `0x2D8A` = `0xE000`, `0x2D8C` = `0xE7` (MP3: `0x72`), `0x2D84` = 0 (już odebrane), `0x2D9C` = 0.

### Co dalej
1. Ślad czasowy rejestrów `0x112D60–0x112D9F` i `0x112A00–0x2A2F` podczas WAV (sonda próbkująca co ~1 ms i wypisująca tylko zmiany). Pokaże kolejność: żądanie DSP → zapis CPU → „dane gotowe”.
2. Jak odtwarzacz wybiera dekoder PCM (`xpcm`, typ `0x0B` w DEC, albo `XPCM2` w SE) i jak ustawia format (48 kHz, 16 bit, 2 kanały). `0x8000997c(0xA1/0xD1, …)` wybiera kodek.
3. Odtworzyć to w `audiotest2`: załadować `xpcm`, ustawić format, karmić bufor na początku pamięci DEC.

### Ślad czasowy skrzynki DEC podczas WAV ✅ (`tools/baremetal/stockprobe/stocktrace.c`)
Sonda próbkuje `0x112D60–0x112D9E` i `0x112A00–0x2A2E` najszybciej jak może przez 2 s, zapisuje zmiany w RAM (`0xA3900000`) i dopiero potem je wypisuje. `printf` oryginału obsługuje niezawodnie tylko **3 argumenty**: przy 4. wypisywał śmieci. Wynik: `binaries/ghidra_stock/stock_trace_wav2.txt` (6000 zdarzeń, 315 ms).
- **`0x112D68`**: licznik, +1 dokładnie co **4,0 ms**.
- **`0x112D6A`**: +`0x200` co **64 ms**, a jednocześnie **`0x112D8C`** +1 (numer porcji). 64 ms przy 48 kHz stereo 16 bit = **12 288 B**, więc jednostka `0x2D6A` = 24 B (8 słów DSP), a porcja = `0x200` jednostek = 12 KB. To wskaźnik odczytu dekodera ❓ (w śladzie od `0x0C00` do `0x1800`).
- `0x112D60`, `0x112D62`, `0x112D6E`, `0x112A16`: zmieniają się ciągle (liczniki i stan dekodera).
- **CPU nie zgłasza danych przez rejestry skrzynki**: `0x2D84`, `0x2D86`, `0x2D94`, `0x2D9C` są stałe. Najpewniej odtwarzacz dopisuje dane przed wskaźnikiem odczytu, a DSP czyta sam ❓.
- Wartości stałe podczas WAV: `0x2D66` = 7, `0x2D6C` = `0x200` (rozmiar porcji?), `0x2D8A` = `0xE000`, `0x2D8E` = `0x10A0`, `0x2D98` = `0x55`, `0x2A08` = `0x200`, `0x2A0C` = `0x0A00`, `0x2A0E` = `0x0F`.
- Do wyjaśnienia: wskaźnik × 24 B daje przesunięcia do `0x24000` (147 KB), a sonda znalazła próbki tylko w pierwszych 64 KB pamięci DEC. Następna sonda: zrzut pamięci DEC wokół `baza + 0x2D6A·24` i `baza + 0x2D6A·8`.

## Pierwszy dźwięk z Linuksa: `tools/board/audiotest3.c` (2026-10-14, zweryfikowane na płytce)

Ton 1 kHz słychać na HDMI. Na początku charczał, bo dane szły za szybko (patrz niżej). Ścieżka jest taka sama jak w odtwarzaczu WAV stocka: **DEC DSP z dekoderem xPCM**, bez DMA Readera.

1. Inicjalizacja jak w `audiotest2`, plus brakujący krok z `MDrv_SOUND_Init` (`0x8001580c`): **przed** kodem systemowym SE (0x60) trzeba załadować do SE kod **0x31 `mpeg_ad`**:

   | Segment | Rozmiar | Adres DSP |
   |---|---|---|
   | PM | 0x20E2 B | 0x161F |
   | CM | 0xB07 B | 0x780 |
   | X1 | 0x2745 B | 0xA000 |
   | X2 | 0x1710 B | 0x11000 |

   X1 i X2 lądują w DRAM pod adresem baza SE + adres·3. Nagłówek z kodem to `se31_fw.h`.
2. Po starcie obu DSP stock wywołuje „SIF load 0xB1” (`0x8000a988` → `Alg2ReloadCode`, `0x8000f71c`). Tabela pau_info SIF jest w tym firmware **pusta**, więc wykonuje się tylko handshake z SE:
   - puls 0x2AA0 bit 6, 0x2A80 = 3;
   - 0x2DDC hi = 0xF0, puls 0x2A80 bit 5, czekać aż 0x2DFE = 0x33;
   - 0x2DDC hi = 0xF1, puls, czekać aż 0x2DFE = 0x77;
   - 0x2DDC = 0.

   Potem 0x2DFB = 0x40, tak jak w stocku. **To była przyczyna ciszy**: bez tego SE (DSP od ścieżki wyjściowej) stał na komendzie 0xF3 i nie przetwarzał nic, choć DEC dekodował.
3. Wybór dekodera (`0x802bf630` → `0x8000f348(9)` → `AlgReloadCode(0x0B)`, `0x8000f5bc`):
   - 0x2D2C = 0; komenda dekodera 0x2A2C = 0;
   - puls 0x2A20 bit 6;
   - 0x2D9C hi = 0xF0, puls 0x2A00 bit 5, czekać aż 0x2DBE = 0x33;
   - 0x2A00 |= 3;
   - IDMA: PM xPCM (0x29D B pod 0xFFF), potem CM (0x1A88 B pod 0x200), plik `xpcm_fw.h`;
   - 0xF1, czekać aż 0x2DBE = 0x77;
   - 0x2B01 bit 6 = 0, 0x2D9C = 0.

   Potem 0x2DBB = 0xC0 (ID dekodera; 0xC1 podczas grania).
4. Parametry xPCM (`0x800137a0`): IDMA do pamięci DM (adres | 0x8000), DM 0x37A0..0x37A5 = format 1 (PCM), kanały−1, częstotliwość, bity, blockSize, samplesPerBlock.
5. PLAYFILE: 0x2C60 &= ~7, 0x2A2C (bity 0–4) = 4.
6. Podawanie danych (`0x802bf9f4`/`0x802bf940`, ISR `0x8000d4e4` przy 0x2DB2 == 3, IRQ 0x3D w numeracji eCos):
   - żądanie DSP widać jako zmianę licznika **0x2DAC**;
   - skopiować 0x2D6C·8 B (4 KB) PCM (16-bit LE, przeplot L/R) pod baza DEC + 0x2D6A·8 (pierścień 64 KB);
   - zgłosić gotowość: 0x2D8C = licznik++, 0x2D8B = 0xE0, przełączyć bit 5 w 0x2A00 tam i z powrotem (`0x8000dba8`);
   - mailbox 5 (0x2D8A) = rozmiar/8 tylko dla krótkiej ostatniej porcji.

   0x2D66 to sekundy odtwarzania, 0x2D68 to ms/4.
7. DSP prosi o dane, gdy ma miejsce (~370 KB/s), czyli szybciej niż gra (192 KB/s przy 48 kHz stereo). Gdy podaje się wszystko od razu, nadpisuje się nieodtworzone dane i dźwięk charczy. Trzeba więc ograniczać tempo do czasu rzeczywistego plus zapas (`LEAD`, domyślnie 32 KB). Stock robi to naturalnie, bo karmi z pętli odtwarzacza co ~64 ms.

Rejestry wyjścia ustawione na wartości ze stocka (lista `stock_out` w `audiotest3.c`): 0x2C62/6A/82/86/C8, SE 0x2D00–0x2D54, 0x2DC2/CA, 0x2D8E i HDMI 0x1131xx/0x1133xx.

### Zimny start: DEC też potrzebuje kodu przed systemem (zweryfikowane 2026-10-14)

Po pełnym odcięciu zasilania (10 s) DEC nie odpowiadał na 0xF0. `MDrv_MAD_Init` (`0x8001375c` → `0x800136e4(3)`) ładuje do DEC dekoder **0x03 `mpeg`** **przed** kodem systemowym 0x50:

| Segment | Rozmiar | Adres DSP |
|---|---|---|
| CM | 0x1C05 B | 0x200 |
| PM | 0x1FE3 B | 0xFFF |
| X1 | 0x1AEB B | 0x8600 |
| X2 | 0x1710 B | 0x12600 |

Nagłówek z kodem to `dec03_fw.h`. Bez niego system DEC skacze w pustą pamięć. Wcześniej działało tylko dlatego, że po krótkim resecie w pamięci DSP został kod załadowany przez stock.

Status „alive” (0x2DBD/0x2DFC = 0xE3) to mailbox DSP→MCU. **Nie zeruje się** po resecie DSP i CPU nie może go wyczyścić, więc nie dowodzi, że DSP działa. Wiarygodnym testem jest dopiero odpowiedź 0x33/0x77 na 0xF0/0xF1.

Z ograniczeniem tempa (`LEAD` 32 KB) przez 15 s poszło 712 porcji × 4 KB ≈ 194 KB/s, czyli tempo czasu rzeczywistego dla 48 kHz stereo (192 KB/s).

Pendrive (PHILIPS 32 GB) potrafi się zawiesić („medium not present”, sense 02/3A). Reset płytki tego nie naprawia, pomaga dopiero odcięcie zasilania. Po pełnym wyłączeniu zasilania sterownik fotg210 raz zgłosił też „controller refused to start: -145”. Ponowne pełne wyłączenie pomogło.

### Ustawienia SE i wyjścia przed startem DSP (2026-10-14)

Pierwsze uruchomienie po zimnym starcie grało cicho, choć DSP dekodował. Drugie już grało. Przyczyna: parametry SE i ścieżki (lista `stock_out`: 0x2D00–0x2D54, 0x2DC2/CA, 0x2C6x/8x, HDMI) były zapisywane dopiero po handshake'u SE, a SE czyta je przy starcie. Teraz `audiotest3` zapisuje je zaraz po tablicy inicjalizacyjnej, przed startem DSP, i jeszcze raz po nim.

Na płytce (stan ciepły) ton jest od razu i czysty. **Do potwierdzenia:** pierwsze uruchomienie po pełnym odcięciu zasilania.

Uwaga: po programie skompilowanym tuż przed odcięciem zasilania zostaje uszkodzona binarka (ext2 bez dziennika). Po kompilacji trzeba zrobić `sync`.

### Stan na koniec sesji (2026-10-15)

- Działa: `audiotest3`, `wavplay`, biblioteka `mstar_audio.h`. Instrukcja: `docs/dzwiek_uzycie.md`.
- Do potwierdzenia: pierwsze uruchomienie po pełnym odcięciu zasilania.
- Działa (2026-10-15): Doom z dźwiękiem na HDMI i SCART (`fbDOOM/fbdoom/i_mstarsound.c`, commit `cf08a57`). Brak dalszych komunikatów po `I_MStar_InitSound` to nie zawieszenie, tylko normalne zachowanie.
- Później: sprzętowe MP3 (dekoder DEC 0x07, plan w `docs/dzwiek_uzycie.md`), muzyka w Doomie (OPL), rezerwacja pamięci DSP w DTS.

## Własny program DSP zamiast firmware'u MStara (2026-10-20) ✅

Sprawdzone na płycie, z pomiarem mikrofonem komputera (szczegóły i przebieg: `dsp_re/README.md`, sekcja „Sesja 2026-10-20 (trzecia)”).

- **Dźwięk na HDMI bez kodu MStara:** `dsp_re/own/pcm.s` (nasz program SE) + `tools/board/own_audio.h` (to samo API co `mstar_audio.h`: `maudio_open/write/close`, tylko 48 kHz). fbDOOM: `make clean; make NOSDL=1 OWN_SOUND=1`.
- **Sprzęt:** przerwanie slotu `0x08` SE raz na ramkę wyjścia HDMI (48 kHz); potwierdzenie = odczyt `DM(0x817F)` i dwa zapisy z instrukcją pomiędzy; próbka = słowo **24-bit ze znakiem** w `DM(0x8060)` (L) i `DM(0x8061)` (P), czyli PCM 16-bit `<< 8`. Rejestry i DM SE mają 24 bity; DM to `0x0000–0x2FFF`.
- **Dane z Linuksa:** pierścień w DM SE pisany przez IDMA (`0x112A84` adres `| 0x8000`, dane `0x112A82/83`), ok. 2,2 mln słów/s; DSP podaje pozycję w skrzynce `0x112DE0`.
- **Minimalny init bloku audio** po zimnym starcie (bisekcja tabeli `MDrv_AUDIO_WriteInitTable`, 192 wpisy): reset `0x112C00`, `0x110C80 = 0x0E`, `0x112CA4 = 0xFF`, `0x112C48 |= 0x80`, `0x112CB1 |= 0x04`, włączenie (`0x112D31`, `0x112B42`), potem ścieżka HDMI jak w `hdmi_audio_on()`. Bez initu IDMA nie działa i jest cisza.
