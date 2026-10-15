# Jak oryginalny system szuka kanałów (DVB-T)

Analiza statyczna oryginalnego programu (eCos, `decompressed.bin`, projekt Ghidry `binaries/ghidra_stock`, pełna dekompilacja `all.c`). 🔍 Z kodu, bez śledzenia na żywo. Adresy funkcji dotyczą oryginału.

> **Uwaga praktyczna (2026-10-14):** w Polsce naziemna telewizja nadaje od 2022 r. tylko w **DVB-T2 / HEVC**. Demodulator MSD7818 obsługuje wyłącznie DVB-T (i DVB-C), więc oryginalny skan nie znajduje niczego, ani TV, ani radia, nawet przy dobrej antenie. Sygnały widoczne w `rfscan` (ok. 474 i 666 MHz) to najpewniej multipleksy DVB-T2: tuner i pomiar mocy je widzą, ale demodulator ich nie zdekoduje.

## W skrócie

1. Menu „szukaj kanałów” wybiera **zakres kanałów dla kraju** (tabela w kodzie) i przelicza numery kanałów na częstotliwości.
2. Osobne **zadanie skanujące** (`ChScan_Task`) przechodzi kanał po kanale:
   1. stroi tuner i demodulator;
   2. czeka na synchronizację (lock), najdłużej 5 s;
   3. jeśli jest sygnał, zgłasza „kanał znaleziony”;
   4. przechodzi do następnej częstotliwości.
3. Po zgłoszeniu aplikacja czyta z multipleksu tablice DVB (**PAT → PMT, SDT, NIT**), czyli listę programów, PID-y audio i wideo, nazwy stacji i numerację LCN. Programy zapisuje do bazy (`MApp_Scan_AddOneDTVPchVchs`).

## 1. Zakres i częstotliwości (`MApp_MenuChannel_FullScan_Start`, `0x8015bc48`)

- Kraj (region) zwraca `0x8015b13c`. Dla regionu `0x10` skan zaczyna się od zadanej ręcznie częstotliwości.
- Najwyższy kanał: `0x8017d438(region)`, najniższy: `0x8017d4a0(region)`, np. dla regionów 0, 1, 3, 4: kanały **5…70**.
- Numer kanału → częstotliwość (kHz) w `0x8017d508`. Dla Europy (regiony 0, 1, 4, 5, 9, 0xC):
  - **VHF**, kanały 5–12: `f = kanał × 7000 + 142500` (kan. 5 = 177,5 MHz, szerokość 7 MHz);
  - **UHF**, kanały 21–69: `f = 306000 + kanał × 8000` (kan. 21 = 474 MHz, szerokość 8 MHz).

  Inne regiony mają inne tabele: np. region 2 to kanały 24–35 co 6 MHz od 532,5 MHz, a 3 i 6 mają tablice w pamięci.
- Odwrotnie, częstotliwość → numer kanału: `0x8017d8a4`.
- Szerokość pasma przekazana do skanu: `(szerokość_kHz / 1000) − 6`, czyli 0 = 6 MHz, 1 = 7 MHz, 2 = 8 MHz.
- Parametry skanu zapisane w strukturze (`MApi_ChanScan_Start`, `0x8017e41c`, kopiuje 0x38 bajtów). Jest tam też symbol rate `0x1ADB` = 6875 kS/s, używany tylko w DVB-C.

**Druga ścieżka, `MApp_MenuChannel_AutoScan_Start`:** bierze listę częstotliwości z tablicy **NIT** (lista nadajników z sieci), sortuje ją i dokłada domyślne 243 MHz. Ma parametry DVB-C (QAM, 6875 kS/s), a log startu mówi „DVBC System Memory Usage”. Oprogramowanie jest wspólne dla DVB-T i DVB-C, a wybór zależy od konfiguracji.

## 2. Zadanie skanujące: maszyna stanów (`0x8017e238`)

Zadanie czeka na zdarzenie, a potem wykonuje funkcję bieżącego stanu, która zwraca następny stan. W stanie „czekam na lock” (2) śpi 30 ms między sprawdzeniami.

