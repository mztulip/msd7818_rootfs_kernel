# Demodulator DVB-T i próbki częstotliwości pośredniej (IF)

Pytanie: czy da się wyciągnąć surowe próbki IF z ADC (jak w RTL-SDR), czy trafiają one tylko do sprzętowego dekodera DVB-T?

**Krótka odpowiedź:** w normalnej pracy próbki z ADC **nie wychodzą poza blok demodulatora**. Nie ma ścieżki DMA, która kopiowałaby je do DRAM, a procesor MIPS widzi tylko gotowy strumień MPEG-TS i rejestry statusu. Są poszlaki trybu diagnostycznego „ADC dump” w IP MStara, ale nie jest potwierdzony dla Kronusa, a nawet jeśli istnieje, da tylko krótkie migawki, nie ciągły strumień. Szczegóły poniżej.

## Tor sygnału

```
antena → MxL5007T (RF → IF) → pin SoC → ADC (w SoC) → DSP demodulatora DVB-T → MPEG-TS → TSP (demux) → DMA → DRAM
                                              ↑ steruje: wbudowany 8051 z firmware ładowanym przez MIPS
```

| Element | Co wiadomo | Pewność |
|---|---|---|
| Tuner MxL5007T | I2C port 0, adres `0x60`; wyjście IF (MxL5007T ma kilka do wyboru, m.in. 4,57 MHz dla kanałów 8 MHz) | ✅ adres; ❓ częstotliwość IF. Nazwa `schematics/dodatkory_tor_posredniej4.5Mhz.png` sugeruje ~4,5 MHz |
| ADC + MPLL demodulatora | blok `dmd_ana_misc`, `0x1128xx` → CPU `0xBF225000` (zgodne z `kronus.dtsi`) | 🔍 |
| Zegary demodulatora | `0x111F02`–`0x111F1D` → CPU `0xBF223E04`–`0xBF223E39`, ustawiane w `INTERN_DVBT_InitClkgen` | 🔍 |
| DSP DVB-T | sprzętowy tor OFDM (miksery, AGC, FFT, korektor, Viterbi, RS) | 🔍 (z nazw parametrów w `drvDMD_INTERN_DVBT.h`) |
| Mikrokontroler 8051 | steruje DSP; firmware `INTERN_DVBT_table` (24 448 B) w `libdrvDEMOD.a`, ładowany przez MIPS | 🔍 |
| Wyjście | MPEG-TS do demultipleksera TSP | 🔍 |

## Firmware 8051 i jego ładowanie (🔍 z `halDMD_INTERN_DVBT.o`)

`INTERN_DVBT_table` zaczyna się od `02 58 C1 02 55 E9 …`, czyli od tablicy wektorów 8051 (`LJMP 0x58C1` pod `0x0000`, kolejne `LJMP` pod `0x0003`, `0x000B`…). Demodulator ma więc własny 8051.

`INTERN_DVBT_Power_On_Initialization`:
1. `INTERN_DVBT_InitClkgen` (zegary, MPLL, ADC), potem `HAL_DMD_ADC_IQ_Switch`.
2. Ładowanie firmware'u przez rejestry w banku `0x1034`, offset `0x80`:

| Reg | CPU | Użycie przy ładowaniu |
|---|---|---|
| `0x103480` | `0xBF206900` | zatrzymanie / start MCU (0 → 1) |
| `0x103481` | `0xBF206901` | j.w. |
| `0x103483` | `0xBF206905` | tryb zapisu (`0x50` / `0x51`) |
| `0x103484` / `0x103485` | `0xBF206908` / `0xBF206909` | adres / dane |

3. Reset (`INTERN_DVBT_Reset`), potem komunikacja przez skrzynkę pocztową `MDrv_SYS_DMD_VD_MBX_*` (`libdrvSYS.a`) i inicjalizacja rejestrów DSP (`INTERN_DVBT_DSPReg_Init`, tabela `INTERN_DVBT_DSPREG`).

Zgadza się to z obserwacją na płycie: po `tuner_init` pod `0xBF206908` jest `0x5E6E`, czyli w okolicy końca firmware'u (rozmiar `0x5F80`), a pod `0xBF206900` jest `0x0100` (MCU uruchomiony) ✅/❓.

## Gdzie normalnie trafiają próbki

