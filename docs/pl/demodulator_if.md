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
| `0x8000`–`0x83FF` | RAM firmware'u: zmienne, a pod `0x8380 + n` (n < `0x80`) „rejestry DSP” (parametry z `INTERN_DVBT_DSPREG`) | 🔍 |

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
| `3` | `data = XDATA[0x8380 + adres]` („rejestr DSP”, tylko adres < `0x80`; poprawione 2026-11-03 z deasemblacji `0x5DEE`/`0x5DFB`) | `…_DVB_ReadDspReg` |
| `4` | `XDATA[0x8380 + adres] = data` | `…_DVB_WriteDspReg` |
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

### Jak MIPS sięga do XDATA 8051 (podsumowanie)

- XDATA 8051 (w tym rejestry demodulatora `0x2000`–`0x25FF`, np. `0x2098`) **nie jest zmapowana w przestrzeń adresową MIPS**. Dostęp jest pośredni: MIPS wpisuje polecenie do wspólnego banku RIU `0x1126`, wywołuje INT0, a odczyt lub zapis wykonuje handler 8051 (`0x55E9`) i odsyła wynik przez ten sam bank.
- Wspólny jest tylko bank `0x1126`: 8051 widzi go jako XDATA `0x2F00`–`0x2F1F`, a MIPS jako `0xBF224C00…` (rejestr 16-bitowy co 4 bajty, młodszy bajt `+0`, starszy `+1`, patrz `riu.md`).
- **Nie ładujemy własnego kodu do 8051.** Obsługa skrzynki jest częścią firmware'u MStara (`INTERN_DVBT_table`), który ładuje do 8051 komenda MBoota `tuner_init`, bo stockowy driver MStara używa poleceń 1/2 do konfiguracji demodulatora. My tylko korzystamy z tego „tylnego wejścia”. Bez `tuner_init` 8051 nie ma firmware'u i skrzynka nie odpowiada (`dmdmbx`: „no handshake from 8051”).
- Koszt to jedno przerwanie i obsługa w 8051 na każdy bajt (ok. 4,4 µs), stąd brak równego strumienia próbek.
- **Rozstrzygnięte 2026-11-03:** strona `0x20` jest też bankiem RIU `0x1120`, który MIPS może czytać bezpośrednio po przełączeniu `0x101E39` = 0 (patrz „Bezpośredni dostęp MIPS do rejestrów demodulatora”).

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

`0x2098` to surowe wyjście ADC I (✅ test 2026-11-03, sekcja niżej).

#### Kto używa `0x2096`–`0x2098` (🔍, sprawdzone 2026-11-03)

**Nikt w oprogramowaniu MStara.** To rejestr sprzętowy, którego ani firmware, ani aplikacja nie czyta:
- **Firmware 8051** (`intern_dvbt_fw.bin`, `intern_dvbc_fw.bin`): brak `MOV DPTR,#0x2096…0x2098`, brak argumentów 16-bit `R6=0x20 R7=0x98`, brak `MOV P2`/`MOV DPH` (adresowania stronami). Z całej strony `0x20` firmware używa tylko `0x2002`, `0x20C0`–`0x20CB`, `0x20D6`–`0x20D7`, czyli swoich zmiennych stanu (czyta je MIPS w `GetLock`, `Version`, `Show_BER_Info`…).
- **Driver MIPS** (`libdrvDEMOD.a`: `halDMD_INTERN_DVBT.o`, `drvDMD_INTERN_DVBT.o`, `halDMD_INTERN_common.o`): wszystkie ok. 380 wywołań `MDrv_SYS_DMD_VD_MBX_ReadReg/WriteReg` mają stałe adresy i żaden nie trafia w `0x2096`–`0x2098`. Wyjątki z adresem z rejestru to tylko `MDrv_DMD_DVBT_GetReg` (ogólne API), `Get_TPS_Info` (tablica) i `Get_FreqOffset`.
- **Stock app** (`kernel.lzma` po rozpakowaniu) i **MBoot** (`RAM87_DU.RAM`): brak instrukcji `addiu/ori` ze stałą `0x2096`–`0x2098` jako adresem XDATA. Kontrola metody: `li a0,0x2113`/`0x2122`/`0x20C0` znajdują się w obu binarkach. Jedyne trafienie w MBoocie (`0x8762E584`) to wskaźnik `lui 0x8769 + 0x2098`.

