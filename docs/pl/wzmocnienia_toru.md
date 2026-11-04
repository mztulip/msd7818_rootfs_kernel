# Regulacja wzmocnień w torze odbiorczym

Stan 2026-11-04. Pomiary na płycie z anteną z kawałka kabla na ziemi, dane: `binaries/spurs/wzmocnienia_test.txt`. Dla każdego ustawienia: szum (mediana binów przy F = 94 MHz, `specan -R -a 8`), prążek 96 MHz z tej samej próbki oraz pilot 19 kHz stacji 101,0 MHz (mediana z 23 okien po 0,5 s). Kolejne przebiegi różnią się o ok. ±1 dB (prążek/szum) i ±2 dB (pilot).

Oznaczenia: ✅ zmierzone na płycie, 🔍 wynika z kodu lub schematu, ❓ hipoteza.

## Stopnie regulacji

| Stopień | Sterowanie | Stan |
|---|---|---|
| LNA i RF w MxL5007T | brak rejestru w sterowniku Linuksa; opis znaleziony w sieci podaje, że układ reguluje RF sam, bez demodulatora ❓ | nie mamy sterowania |
| IF w MxL5007T | wejście napięciowe `VAGC`, na schemacie z pinu `IF_AGC` SoC 🔍 | poziomu PWM nie umiemy jeszcze ustawiać ❓ |
| Pad `IF_AGC` w SoC | RIU `0x101E3A` bit 0 (fiz. `0x1F203C74`), `HAL_DMD_IFAGC_Tristate` 🔍 | przełączenie bez efektu ✅ |
| Pad `RF_AGC` w SoC | ten sam rejestr, bit 1 🔍 | przełączenie bez efektu ✅ |
| **Wzmocnienie ADC w SoC** | RIU `0x112816` (fiz. `0x1F22502C`), po 4 bity na kanał, po `tuner_init` `0x0505` | **działa ✅** |

## Pomiar wzmocnienia ADC

Mniejsza wartość to większe wzmocnienie.

| Rejestr | Zakres próbek | Szum dB | Prążek 96 MHz dB | Prążek − szum dB | Pilot dB |
|---|---|---|---|---|---|
| `0x0000` | 0..255 (przesterowanie) | −39,4 | −18,0 | 21,4 | +24 |
| `0x0101` | 6..247 | −41,2 | −18,9 | 22,3 | +22 |
| `0x0303` | 23..224 | −43,5 | −21,9 | 21,6 | +24 |
| `0x0505` (obecna) | 53..203 | −45,6 | −23,5 | 22,1 | +25 |
| `0x0707` | 63..195 | −47,1 | −25,8 | 21,3 | +25 |
| `0x0909` | 72..191 | −48,4 | −26,6 | 21,8 | +25 |
| `0x0B0B` | 80..191 | −49,7 | −27,2 | 22,5 | +24 |
| `0x0F0F` | 92..191 | −51,1 | −29,8 | 21,3 | +27 |

**Wniosek ✅:** zakres ok. 12 dB, ale szum i sygnał zmieniają się razem. Stosunek sygnału do szumu nie zależy od tego wzmocnienia, więc szum powstaje przed ADC: w tunerze albo w sygnale z anteny. Większe wzmocnienie nie poprawi odbioru słabych stacji; `0x0000` jeszcze dokłada przesterowanie. Regulacja przydaje się tylko do uniknięcia przesterowania silnym sygnałem. Przy wysokich wartościach środek zakresu odjeżdża od 128 (`0x0F0F`: ok. 141) ❓.

W `specan`: klawisze `[` i `]`, stan w napisie `ADC` na ekranie. Przy wyjściu wraca poprzednia wartość ✅ (po `]]` z `0x0505` RX wzrósł o ok. 2 dB, po wyjściu znów `0x0505`). Przy radiu sprawdzone tylko z logu, napisu na ekranie nie oglądano.

## Filtr tunera i położenie IF

Dane: `binaries/spurs/filtr_tunera_test.txt`, `strojenie_if_test.txt`. Pilot stacji, średnia z ok. 35 okien po 0,5 s, dwa powtórzenia.