DVB-T (w odróżnieniu od DVB-T2) nie potrzebuje DRAM: deinterleaving i FFT mieszczą się w wewnętrznym SRAM demodulatora. W bibliotece nie ma żadnej funkcji, która konfigurowałaby DMA próbek ADC do pamięci. Jedyny strumień o dużej przepustowości wychodzący z demodulatora to **TS**.

## Możliwe sposoby dostępu (od najbardziej do najmniej obiecujących)

### 1. Tryb „ADC dump” do SRAM demodulatora ❓
W źródłach zewnętrznego demodulatora MStara MSB1233C (`MstarCore/src/drivers/cofdm/MSB1233C.c`, ok. linii 1955) są komentarze ze środowiska symulacyjnego, m.in.:
```
// @0x0916
// [3:0]  reg_ckg_dvbtm_adc_2x_4x
// [8:4]  reg_ckg_dvbtm_sram_adc_t22x
// `elsif SIM_ADC_DUMP
//     wreg 9 0x16 0x0101
```
Rdzeń DVB-T MStara ma więc tryb, w którym próbki ADC są zapisywane do wewnętrznego SRAM (osobny zegar `sram_adc`). Kronus zapewne używa tej samej rodziny IP („dvbtm”), ale numery rejestrów są inne i nieznane.

Jeśli to zadziała:
- dostaniemy **migawkę** o rozmiarze SRAM (pewnie dziesiątki–setki KB, czyli milisekundy sygnału),
- odczyt przez rejestry albo skrzynkę 8051 będzie powolny,
- wystarczy to do analizy widma w danej chwili, ale nie do ciągłego SDR.

### 2. Własny firmware 8051 ❓
Firmware ładuje MIPS, więc można go podmienić. 8051 ma bezpośredni dostęp do rejestrów DSP i SRAM. To nadal tylko migawki (8051 jest wolny), ale to najpewniejsza droga do zbadania pkt 1.

Krok wstępny: deasemblacja `INTERN_DVBT_table` w Ghidrze (procesor 8051) i szukanie obsługi poleceń ze skrzynki pocztowej.

### 3. Wyjście debug I/Q ❓
`drvDMD_INTERN_DVBT.h` ma parametr `E_DMD_DVBT_MIXER_IQ_DBG_SEL`. To prawdopodobnie wybór sygnału do rejestru diagnostycznego. Odczyt pojedynczych wartości przez RIU daje próbki w przypadkowych chwilach: nadają się do pomiaru mocy czy DC, ale nie do rekonstrukcji sygnału.

### 4. Dane, które demodulator już liczy ✅ (dostępne funkcje)
Bez sztuczek da się odczytać: AGC (IF/RF), SNR, BER, przesunięcie częstotliwości (`INTERN_DVBT_Get_CFO`, `Get_SFO`), parametry TPS, a także odpowiedź impulsową kanału (`INTERN_DVBT_Show_td_coeff`, `Show_ChannelLength`). To wystarczy na „analizator sygnału DVB-T”, ale to nie są surowe próbki.

### 5. ADC dane w strumieniu TS ❓
Gdyby demodulator potrafił wstawiać dane diagnostyczne do TS (tak działa tryb SDR w RTL2832U), dałoby się je przechwycić przez TSP do DRAM. W MStarze nie ma na to żadnej poszlaki. Do tego przepustowość TS (dziesiątki Mbit/s) jest mniejsza niż strumień ADC (setki Mbit/s), więc najwyżej po decymacji.

### 6. Sprzętowo: podsłuchać IF przed SoC ✅ (realne)
Sygnał IF z MxL5007T idzie ścieżką analogową do SoC. Zewnętrzny ADC na tej linii daje prawdziwe SDR z MxL5007T jako front-endem (strojenie przez I2C z Linuksa, gdy I2C zacznie działać):
- kanał 8 MHz na IF ~4,5 MHz zajmuje ok. 0,5–8,5 MHz, więc potrzeba ≥ 20 MS/s,
- węższe sygnały (FM, patrz `MxL5007T_tune_radio.pdf`) też wymagają próbkowania powyżej 2× IF, chyba że użyje się próbkowania pasmowego (undersampling) z filtrem.

## Analiza firmware'u 8051 (🔍)

### Pliki i narzędzia

- `binaries/demod_fw/intern_dvbt_fw.bin` (24 448 B) i `intern_dvbc_fw.bin` (19 344 B) wycięte z `libdrvDEMOD.a` (`objcopy --only-section=.data.INTERN_DVBT_table`). Ładowane od adresu `CODE:0000`.
- `tools/ghidra/Seed8051.java` (pre-script): zakłada funkcje na wektorach z `LJMP` oraz na celach `LCALL` wołanych z ≥ 2 miejsc. Kompilator Keil C51 dużo skacze pośrednio, więc bez tego Ghidra znajduje tylko ok. 2 KB kodu.
- `tools/ghidra/Export8051.java` (post-script): dekompilacja wszystkich funkcji (`decompiled.c`), lista funkcji i mapa dostępów do XDATA (`xdata_refs.txt`).

```sh
JAVA_HOME=/usr/lib/jvm/java-27-openjdk /opt/ghidra/support/analyzeHeadless <proj_dir> demod \
  -import binaries/demod_fw/intern_dvbt_fw.bin -overwrite \
  -loader BinaryLoader -processor 8051:BE:16:default \
  -scriptPath tools/ghidra -preScript Seed8051.java -postScript Export8051.java <out_dir>