**Czym oryginał mierzy siłę sygnału:** `HAL_DMD_GetRFLevel` (`halDMD_INTERN_common.o`, wołane przez `GetSignalStrength`) czyta `0x2113` (próg 200), zapisuje selektor `0x2122` = `3`, czyta `0x2125`/`0x2124` (odczyt AGC) oraz `0x2105`, `0x212C`/`0x212D`. To kanał AGC demodulatora, a nie `0x2098`. Działa tylko wtedy, gdy demodulator prowadzi pętlę AGC (po `SetActive`/strojeniu kanału).

#### Czy jest drugi kanał (I/Q)? (stan 2026-11-03)

**Odpowiedź z testu na płycie (niżej):** sprzęt ma ADC I i ADC Q, ale sygnał z tunera idzie tylko przez ADC I; `0x2098` to surowe wyjście ADC I. Poniżej rozważania sprzed testu.

Znaleziony jest tylko jeden wyraźny rejestr sygnału (`0x2098`). Skan `changes` nie mógł jednak wykazać drugiego:
- MxL5007T daje rzeczywistą pośrednią jednym wyjściem, więc na etapie ADC jest jeden kanał. I/Q powstają dopiero w cyfrowym mikserze demodulatora.
- Skan robiono przy nieaktywnym demodulatorze (bez `SetActive`/strojenia kanału). Rejestry za mikserem mogły stać.
- Skan obejmował tylko XDATA `0x2000`–`0x2FFF`.
- `0x2097[7:4]` i `0x2096[0]` mogą być młodszymi bitami tej samej próbki (ADC 10–12 bit) albo innym kanałem. Brak korelacji z `0x2098` niczego nie przesądza (osobne odczyty). Surowych odczytów tych bajtów nie zapisano. `0x2034` też się zmienia, niezbadany.

Testy do zrobienia:
1. Rozkład `0x2097`/`0x2096` przy tunerze uśpionym i aktywnym. Młodsze bity próbki to równomierny szum w obu stanach, a kanał Q zmieniłby rozrzut razem z sygnałem.
2. Skan `changes` przy demodulatorze uruchomionym na 474 MHz (multipleks). Nowy bajt szumiący wokół `0x80` będzie kandydatem na I/Q za mikserem. Wymaga ustalenia sekwencji startu demodulatora przez skrzynkę (`halDMD_INTERN_DVBT.o`).

Wniosek (potwierdzony testem niżej): `0x2098` to surowe wyjście ADC I, widoczne w XDATA, bo blok rejestrów demodulatora (bank RIU `0x1120`) jest tam zmapowany w całości.

#### ADC, mikser i parametry DSP: analiza biblioteki i firmware'u (🔍, 2026-11-03)

**Rejestry DSP** (polecenia 3/4 skrzynki, XDATA `0x8380 + n`) mają indeksy z enuma `DVBT_Param` w `MstarCore/bsp/k1/include/drvDMD_INTERN_DVBT.h` (stara tabela, nie `DVBT_N_Param`). Potwierdza to `INTERN_DVBT_Config`, który pisze `0x03` BW, `0x06` LP_SEL, `0x27`/`0x28` FC (częstotliwość IF), `0x29`/`0x2A` FS (próbkowanie), `0x33` IQ_SWAP, `0x3D` CCI, `0x71` TS_SERIAL. Ważne dla ADC:

| n | Nazwa | XDATA | Źródło w sprzęcie (procedura raportu w 8051 pod `0x39F0`–`0x3A84`) |
|---|---|---|---|
| `0x0F` | CFG_ZIF | `0x838F` | |
| `0x17` | AGC_REF | `0x8397` | ← `0x2113` (ten sam bajt czyta `HAL_DMD_GetRFLevel`) |
| `0x1B` | PWDN_ADCI | `0x839B` | ← XDATA `0x2E03` bit 4 |
| `0x1C` | PWDN_ADCQ | `0x839C` | (nieodwzorowany w firmware) |
| `0x1D` | MPLL_ADC_DIV_SEL | `0x839D` | ← XDATA `0x2E61` |
| `0x1E` | OP_DCR_EN (usuwanie DC) | `0x839E` | |
| `0x2B` | MIXER_IQ_SWAP_MODE | `0x83AB` | ← XDATA `0x215E` bit 0 |
| `0x30` | MIXER_IQ_DBG_SEL | `0x83B0` | ← XDATA `0x215E` bit 4 |

FC i FS firmware kopiuje do swoich zmiennych (`0x8239`, `0x824D`) i stosuje dopiero przy starcie maszyny stanów, więc zmiana samych parametrów DSP bez restartu demodulatora nic nie da.

**Sprzęt ma dwa ADC (I i Q):** osobne `PWDN_ADCI`/`PWDN_ADCQ` i `ADC_PGA_GAIN_I/Q`. Przy tunerze low-IF (MxL5007T) używany jest pewnie jeden.

**Blok ADC = RIU `0x1128`** ✅ (fiz. `0x1F225000`, czytelny z MIPS; zawartość po `tuner_init` dokładnie jak zapisy `InitClkgen`). To ten sam IP „ADCPLL” co w `MSB1233C.c` (ok. linii 1680–1810, komentarze bitów). Strona XDATA `0x2E` **nie** jest tym bankiem (czyta same zera); co oznacza `0x2E03`/`0x2E61` w firmware, nie wiadomo ❓.

`INTERN_DVBT_InitClkgen` (kolejność zapisów RIU): `0x101E39`=0, `0x112002`=`0x52`, `0x112003`=0, `0x112060`/`61`/`64`=0, `0x112065`=`0x0C`, `0x112066`=0, `0x112067`=`0x0C`, `0x11286A`/`6B`=`0x04`/`0x06`, `0x112868`/`69`=0, `0x112866`/`67`=`0x00`/`0x24`, `0x112860`/`61`=`0x01`/`0x4C`, `0x112802`/`03`=`0x40`/`0x04`, `0x112816`/`17`=`0x05`/`0x05`, `0x112818`=`0x03`→0, `0x112819`=0, `0x112879`=`0x10`, `0x112840`/`41`=0, `0x11286C`=0, `0x10331E`=`0x10`, `0x103480`=0, `0x103314`=0, `0x111F22`=1, `0x111F02`–`0x111F19`=0 (zegary demodulatora włączone), `0x103300`/`01`=`0x11`/`0x05`, `0x103308`/`09`=`0x01`/0, `0x111F1C`/`1D`=1/1, `0x10331A`/`1B`=1/1, `0x101E9E`=0, `0x101EA0`/`A1`=0, `0x103C0E`=1.

`HAL_DMD_ADC_IQ_Switch(tryb)`: `0x112876` = `0`/`1`/`2` (wybór wejścia ADC), `0x1128A1` = 0 lub 1. Tryb wybiera, który pad/kanał (I czy Q) dostaje sygnał.

`INTERN_DVBT_Power_On_Initialization`: `InitClkgen`, `ADC_IQ_Switch`, ładowanie firmware'u (`0x103480`–`0x10348C`), `0x101E3E`=2, `Reset`, potem przez skrzynkę `0x2200`=`0xFF`, odczyt-zapis `0x2204`, `DSPReg_Init`. `INTERN_DVBT_Active` to tylko `Cmd_Packet_Send(…, 1)`.

#### ✅ `0x2098` to surowe wyjście ADC I (test na płycie 2026-11-03)