| Stan | Funkcja | Co robi | Dalej |
|---|---|---|---|
| 0 Init | `0x8017de3c` | pierwsza częstotliwość (z tabeli kanałów albo z listy NIT) | 1 |
| 1 Tune2RF | `0x8017dedc` | strojenie (`_ChScan_Tune2RF %dKHz`), ustawia limit czasu (`0x8017eb00` → **5000 ms**) | 2, albo 4 przy błędzie |
| 2 WaitLock | `0x8017dfa4` | co 30 ms pyta demodulator o lock; „CH %d found, LockTime %d” → zdarzenie **4** (znaleziony); po limicie „Timeout” | 3 |
| 3 SetTPS / następny | `0x8017e068` | kolejny kanał (`0x8017dbb4`) i jego częstotliwość, nowa szerokość pasma; za ostatnim „Last RF channel” → zdarzenie `0x10` (koniec) | 1 albo 4 |
| 4 Idle | `0x8017e20c` | koniec | — |

## 3. Strojenie i lock

**Strojenie** (`0x8017ea40` → `0x8017fde8`):
1. `0x801803e0(częstotliwość, szerokość)`: tuner MxL5007T przez I2C. To ta sama sekwencja co w [mxl5007t.md](mxl5007t.md): tabela init, `N = f_MHz × 64`, pasmo filtra 6/7/8 MHz.
2. `0x800c0e30(1|2|3, 0, 0)`: demodulator DVB-T, szerokość pasma 6 / 7 / 8 MHz.
3. `0x800c0e6c(1)`: restart demodulatora (start akwizycji). Wynik „Tuner Success”.

**Sprawdzenie locka** (`0x8017eb10` → `0x8017fbc8` → `0x800c0f70`, czyli `GetLock` demodulatora). Demodulator to wewnętrzny 8051 opisany w [demodulator_if.md](demodulator_if.md):

| Wynik | Znaczenie | Reakcja skanu |
|---|---|---|
| 0 | **lock** (jest sygnał DVB-T) | kanał znaleziony, limit dalej 5000 ms |
| 3 | **brak kanału** (demodulator szybko stwierdził, że nic tu nie ma) | limit skracany do **2000 ms** |
| inne | jeszcze szuka | czekamy dalej |

Pusty kanał kosztuje więc ok. 2 s, a kanał z sygnałem ok. 5 s plus czytanie tablic. Pełny skan Europy (kanały 5–12 i 21–69, 57 kanałów) trwa ok. 2 minut.

## 3a. Skąd demodulator „wie”, że jest sygnał (`MDrv_DMD_DVBT_GetLock`, `0x800c0f70`) 🔍

Wszystkie odczyty idą przez skrzynkę pocztową do firmware'u 8051 demodulatora (`0x800e7898(adres_XDATA, &bajt)`, to samo co nasze `dmdmbx r`, zob. [demodulator_if.md](demodulator_if.md)). Sprawdzenia flag (`0x800c2a34(typ)`):

| Typ | XDATA | Warunek | Znaczenie (wg użycia) |
|---|---|---|---|
| 1 | `0x23E0` | **== `0x0B`** | stan maszyny stanów 8051 = 11, **pełny lock** (koniec akwizycji, dane płyną) |
| 2 | `0x232C` | bit 1 | ❓ |
| 3 | `0x2222` | bit 1 | ❓ |
| 4 | `0x2145` | bit 0 | ❓ |
| 5 | `0x212F` | bit 0 | ❓ |
| 6 | `0x24CF` | bit 4 | ❓ |
| 7 | `0x20C0` | bit 3 | „coś znalazł”: wstępna synchronizacja sygnału DVB-T (wydłuża czekanie) ❓ |

W firmware 8051 stan `0x23E0` ustawia funkcja `CODE:0x0D9D` (zapisuje stan i zeruje licznik czasu stanu `0x23F0`). Wartość jest liczona, czyli kolejne kroki akwizycji (AGC, wykrycie trybu/guard interval, synchronizacja czasu i częstotliwości, TPS, korekcja błędów…) aż do stanu 11.

Logika `GetLock` (`0x800c2d30`, parametry `1500` i `6000` ms z `0x8063E152`/`0x8063E150`):
1. **`0x23E0 == 0x0B` → lock (wynik 0).** Przy okazji liczy jakość sygnału (`0x800c2b14`, liczba zmiennoprzecinkowa z zakresu 1e-8…0,1, czyli najpewniej **BER**). Wynik jest wygładzany: `nowa = 0,9·stara + 0,1·pomiar`.
2. Utrata locka krócej niż **1 s** temu: dalej „lock” (histereza, żeby obraz nie gasł przy chwilowym spadku).
3. Brak locka:
   - jeśli kiedykolwiek od restartu pojawił się bit 3 w `0x20C0` („coś jak DVB-T”), czeka do **6000 ms**;
   - jeśli nie, poddaje się po **1500 ms**.

   Po upływie czasu zwraca **3 = brak kanału**, a przed nim **1 = szuka dalej**.