```
Ghidra 12 wymaga nowszej Javy niż domyślna 11, stąd `JAVA_HOME`.

Pokrycie: ok. 290 funkcji, ok. 7,8 KB z 24 KB w ciałach funkcji. Reszta to dane albo kod osiągany pośrednio, czyli materiał do dalszej analizy w GUI.

### Wektory i start

| Adres | Wektor | Cel | Rola |
|---|---|---|---|
| `0x0000` | RESET | `0x58C1` | start Keil C51: zeruje IDATA, `SP=0x72`, rozpakowuje tablicę inicjalizacji (`0x5E06`), potem konfiguruje `XDATA 0x20C0–0x20C3` (wartości z `CODE:0x5E7A–0x5E7C`) i zeruje `0x2F1C` |
| `0x0003` | INT0 | `0x55E9` | **obsługa skrzynki pocztowej od MIPS** (niżej) |
| `0x000B` | TIMER0 | `0x5D9F` | tik systemowy, przeładowanie `TH0:TL0 = 0xDCD8` |

`0x5F63` włącza INT0 (`EX0=1`, `IT0=1` = zbocze) i czyści bit 2 w `XDATA 0x1B00`.

Funkcje `0x0456`, `0x046F`, `0x049C`, `0x04AE` to procedury biblioteki Keil (`?C?CLDPTR` / `?C?CSTPTR` itp.) do wskaźników generycznych: typ `1` = XDATA, `0` = IDATA, `0xFE` = PDATA. Warto je tak nazwać w Ghidrze, bo dekompilacja robi się wtedy dużo czytelniejsza.

### Mapa XDATA 8051

| Zakres XDATA | Co to jest | Pewność |
|---|---|---|
| `0x0000`–`0x00FF` | drobne zmienne | 🔍 |
| `0x1B00`–`0x1B10` | sterowanie przerwaniami MCU (`0x1B10` bit 2 = przerwanie od hosta, `0x1B08` = kasowanie) | 🔍 |
| `0x2000`–`0x25FF` | **rejestry sprzętowe demodulatora** (najwięcej odwołań: strony `0x21`–`0x25`) | 🔍 zakres; ❓ mapowanie na RIU |
| `0x2F00`–`0x2F1F` | **skrzynki pocztowe** z MIPS (RIU `0x1126xx`) | 🔍 potwierdzone z obu stron |
| `0x8000`–`0x83FF` | RAM firmware'u: zmienne, a pod `0x8300 + n` „rejestry DSP” (parametry z `INTERN_DVBT_DSPREG`) | 🔍 |

Mapowanie stron `0x20`–`0x25` na RIU nie jest jeszcze ustalone. Strona `0x2F` odpowiada bankowi RIU `0x1126` (patrz niżej), więc nie jest to po prostu „młodsze 16 bitów adresu”.

### Skrzynka pocztowa: protokół (potwierdzony po obu stronach)

8051 (INT0, `0x55E9`) i MIPS (`HAL_SYS_DMD_VD_MBX_DVB_*` w `libdrvSYS.a`, `halDMD_VD_MBX.o`) używają tych samych rejestrów:

| 8051 XDATA | RIU | CPU MIPS | Pole |
|---|---|---|---|
| `0x2F00` | `0x112600` | `0xBF224C00` | polecenie; 8051 odpisuje `0xFF` = gotowe |
| `0x2F01` | `0x112601` | `0xBF224C01` | adres, młodszy bajt |
| `0x2F02` | `0x112602` | `0xBF224C04` | adres, starszy bajt |
| `0x2F03` | `0x112603` | `0xBF224C05` | dane |
| — | `0x103483` bit 1 | `0xBF206905` | MIPS: impuls `2` → `0` = przerwanie INT0 dla 8051 |

Polecenia:

| Kod | Działanie w 8051 | Funkcja MIPS |
|---|---|---|
| `1` | `data = XDATA[adres]` | `…_DVB_ReadByte` |
| `2` | `XDATA[adres] = data` | `…_DVB_WriteByte` |
| `3` | `data = XDATA[0x8300 + adres]` („rejestr DSP”) | `…_DVB_ReadDspReg` |
| `4` | `XDATA[0x8300 + adres] = data` | `…_DVB_WriteDspReg` |
| `0xA5` | pętla nieskończona (zatrzymanie MCU, np. przed przeładowaniem firmware'u) | |

Najważniejszy wniosek: **przez polecenia 1/2 MIPS może czytać i pisać dowolny adres XDATA 8051**, w tym wszystkie rejestry sprzętowe demodulatora widoczne dla 8051. To gotowe narzędzie do badania demodulatora bez pisania własnego firmware'u, także do szukania trybu „ADC dump”.

Odczyt bajtu XDATA z U-Boota (tylko gdy firmware działa, czyli po `tuner_init`; nietestowane ❓):
```
mw.b bf224c04 <adres_hi>
mw.b bf224c01 <adres_lo>
mw.b bf224c00 1
mw.b bf206905 2
mw.b bf206905 0
md.b bf224c00 1      # oczekiwane FF
md.b bf224c05 1      # wartość
```

### Druga skrzynka: pakiety poleceń

`XDATA 0x2F1C`/`0x2F1D` (prawdopodobnie RIU `0x11261C`/`0x11261D`, CPU `0xBF224C38`/`0xBF224C39` ❓) obsługują funkcje `0x5835` i `0x57A4`. Wygląda to na kanał dla `INTERN_DVBT_Cmd_Packet_Send` z MIPS: bity `0x2F1C` (bit 0 = gotowe, bit 1 = zajęte, bity `[7:4]` = długość?) i bufor w `XDATA 0x832F`. `0x57A4` rozdziela kody poleceń `0`, `2`, `5`, `9`, `12`.

## Następne kroki badawcze (bez pisania kodu na płytę)

1. W GUI Ghidry nazwać funkcje biblioteki Keil i dokończyć dezasemblację pętli głównej (startup kończy się skokiem do `main` przez tablicę Keila).
2. Ustalić, jak strony XDATA `0x20`–`0x25` mapują się na RIU. Wystarczy porównać zapisy MIPS w `INTERN_DVBT_InitClkgen` z odczytami przez skrzynkę (polecenie 1) na płycie.
3. Porównać zegary Kronusa (`0x111F02`–`0x111F1D`) z rejestrami „TOP” MSB1233C (`0x0900 + n*2`), aby znaleźć odpowiednik `reg_ckg_dvbtm_sram_adc_t22x`.
4. Zdekodować pakiety poleceń (`INTERN_DVBT_Cmd_Packet_Send` po stronie MIPS i `0x57A4` w 8051).
5. Po `tuner_init` zrzucić w U-Boocie rejestry demodulatora (`md bf223e00 40`, `md bf225000 80`, `md bf224c00 10`) jako punkt odniesienia.

## ✅ Odczyt sygnału z demodulatora i widmo przemiatane (2026-10-13)

### Dostęp do XDATA 8051 z Linuksa: `tools/board/dmdmbx.c`

Program dla płyty (kompilacja na płycie: `gcc -O2 -o dmdmbx dmdmbx.c`). Realizuje protokół skrzynki opisany wyżej przez `/dev/mem` (RIU `0x1F200000`):
1. czekaj, aż bajt polecenia `0x24C00` = `0`;
2. wpisz adres (`0x24C04` = starszy, `0x24C01` = młodszy), dane (`0x24C05`) i polecenie;
3. impuls `2` → `0` pod `0x06905`;
4. czekaj na `0xFF`;
5. odczytaj dane i wyzeruj polecenie.

Jeden odczyt trwa ok. 4,4 µs (4096 bajtów × 10 przebiegów w 0,18 s).

Wymaga działającego firmware'u 8051: w MBoocie `tuner_init`, a potem start Linuksa z tej samej sesji (Linux nie resetuje demodulatora). Polecenia: `r ADDR N`, `w ADDR VAL`, `dr ADDR N` (rejestry DSP, `0x8300+`), `changes ADDR N K`, `sample ADDR N K`, `pwr ADDR K`.

Kontrola: `dmdmbx r 0x20c0 16` → `12 00 04 …`. `0x20C2` = `0x04`, to samo, co odczytał MBoot ✅.

### „Żywe” rejestry ✅

Skan `dmdmbx changes 0x2000 0x1000 10` (bez strojenia demodulatora): zmieniają się tylko `0x2034` i `0x2096`–`0x2098`.

**XDATA `0x2098` śledzi sygnał IF z tunera** (8 bit, kod z przesunięciem, środek `0x80`):

| Stan tunera | Rozkład `0x2098` (3000 odczytów) |
|---|---|
| uśpiony (`0x01`=0, `0x0F`=0) | `0x80`–`0x82`, wariancja **0,3** |
| aktywny, 474 MHz | `0x0F`–`0xE5`, 158 różnych wartości, wariancja **~720–890** |

- Kolejne odczyty są nieskorelowane (autokorelacja z przesunięciem 1 ≈ 0,01), bo każdy odczyt to przypadkowa chwila co kilka µs, a IF ma kilka MHz.
- `0x2097` (górna połówka) i `0x2096` bit 0 też się zmieniają, ale nie korelują z `0x2098`. Każdy bajt jest odczytywany osobno, więc nie wiadomo, czy to młodsze bity tej samej próbki ❓.

**Wniosek:** nie ma równomiernego strumienia próbek, więc **FFT / widmo pośredniej nie jest możliwe tą drogą**. Da się za to wiarygodnie **mierzyć moc** jako wariancję wielu odczytów (powtarzalność ok. ±5%).

### Widmo przemiatane RF na ekranie: `tools/board/rfscan.c`

Kompilacja na płycie: `gcc -O2 -o rfscan rfscan.c -lm`. Program:
1. inicjalizuje MxL5007T przez `/dev/i2c-0` (DVB-T, IF 4,57 MHz, kwarc 24 MHz, pasmo 8 MHz);
2. dla każdej częstotliwości stroi tuner, czeka na blokadę PLL (`0xD8` = `0x0F`) i czas ustalania, liczy wariancję `READS` odczytów `0x2098`, a z niej 10·log10 (dB);
3. rysuje wykres słupkowy prosto w buforze ramki (tło i siatka raz, potem podmiana tylko bieżącego słupka; czerwony znacznik pod osią pokazuje pozycję przemiatania, więc w trybie ciągłym obraz nie miga) (fizycznie `0x2100000`, 1920×1080 RGB565): siatka co 100 MHz i co 10 dB, zakres 0–40 dB. Wypisuje też tabelę na konsolę.

```sh
# w MBoot: tuner_init, potem start vmlinux_i2c.bin (patrz i2c.md); w Linuksie:
echo 0 > /sys/class/vtconsole/vtcon1/bind      # inaczej fbcon zamaluje wykres
./rfscan 44 880 4 1500 1 30                     # START STOP STEP READS LOOPS SETTLE_MS
./rfscan 44 880 4 1500 0 30 > /tmp/rfscan.log 2>&1 &   # tryb ciągły w tle (konsola wolna)
kill %1                                         # albo: killall rfscan
echo 1 > /sys/class/vtconsole/vtcon1/bind      # przywrócenie konsoli
```

#### Jak znaleziono rejestr `0x2098`

1. **Interfejs:** w firmware 8051 przerwanie INT0 obsługuje skrzynkę pocztową (polecenie 1 = odczyt dowolnego adresu XDATA), a po stronie MIPS ten sam protokół jest w `libdrvSYS.a` (`HAL_SYS_DMD_VD_MBX_DVB_ReadByte`). Na tej podstawie powstało narzędzie `dmdmbx` do czytania XDATA z Linuksa.
2. **Warunek:** firmware 8051 musi działać, więc w MBoocie `tuner_init`, a potem start Linuksa w tej samej sesji. Sprawdzenie: `dmdmbx r 0x20c0 16` daje `0x20C2` = `0x04`, tak jak w odczycie MBoota.
3. **Przeszukanie:** wg analizy firmware'u rejestry sprzętowe demodulatora leżą w XDATA `0x2000`–`0x2FFF`. `dmdmbx changes 0x2000 0x1000 10` czyta cały obszar 10 razy (0,18 s) i wypisuje bajty, które się zmieniły. Wyszły tylko `0x2034` i `0x2096`–`0x2098`; reszta jest stała, bo demodulator nie był nastrojony.
4. **Charakterystyka:** `dmdmbx sample 0x2094 6 25` pokazał, że `0x2098` zachowuje się jak szum wokół `0x80`, a `0x2097` i `0x2096` mają losowe pojedyncze bity.
5. **Test przyczynowy:** porównanie rozkładu `0x2098` przy tuner uśpiony (`mxl_w 0x01 0x00; mxl_w 0x0f 0x00`) i tuner nastrojony na 474 MHz (300 odczytów): uśpiony `0x80`–`0x82`, aktywny `0x5F`–`0x9D` i szerzej. Bajt reaguje więc na sygnał z tunera.
6. **Statystyka** (2000 odczytów pobranych na PC): 8 bit, 158 różnych wartości, średnia 129 (≈ `0x80`), odchylenie ok. 29, autokorelacja z przesunięciem 1 ≈ 0,01 (próbki niezależne). Bajty `0x2096`/`0x2097` nie korelują z `0x2098`; każdy bajt jest czytany w innej chwili, więc to nic nie przesądza.
7. **Weryfikacja jako miernik mocy:** wariancja uśpiony = 0,3; nastrojony = 720–890 (474 MHz), 565 (200 MHz), 295 (100 MHz), powtarzalnie ±5%. Z anteną widać multipleksy DVB-T jako garby szerokości kanału.

Nie wiadomo jeszcze, czym dokładnie jest `0x2098` (wyjście ADC, miksera albo błąd AGC) ❓. Odpowiedź da firmware 8051: które funkcje czytają lub zapisują ten adres.

#### Skąd dane i jak liczona jest moc

**Źródło danych.** Bajt XDATA `0x2098` demodulatora (8051) zmienia się zależnie od sygnału z tunera. Przy uśpionym tunerze stoi (`0x80`–`0x82`), a przy pracującym skacze wokół `0x80` (`0x0F`–`0xE5`). To chwilowa wartość z toru IF po ADC w kodzie z przesunięciem (`0x80` = zero). MIPS czyta go przez skrzynkę pocztową 8051 (polecenie 1 = odczyt XDATA), ok. 4,4 µs na odczyt. Odczyty wypadają w przypadkowych chwilach, a IF ma kilka MHz, więc próbki są nieskorelowane (autokorelacja ok. 0,01): FFT jest niemożliwe, ale moc da się zmierzyć.

**Pomiar w jednym punkcie (`rfscan`):**
1. strojenie MxL5007T przez I2C na f, pasmo 8 MHz;
2. czekanie na blokadę PLL (`0xD8` = `0x0F`) i jeszcze `SETTLE_MS` (30 ms), bo tuż po przestrojeniu sygnał się ustala;
3. N odczytów (domyślnie 1500), xᵢ = vᵢ − 128;
4. średnia m = Σxᵢ/N, **wariancja σ² = Σxᵢ²/N − m²**;
5. poziom = **10·log₁₀(σ²)** dB (względne, nieskalibrowane).

**Dlaczego wariancja = moc:** moc to średni kwadrat sygnału. Po odjęciu średniej (składowej stałej, DC) zostaje moc składowej zmiennej. Do średniej mocy nie trzeba spełniać twierdzenia o próbkowaniu: losowe chwile to uczciwe próbki tego samego sygnału. Twierdzenie o próbkowaniu jest potrzebne dopiero do odtworzenia kształtu sygnału albo widma wewnątrz pasma. Błąd statystyczny przy N = 1500 wynosi ok. √(2/N) ≈ 3,7% ≈ 0,16 dB, co zgadza się ze zmierzoną powtarzalnością ±5%.

**Skąd wykres:** każdy punkt to całkowita moc w oknie 8 MHz wokół f (filtr kanałowy tunera). Przestrajanie f co 4 MHz (albo co 1 MHz) i rysowanie słupków to zasada klasycznego analizatora z przemiatanym filtrem, tu z filtrem 8 MHz. Dlatego multipleks DVB-T wygląda jak płaski garb szerokości kanału.

**Ograniczenia:** brak pętli AGC (wzmocnienie tunera nieregulowane, więc różnice są małe), skoki tła od przełączania pasm tunera (ok. 128, 336, 540, 650 MHz), brak kalibracji do dBm.

**Czas ustalania jest kluczowy.** Pomiar w 474 MHz zależnie od `SETTLE_MS` (mimo blokady PLL):

| SETTLE_MS | 0 | 5 | 20 | 50 | 100 | 300 |
|---|---|---|---|---|---|---|
| wariancja | 3871 | 2510 | 712 | 721 | 764 | 818 |

Poniżej ok. 20 ms mierzy się stan przejściowy, a pierwsza wersja (5 ms) dawała ok. 38 dB wszędzie i fałszywe dołki co 56 MHz. Przy 30 ms pełne przemiatanie 44–880 MHz co 4 MHz (210 punktów) trwa **9,3 s**.

Pierwsze wyniki to pomiary **bez anteny** (linia bazowa), zapisane w `binaries/rfscan/bez_anteny_44-880_krok4.txt` i `bez_anteny_466-700_krok1.txt`:
- poziom 24–30 dB ze **skokami** przy ok. 128, **336**, 540 i 650 MHz: przełączanie pasm/wzmocnienia w tunerze. Skok przy 336 MHz pokrywa się z progiem 333 MHz, przy którym ustawiany jest bit `0x40` w rejestrze `0x80` tunera;
- wybrzuszenia bez anteny: ok. 92 MHz (+0,8 dB), 212–220 MHz (+1,5 dB), plato ok. 517–523 MHz (+1,3 dB). To zakłócenia zbierane bez anteny (np. z samej płyty) albo cechy tunera, a nie stacje;
- pojedyncze szpilki +5…+8 dB w 575, 577 i 645–651 MHz (sąsiednie punkty co 1 MHz wracają do tła): artefakty strojenia (prawdopodobnie przełączanie VCO, przy którym 30 ms to za mało).

### Skan z anteną minus linia bazowa ✅

Pliki: `binaries/rfscan/antena_44-880_krok4.txt`, `antena_466-700_krok1.txt` (te same parametry co bez anteny). Różnice ≥ 1 dB:

| Częstotliwość | Przyrost | Interpretacja |
|---|---|---|
| **470–478 MHz** | do +3,2 dB, plato szerokości ok. 8 MHz | **kanał 21 (474 MHz): multipleks DVB-T**, kształt pasuje do kanału 8 MHz |
| **665–671 MHz** | ok. +2 dB, plato ok. 7 MHz | **kanał 45 (666 MHz): multipleks DVB-T** |
| **740–744 MHz** | +8…+9 dB | najsilniejszy sygnał; krok 4 MHz nie pokazuje kształtu ❓ |
| 816–820 MHz | +2,5 dB | prawdopodobnie downlink LTE 800 (791–821 MHz) |
| 444–448 MHz | +3,5 dB | okolice PMR446 ❓ |
| 148–152, 396–420 MHz | ok. +1,3 dB | słabe sygnały radiokomunikacyjne ❓ |

Pojedyncze ujemne różnice (np. 647 MHz, −2,9 dB) to artefakty strojenia, niepowtarzalne między przebiegami.

Wniosek: `rfscan` działa jako prosty analizator widma RF. Multipleksy DVB-T są widoczne jako płaskie „garby” szerokości kanału. Kontrast to tylko kilka dB, bo wzmocnienie tunera nie jest regulowane (brak pętli AGC demodulatora) ❓.


Ograniczenia i pomysły ❓:
- wzmocnienie tunera nie jest kontrolowane (demodulator nie prowadzi pętli AGC), więc poziomy są względne;
- warto odjąć „tło”: przemiatanie bez anteny zapisać jako linię bazową;
- węższe pasmo (`0x0C` = `0x15`, 6 MHz) i mniejszy krok dadzą lepszą rozdzielczość;
- znaczenie `0x2098` (czy to próbka ADC, czy np. wyjście miksera/AGC) i bajtów `0x2096`/`0x2097` jest do ustalenia w firmware 8051 (kto czyta i pisze te adresy).