Warunki: antena, MBoot `tuner_init`, Linux z USB, tuner na 474 MHz (`rfscan 474 474 4 300 1 30`). Pomiar: `dmdraw 20000` (bezpośredni odczyt, niżej). Każda zmiana cofnięta przed następną; firmware 8051 odpowiadał przez cały czas. Skrypty na płycie: `/root/riub.sh` (zrzut banku RIU przez `devmem`), `/root/adct.sh PHYS VAL OPIS` (zmiana bajtu RIU, pomiar, przywrócenie).

| Zmiana (bank `0x1128`, fiz. adres) | `0x2098` |
|---|---|
| bez zmian | wariancja 1400–1440, średnia ≈ `0x80` |
| `0x112818` bit 0 = 1 (power-down ADC I), `0x1F225030` | **stałe `0x89`**, wariancja 0 |
| `0x112818` bit 1 = 1 (power-down ADC Q) | bez zmian |
| `0x112802` bit 0 / bit 1 (w MSB1233C power-down Q/I) | bez zmian (w Kronusie to nie power-down) |
| `0x112803` bit 4 = 1 (zamiana ADC I↔Q), `0x1F225005` | stałe `0x80` |
| `0x112802` = `0x00` (IMUX = 0, wejście kalibracyjne), `0x1F225004` | wariancja 0,2 (sam szum kwantyzacji) |
| `0x112802` = `0x10` (IMUX = 1, wejście VIF) | stałe `0xFF` (nasycenie) |
| `0x112876` = 1, `0x1F2250EC` (było 2) | stałe `0x93` |
| `0x112876` = 0 | bez zmian |
| XDATA `0x215E` = `0x10` / `0x01` (debug sel / zamiana I/Q miksera cyfrowego) | bez zmian |

Wniosek: `0x2098` zależy tylko od ADC I i od jego multipleksera wejścia, a nie od miksera cyfrowego. Stała wartość na wejściu ADC daje stałą wartość `0x2098` (`0xFF`, `0x89`, `0x93`). Za mikserem cyfrowym stała zamieniłaby się w sinusoidę o częstotliwości IF, a za usuwaniem DC wróciłaby do `0x80`. To jest więc **surowa próbka ADC I** (pewnie 8 starszych bitów), przed mikserem i usuwaniem DC. Kanał Q (ADC Q) w tej płycie nie niesie sygnału, bo tuner low-IF podaje sygnał tylko na wejście I.

Słowo 1 ADC (`0x112802`/`03` = `0x0440`) ma układ z MSB1233C: IMUX [6:4] = 4 i QMUX [10:8] = 4 („DVB z padów”), bit 12 = zamiana I/Q. Power-down ADC I/Q to słowo `0x0C` (`0x112818` bity 0/1), jak w drugim komentarzu MSB1233C. MBoot zostawia `ADC_IQ_Switch` w trybie 2 (`0x112876` = 2, `0x1128A1` = 1).

#### Bezpośredni dostęp MIPS do rejestrów demodulatora ✅ (2026-11-03)