Wynik: pusta częstotliwość jest odrzucana po ok. 1,5 s. Częstotliwość, na której demodulator zaczyna coś łapać, dostaje do 6 s na pełną synchronizację. Do sprawdzenia na płycie z anteną: `dmdmbx r 0x23e0 1` i `dmdmbx r 0x20c0 1` przy dostrojeniu do znanego multipleksu (np. 474 MHz) ❓.

## 4. Po znalezieniu: tablice DVB (SI/PSI)

Zdarzenie „znaleziony” odbiera aplikacja. Parser SI (`../../../DVBT/app/si/apiSI.c`, funkcje `SI_DVB_PMT_CScan_Parse`, `SI_DVB_NIT_CScan_Parse`, `SI_DVB_SDT_Monitor_Parse`, wpisy `########pat:%d,%d`) przez demultiplekser (DMX) czyta:
- **PAT** (PID 0): lista programów i PID-ów ich tablic PMT;
- **PMT**: PID-y wideo i audio (z kodekiem, językiem) każdego programu;
- **SDT**: nazwy programów i dostawców;
- **NIT**: numery LCN („test LCN”) i lista częstotliwości sieci.

Następnie `MApp_Scan_AddOneDTVPchVchs` dodaje „fizyczny kanał” i jego „kanały wirtualne” (programy) do bazy (`MApi_DB_CM_AddProgram`), a `MApp_SavePowerSetting` / `MApp_CheckFlash` zapisują ją do flasha.

## Związek z naszymi narzędziami

- `tools/board/rfscan` robi coś podobnego do kroku 1 i 2, ale bez demodulacji: stroi MxL5007T i mierzy moc z próbek sygnału w XDATA `0x2098`. Pokazuje *gdzie jest sygnał*, ale nie *czy to DVB-T*.
- Pełny skan pod Linuksem wymagałby uruchomienia demodulatora (wgranie firmware'u 8051 DVB-T, ustawienie pasma, odczyt locka przez skrzynkę pocztową), a do odczytu tablic także demultipleksera i strumienia TS. To duży, osobny projekt.

## Radio 🔍

**Radio w DVB-T to zwykłe usługi w tych samych multipleksach co telewizja.** Nie ma osobnego skanowania: ten sam skan znajduje multipleks, a podział TV/radio wynika z tablic DVB:
- **SDT** podaje dla każdej usługi `service_type`: `0x01` telewizja, `0x02` **radio cyfrowe**, inne to dane, HD itd.;
- **PMT** radia ma tylko strumień audio, bez wideo (komunikaty `TS stream is RADIO, no video format.`, `Prog#. Radio program, PCR = %d.`).

W `MApp_Scan_AddOneDTVPchVchs` (`0x8015c654`, pętla po usługach multipleksu) jest licznik dla każdego typu, którym potem menu pokazuje „TV / Radio”:

| Typ usługi (bajt `local_ec`) | Licznik | Znaczenie |
|---|---|---|
| 1, 7 | `0x80664CA4` | **TV** (7 to najpewniej TV HD / zaawansowany kodek) |
| 2 | `0x80664CA0` | **radio** |
| 4, 5, 0x0C | `0x80664C9C`, `0x80664CBC`, `0x80664C98` | inne (dane, NVOD itp.) ❓ |

Baza programów ma osobną listę i kolejność dla radia (`BASEADDRESS_PR_CURRENTORDER_RADIO`, `m_wCurrentOrderOfRadio`). Przy odtwarzaniu radia oprogramowanie dekoduje tylko audio i pokazuje nieruchomy obraz („show if frame radio”, `Radio Decode Ready`).

**Radio FM (analogowe) nie jest obsługiwane.** Tuner MxL5007T obejmuje pasmo 88–108 MHz, ale demodulator w układzie umie tylko DVB-T/DVB-C. Tryb `FM-RADIO` w audio MStara (SIF, demodulacja analogowa w DSP) ma w tym oprogramowaniu pusty kod (typ DSP `0xB4` bez firmware'u). Programowej demodulacji FM nie zrobimy przez nasz rejestr próbek `0x2098`, bo odczytywany przez skrzynkę pocztową daje za mało próbek na sekundę.

Brak radia w Twoim skanie oznacza więc, że nie złapał **żadnego** multipleksu: radio DVB-T leży w tych samych multipleksach co telewizja.