| Stacja MHz | 8 MHz | 7 MHz | 6 MHz | 6 MHz, IF o 1 MHz niżej |
|---|---|---|---|---|
| 101,0 | +25,9 / +26,4 | +26,0 / +25,9 | +26,9 / +26,2 | +15,2 / +17,9 |
| 99,9 | +18,0 / +16,3 | +18,7 / +16,5 | +18,7 / +20,3 | +5,2 / +3,5 |
| 91,1 | +6,2 / +8,4 | +7,3 / +8,0 | +2,7 / +7,7 | −0,6 / +0,6 |
| Średnia | +16,9 | +17,1 | +17,1 | +7,0 |

- **Węższy filtr nie daje mierzalnego zysku ✅.** Domyślnie zostaje 8 MHz; klawisz `b` przełącza filtr podczas słuchania, a przemiatanie zawsze używa 8 MHz. Obliczone wcześniej 4,6 dB strat na składaniu szumu nie przełożyły się na zysk z węższego filtru; przyczyna nieustalona ❓.
- Filtr 6 MHz traci ok. 10 dB, gdy IF spada do ok. 1,6 MHz, czyli do jego krawędzi ✅. Dlatego radio stroi teraz do najbliższego pełnego MHz ok. 2 MHz nad stacją (IF 2,1–3,1 MHz).
- **Na filtrze 8 MHz położenie IF nie ma znaczenia ✅:** 101,0 MHz przy strojeniu 102, 103 i 104 MHz ma od +26,5 do +28,3 dB.
- **Pomiary z różnych chwil są słabo porównywalne ✅:** 91,1 MHz przy tym samym strojeniu 94 MHz miało najpierw +6,2 i +8,4 dB, a później +0,5 i +1,3 dB. Sam sygnał tej stacji zmienia się w czasie (słaba antena? fading? ❓).

## Pomiar bez anteny (2026-11-04)

Dane: `binaries/spurs/bez_anteny_szum.txt`. Antena odłączona przez użytkownika, porównanie z pomiarami z anteną z tego samego dnia.

| Co | Z anteną | Bez anteny |
|---|---|---|
| Szum przy F = 94 MHz | −45,5 dB | −45,7 dB |
| Prążek 96 MHz (grzebień) | ok. −23,5 dB | −23,7 dB |
| Prążek 216 MHz (grzebień) | −3,8 dB | −3,6 dB |
| Pilot 101,0 MHz | ok. +26 dB | −0,7 dB (brak) |
| Pilot 99,9 MHz | ok. +17 dB | −0,4 dB (brak) |
| Prążek 742,5 MHz (HDMI) | ok. −18 dB | poniżej progu 12 dB nad szumem |

- **Szum −45 dB to szum własny toru, nie sygnał z anteny ✅.** Sygnał z anteny jest od niego słabszy, więc każdy dB więcej z anteny to prawie cały dB lepszego stosunku sygnału do szumu. Dlatego spodziewany zysk daje lepsza antena albo wzmacniacz niskoszumny przed tunerem ❓ (jeszcze nie próbowane).
- **Grzebień 6 MHz nie wchodzi przez antenę ✅,** co zgadza się ze związaniem z kwarcem tunera.
- **Stacje znikają ✅,** więc to nie były artefakty.
- **HDMI 742,5 MHz wchodzi głównie przez antenę ✅:** kabel antenowy łapie promieniowanie HDMI (możliwe, że z kabla HDMI ❓). Inne prążki HDMI w tym pomiarze nie były sprawdzane.

## Co dalej

- Ustawienie poziomu `IF_AGC` (PWM) wymaga znalezienia rejestru. Skoro większość szumu jest przed ADC, zysk w stosunku sygnału do szumu jest wątpliwy ❓.
- Największy spodziewany zysk: lepsza antena ❓.

Uwaga: dwie jednoczesne kopie `specan` nawzajem resetują DSP i nie potrafią wtedy wyjść nawet po `SIGTERM` (zostaje `kill -9`, a potem ręczne przywrócenie `0x101E39` i `sched_rt_runtime_us`) ✅.