- **Strona XDATA `0x20` = bank RIU `0x1120`** (fiz. `0x1F224000`): XDATA `0x2002` = `0x52`, `0x2065`/`0x2067` = `0x0C`, czyli zapisy `InitClkgen` do `0x112002`/`0x112065`/`0x112067`. `0x2098` = fiz. `0x1F224130` (młodszy bajt słowa `0x112098`), `0x2096`/`0x2097` = `0x1F22412C`.
- **Przełącznik właściciela szyny: RIU `0x101E39`** (fiz. `0x1F203C71`; `0xBF203C70` = `0x0100` po `tuner_init`). `1` = rejestry demodulatora należą do 8051 (MIPS czyta z banku `0x1120` same zera), `0` = dostęp ma MIPS. `HAL_DMD_ADC_IQ_Switch` zapisuje go, ustawia 0 na czas swoich zapisów i przywraca. Bank ADC `0x1128` i skrzynka `0x1126` są dostępne przy obu ustawieniach.
- `tools/board/dmdraw.c` (`dmdraw N [plik]`): ustawia `0x101E39` = 0, czyta N razy 16-bitowe słowa `0x112096` i `0x112098`, przywraca `0x101E39`, wypisuje tempo, średnią, wariancję i autokorelację `0x2098`. Plik: 4 bajty na próbkę (`0x2096`, `0x2097`, `0x2098`, `0x2099`).
- Tempo: **ok. 260 ns na próbkę** (dwa odczyty 16-bit), czyli ok. 3,8 MS/s, 17 razy szybciej niż skrzynka (4,4 µs). Wariancja taka sama jak przez skrzynkę. Autokorelacja z przesunięciem 1 ≈ −0,12 (przez skrzynkę ≈ 0,01): kolejne próbki są już trochę zależne. Odstępy między odczytami nie są jednak równe (przerwania, magistrala), a ADC pracuje dużo szybciej, więc to nadal nie jest strumień próbek do FFT ❓. To trop na później.
- Na czas odczytu 8051 traci dostęp do rejestrów demodulatora. Przy nieaktywnym demodulatorze nie było problemów; przy pracującym (`SetActive`) trzeba uważać ❓.

#### ✅ Próbkowanie ADC z MIPS i widmo pośredniej (2026-11-03/04)

> **Korekta 2026-11-04:** prążek opisany niżej jako „stacja FM 96,03 MHz” to wielokrotność 6 MHz z grzebienia związanego z kwarcem tunera, a nie radio. Prążek „4,495 MHz” to przeciek LO tunera, a rzeczywista IF to ok. 4,5953 MHz. Patrz [falszywe_sygnaly.md](falszywe_sygnaly.md). Widmo na żywo też jest już zrobione: `tools/board/specan.c`, [analizator_widma.md](analizator_widma.md).

`tools/board/dmdts.c` (`dmdts N plik`): przy każdej próbce zapisuje licznik CP0 Count (`rdhwr $2`, 276 MHz = 3,6 ns) i słowo `0x112098`. Format: nagłówek 8 B (u32 częstotliwość licznika, u32 N), potem rekordy 8 B (u32 Count, u16 słowo, u16 wypełnienie). Bufor jest wcześniej wypełniony i zablokowany (`mlockall`). Bez tego co 512 rekordów (strona 4 KB) wypadała dziura ok. 20 µs na błąd strony.

**Regularność (65 536 próbek, 7,3 ms):** odstęp 30 albo 31 taktów licznika, czyli **ok. 110 ns, fs ≈ 9,09 MS/s, rozrzut ±1 takt (≈ 2 ns)**. W całym pliku jest jedna dłuższa przerwa (ok. 130 µs, pewnie przerwanie zegara) i kilkanaście przerw 0,3–1 µs. Odcinki bez przerw mają dziesiątki tysięcy próbek. Rozrzut 2 ns wystarcza dla pośredniej do ok. 9 MHz.

**Test z FM** (`rfscan F F 1 300 1 30`, potem `dmdts 65536`, F = 96…100 MHz; pliki `binaries/adc_capture/fm96…fm100.bin`, `ts474.bin`; wykres `widma_fm96-100_474.png`; analiza `tools/adc_spectrum.py`, Welch 4096, odcinki dzielone przy przerwach > 1 µs). Pobieranie z płyty: `tools/serial/uart_get.py ZDALNY LOKALNY` (gzip + base64 + md5).

| Strojenie | Prążek stacji | RF = F − 4,57 + prążek |
|---|---|---|
| 97 MHz | 3,592 MHz, +20 dB | 96,02 MHz |
| 98 MHz | 2,594 MHz, +22 dB | 96,02 MHz |
| 99 MHz | 1,595 MHz, +22 dB | 96,03 MHz |
| 100 MHz | 0,60 MHz | 96,03 MHz |
| 96 MHz | ok. 4,49 MHz (odbicie od fs/2) | |

Prążek przesuwa się dokładnie o 1 MHz na 1 MHz strojenia, więc to prawdziwy sygnał radiowy: **stacja na ok. 96,03 MHz**. Widmo nie jest odwrócone (IF = RF − F + 4,57). Widać też słabsze szersze garby (inne stacje FM) i zbocze filtra pośredniej tunera poniżej ok. 0,8 MHz. Prążek 4,495 MHz jest we wszystkich plikach, także na 474 MHz, więc to zakłócenie sprzętowe. DVB-T na 474 MHz daje płaskie widmo.

Ograniczenia:
- fs ≈ 9,09 MS/s, a pasmo pośredniej to 0,57–8,57 MHz, więc widmo jest **złożone wokół 4,54 MHz**: każde miejsce na wykresie to dwie częstotliwości RF (IF i fs − IF). Węższy filtr tunera (6 MHz) tylko trochę pomaga.
- Odczyt idzie przez magistralę RIU i zależy od CPU, a nie od zegara ADC. Dłuższe nagrania mają przerwy na przerwania (w Linuksie co kilka ms).
- Z MIPS tempa nie zwiększymy (ok. 110 ns na odczyt RIU). 8051 byłby wolniejszy, ma ok. 1 KB RAM i przesyła 4,4 µs/bajt, więc nie daje ciągłego strumienia.

Tryb zrzutu ADC do SRAM demodulatora (prawdziwe kolejne próbki z pełną częstotliwością ADC): w źródłach i bibliotekach tego drzewa nie ma nic poza multiplekserem zegara SRAM w MSB1233C (`reg_ckg_dvbtm_sram_adc_t22x`, TOP `0x16` = `0x0101` przy `SIM_ADC_DUMP`). Rejestru wyzwalającego zrzut i sposobu odczytu SRAM nie znaleziono ❓.

#### Podsumowanie i co dalej (SDR z ADC, stan 2026-11-04)

1. **Próbkowanie z MIPS działa** ✅: ok. 110 ns na próbkę (9,09 MS/s), rozrzut ok. 2 ns. Dziury co 512 próbek były błędami stron przy pierwszym zapisie do bufora, nie przerwaniami; po wypełnieniu i zablokowaniu bufora (`mlockall`) w 7 ms nagrania zostaje jedna dłuższa przerwa (ok. 130 µs).
2. **Widmo pokazuje prawdziwe sygnały** ✅: stacja FM ok. 96,03 MHz przesuwa się o 1 MHz na 1 MHz strojenia, widać słabsze stacje i zbocze filtra pośredniej, DVB-T daje płaskie widmo.
3. **Ograniczenia:** pasmo pośredniej 0,57–8,57 MHz złożone wokół 4,54 MHz (każde miejsce na wykresie to dwie częstotliwości RF); w dłuższych nagraniach co kilka ms przerwa na przerwanie systemu.
4. **Kod na 8051 nie pomoże:** wolniejszy od MIPS (kilkanaście taktów na próbkę, ok. 0,3 µs lub więcej), ok. 1 KB RAM, przesył przez skrzynkę 4,4 µs/bajt, więc ani szybciej, ani w sposób ciągły. Jedyna zaleta (równe odstępy bez systemu operacyjnego) da się uzyskać też na MIPS.
5. **Zrzut ADC do SRAM demodulatora** ❓: w źródłach i bibliotekach tylko multiplekser zegara SRAM z MSB1233C; rejestru wyzwalającego i sposobu odczytu brak. Dalej tylko ślepe przestawianie rejestrów (ryzyko zawieszenia płyty), nie robione bez zgody.
6. **Prawdziwe ciągłe SDR** daje tylko zewnętrzny ADC na linii pośredniej (punkt 6 w „Możliwe sposoby dostępu”).

Następne kroki do wyboru:
- nagrania bez przerw: moduł jądra z wyłączonymi przerwaniami albo drugi VPE tylko do próbkowania;
- demodulacja FM z nagrania (usłyszenie stacji), co wymaga dłuższych nagrań;
- widmo na żywo z FFT na ekranie, jak w `rfscan`.

#### Poszukiwanie sprzętowego strumienia ADC → DRAM (2026-11-04)

Tropy z innych układów MStara (źródła i biblioteki w `mstar-mboot-master`):
- `MIU_CLIENT_DEMOD_ADCDMA_W`: klient MIU „zapis ADC demodulatora przez DMA” (`libnos.a` układów celtics, clippers, keres, kris, madison, monaco, muji, napoli; wszystkie nowsze niż Kronus). W bibliotekach K1 brak nazw klientów MIU, więc nie wiadomo, czy Kronus go ma ❓.
- `sboot/src/kaiserin/script_clk_gen.S`: `reg_ckg_demod_dma@0x0a[11:8] : ADC_CLK` w banku `0x1033`, czyli bajt `0x103315`. W Kronusie `0x103315` = `0x11` (domyślne, zegar wyłączony); `InitClkgen` go nie rusza, a `INTERN_DVBT_Exit` zapisuje mu `0x01` razem z innymi zegarami DVB-T. Włączenie (`0x00`) niczego widocznego nie zmieniło w bankach `0x1121`–`0x1127`.
- Nazwy banków z `c_riubase.h` innych układów: `DEMOD_0`…`DEMOD_7` = `0x1120`–`0x1127`, `CLKGEN_DMD` = `0x111F`, `DMD_ANA_MISC` = `0x1128` (ADC), `DMDMCU51_IF` = `0x103480`, `DEMOD` = `0x110500`.
- Sterowniki demodulatora K1 (`libdrvDEMOD.a`: DVB-T, DVB-C, ATSC, wspólne) piszą RIU tylko w bankach `0x101E`, `0x1033`, `0x1034`, `0x103C`, `0x111F`, `0x1120`, `0x1126`, `0x1128`. Żadnego DMA ani adresu DRAM.

Zrzut banków z MIPS (`tools/board/dmdbank.c`, właściciel szyny = MIPS, 8 przebiegów, `*` = zmienne słowo):
- `0x1120`: patrz wyżej (`0x2098` zmienne, `0x20C0`–`0x20FF` zmienne firmware'u).
- `0x1121`–`0x1124`, `0x1127`: same zera. Strony XDATA `0x21`–`0x25` **nie** są tymi bankami (np. XDATA `0x2100` = `ff ff 3f 80`, a RIU `0x1121` = 0; RIU `0x1125` ma `0x112502` = `0x0800`, `0x112516` = `0x0001`, a XDATA `0x2500` = 0). Rejestry DSP demodulatora z tych stron nie są widoczne z MIPS ❓.
- `0x1126`: skrzynka, blok `0x40` B powtórzony 4 razy.
- `0x111F`: `0x111F08` = `0x0101` (zegar nieużywany przez sterownik, wyłączony), `0x111F1C` = `0x0101`, `0x111F1E`/`0x111F20`/`0x111F22` = 1.
- `0x1033`: `0x103300` = `0x0511`, `0x10330C` = `0x0101`, `0x103314` = `0x1100`, `0x103316`/`0x10331A` = `0x0101`, `0x10331E` = `0x0010`; `0x103380`–`0x1033DF` wyglądają na losowe dane (klucze CA?).
- `0x1034`: `0x103480` = `0x0100`, `0x103484` = `0x5E6E` (ładowanie firmware'u 8051), `0x1034C0`–`0x1034E0` URDMA.
- Banki `0x1100`–`0x1115` czytają się bez problemu (niezerowe m.in. `0x1102`, `0x1103`, `0x1104`, `0x1106`, `0x1107`, `0x110D`, `0x1113`; `0x1114`/`0x1115` same `0x5802`).
- ⚠ **Odczyt banku `0x1116` zawiesza płytę** (potrzebny reset zasilania). Banków `0x1117`–`0x111E` nie sprawdzano.

Wniosek: w dostępnych materiałach nie ma sprzętowej ścieżki ADC → DRAM dla Kronusa. Jeśli istnieje (`demod_dma`), jej rejestry trzeba by szukać na ślepo.

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
