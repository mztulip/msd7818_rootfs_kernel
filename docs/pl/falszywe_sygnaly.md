# Fałszywe sygnały w widmie: skąd się biorą

Prążki, które `tools/board/specan` ([analizator_widma.md](analizator_widma.md)) pokazuje, a które nie są radiem. Pomiary z 2026-11-04 na płycie, surowe dane w `binaries/spurs/`.

Oznaczenia: ✅ zmierzone na płycie, 🔍 wynika z kodu lub schematu, ❓ hipoteza.

## Podsumowanie

| Rodzina | Częstotliwości | Poziom | Źródło | Pewność |
|---|---|---|---|---|
| Grzebień 6 MHz | wielokrotności 6 MHz, 48–864 MHz, 49 z 58 prążków | do −5 dBFS | **coś związane z kwarcem tunera**, nie SoC | ✅ związanie, ❓ mechanizm |
| Rodzina HDMI | wielokrotności 74,25 MHz: 148,5 / 222,75 / 371,25 / 445,5 / 519,75 / 668,25 / 742,5 MHz | do −18 dBFS, chwilami silniej | **nadajnik HDMI** w SoC (1080p50) | ✅ |
| Przeciek LO | zawsze dokładnie na częstotliwości strojenia | −28 dBFS, tuż po strojeniu do −3 | **generator LO tunera** | ✅ |
| Odbicia lustrzane | RF lustrzane względem strojenia | jak sygnał | **próbkowanie 9,09 MS/s** (składanie widma) | ✅ |
| Złe strojenie | F = 219 MHz | | **strojenie tunera** | ✅ objaw, ❓ przyczyna |

Te pięć rzeczy to nie sygnały z anteny. Nie są też zakłóceniem z procesora: **w widmie nie ma żadnego prążka na kwarcu SoC poza HDMI** (niżej), choć wiele prążków wypada na „okrągłych” częstotliwościach zegarów SoC (144, 216, 432, 552 MHz).

**Oznaczenia w `specan`:** program wylicza te częstotliwości i koloruje linię widma oraz etykietę szczytu: wielokrotności 6 MHz na pomarańczowo, 74,25 MHz na fioletowo; klawisz `c` to wyłącza. To pozycje wyliczone, więc nie każda wielokrotność ma prążek, a przy pełnym zakresie (kolumna ok. 460 kHz) kolor może trafić też w inny sygnał obok.

## Jak rozdzielono źródła: dwa kwarce

Na schemacie MSD7818 są dwa kwarce 24 MHz: przy SoC (`24M (20PF)`) i przy MxL5007T (`24M (12PF)`) 🔍. Schemat nie musi być dokładnie tą płytą, ale pomiar niżej też pokazuje dwa różne zegary.

LO tunera pochodzi z kwarcu tunera, więc odczyt RF jest liczony w jego skali. Prążek związany z tym samym kwarcem ma odchyłkę stałą w całym paśmie. Prążek z innego kwarcu ma odchyłkę proporcjonalną do częstotliwości.

Pomiar (`-R -a 8`, N = 4096, interpolacja parabolą; przed ostateczną kalibracją `FIF_HZ` o 1,3 kHz; `precyzyjne_odchylki.txt`):

| Prążek MHz | 48 | 72 | 96 | 144 | 216 | 288 | 432 | 552 | 600 | 720 | 864 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Odchyłka kHz | +1,3 | +1,2 | +1,4 | +1,3 | +1,1 | +1,3 | +1,3 | +1,3 | +1,3 | +1,5 | +1,4 |

| Prążek HDMI MHz | 148,5 | 222,75 | 371,25 | 742,5 |
|---|---|---|---|---|
| Odchyłka kHz | +2,2 | +2,9 | +3,4 | +6,0 |
| Prosta 1,3 + 6,3 ppm | +2,2 | +2,7 | +3,6 | +6,0 |

- Grzebień: rozrzut 0,4 kHz bez trendu, więc **różnica poniżej ok. 0,25 ppm** względem LO ✅.
- HDMI: nachylenie **6,3 ppm** ✅.
- Zegar HDMI jest dokładnie związany z SoC: okres vsync wobec licznika CP0 Count (1500 klatek) to 5 520 000,12 taktu przy nominalnych 5 520 000, czyli +0,02 ppm ✅.

Wniosek: **kwarc SoC różni się od referencji tunera o ok. 6,3 ppm, a grzebień 6 MHz tej różnicy nie ma, więc nie pochodzi od zegarów SoC**. Dotyczy to także 432 MHz (MPLL) i 552 MHz (CPU): te prążki leżą w grzebieniu, a nie na kwarcu SoC. To wyklucza też nadajnik zewnętrzny, bo ten nie byłby związany z kwarcem tunera.

## Grzebień 6 MHz

Prążki z ostatniego skanu (`lista_koncowa_45-885.txt`), MHz:poziom dBFS:

48:−30, 54:−30, 72:−14, 90:−23, 96:−23, 108:−17, 120:−18, 126:−24, 144:−10, 162:−23, 168:−21, 180:−27, 192:−29, 198:−27, **216:−5**, 234:−27, 252:−25, 270:−27, 288:−10, 306:−30, 324:−16, 336:−27, 360:−23, 384:−30, 396:−27, 408:−30, 432:−16, 468:−24, 486:−32, 504:−25, 522:−31, 528:−32, 540:−25, 552:−29, 576:−27, 600:−23, 612:−27, 624:−24, 648:−10, 672:−25, 696:−24, 744:−30, 756:−26, 768:−29, 792:−20, 816:−24, 828:−22, 840:−20, 864:−17.

Próg listy to 8 dB nad szumem, więc brak na liście nie znaczy braku prążka. Najsilniejsze są wielokrotności 72 MHz (144, 216, 288, 648).

- Nie wchodzi przez antenę ✅: po jej odłączeniu 96, 144, 216, 288, 432 MHz mają te same poziomy ([wzmocnienia_toru.md](wzmocnienia_toru.md)).
- Niezależne od HDMI ✅: wyłączenie TMDS i PLL nie zmienia poziomów (tabela niżej).
- Niezależne od wyjścia zegara tunera (`0x03` = `0x40`, opcja `-C`) ✅: 96, 144, 216, 288, 432 bez zmian. Pojedynczy odczyt 864 MHz spadł z −17 na −22 dB, bez powtórki.
- Mechanizm ❓: 6 MHz = 24 MHz / 4, więc najpewniej harmoniczne referencji lub cyfrowego taktowania wewnątrz MxL5007T wracające na jego wejście RF. Nie sprawdzono, czy to wnętrze układu, czy tor na płytce przy tunerze.

**Korekta:** „stacja FM 96,03 MHz” z pomiarów `dmdts` (2026-11-03) to prążek 16 × 6 MHz. Przesuwanie o 1 MHz na 1 MHz strojenia nie odróżniało go od radia, bo każdy sygnał na wejściu RF zachowuje się tak samo. Podobnie 90 MHz.

## Rodzina HDMI

148,5 MHz to zegar pikseli 1080p; pozostałe to 1,5×, 2,5×, 3× i 5× tej wartości. Test rozstrzygający: stopniowe wyłączanie nadajnika z Linuksa przez `devmem` (rejestry z `script_MainPll_MIPSPLL_UPLL.S` 🔍). `None` = poniżej progu 12 dB nad szumem. Wraca po restarcie.

| Stan | 148,5 | 222,75 | 371,25 | 742,5 | kontrola 144 / 216 / 288 / 432 |
|---|---|---|---|---|---|
| Normalnie | −29,6 | −26,8 | −28,6 | −18,2 | −11,8 / −4,4 / −10,4 / −13,7 |
| TMDS wył. (`0x11305C` = 0, fiz. `0x1F2260B8`; grabber traci obraz) | brak | −27,1 | brak | −25,9 | −11,6 / −3,5 / −10,1 / −13,3 |
| + `0x113054/56/58` = `0xFFFF` | brak | −27,8 | brak | −24,6 | −10,9 / −3,8 / −10,2 / −13,4 |
| + PLL (`0x113066`, `0x11308A` = `0xFFFF`) | brak | **brak** | brak | **brak** | −10,9 / −4,0 / −10,1 / −13,9 |

Czyli 148,5 i 371,25 idą ze sterownika wyjścia TMDS, a 222,75 i 742,5 częściowo lub całkiem z PLL ✅. Prążka 445,5 MHz w tym teście nie mierzono: należy do rodziny tylko z częstotliwości.

Po odłączeniu anteny prążek 742,5 MHz spada poniżej progu ✅, więc przynajmniej ten wchodzi przez kabel antenowy, a nie przez wnętrze płyty.

Start MBoota bez `run flashlogo` niczego tu nie zmienia, bo MBoot włącza obraz jeszcze przed znakiem zachęty (`GOP3 was in format 9`).

## Przeciek LO tunera

Stary „prążek 4,495 MHz” ([demodulator_if.md](demodulator_if.md)) jest przy każdym strojeniu i po uwzględnieniu złożenia wypada dokładnie na RF = F: `473,0003`, `740,0002`, `214,0002` ✅. Stąd rzeczywista IF tunera: **ok. 4,5953 MHz, a nie nominalne 4,57** (`FIF_HZ`, wyznaczone z grzebienia; zgodne z tym przeciekiem).

Poziom zależy od czasu od strojenia (6 strojeń 470–475 MHz, `-a 2`):

| Czas po stabilizacji PLL | 0–8 ms | 10 ms | 15 ms i więcej |
|---|---|---|---|
| Poziom dBFS | do −1…−5 | −13…−28 | −23…−28 (przy części strojeń pod progiem) |

Tuż po strojeniu były też wstęgi ±97 kHz przy silnych prążkach (pomiar 5 ms). Przy 20 ms ich nie ma ✅. Możliwe, że to kalibracja przesunięcia DC w tunerze ❓. Dlatego domyślne ustalanie w `specan` to 20 ms.

## Odbicia lustrzane

fs ≈ 9,09 MS/s, a pasmo IF sięga 8,6 MHz, więc widmo składa się wokół 4,54 MHz i jeden bin to dwa miejsca RF, symetryczne względem strojenia ✅. To skutek odczytu ADC przez MIPS, nie tunera. `specan` usuwa je, biorąc niski odczyt z kilku strojeń.

## Złe strojenie 219 MHz

Powtarzalnie (3 pomiary): przy F = 219 MHz przeciek LO wypada na 218,971 zamiast 219,000, a prążek 216 MHz na 215,972 i ok. 25 dB słabszy ✅. Pozostałe strojenia mają odchyłki poniżej ok. 10 kHz, często przy prążkach zbyt słabych, by ocenić je dokładnie (`lo_wszystkie_strojenia.txt`). Przyczyna nieznana ❓, liczone w `tune()` N = 14016 nie wygląda szczególnie. Przy samym minimum takie strojenie kasowało prawdziwy prążek, dlatego od 4 odczytów `specan` bierze drugi najmniejszy.

## Co nie jest fałszywe lub nie jest wyjaśnione

- **91,1 / 99,9 / 101,0 MHz** ✅: prawdziwe stacje FM, jest w nich pilot stereo 19 kHz po demodulacji ([odbior_fm.md](odbior_fm.md)). **98,1 MHz** ❓: pilot tylko +4,6 dB z 1 s nagrania.
- **668,25 MHz** (9 × 74,25) i **519,75 MHz** (7 × 74,25) ❓: pasują do rodziny HDMI, ale w teście z wyłączaniem nadajnika ich nie sprawdzano.
- **Garb ok. 482 MHz** ❓: ok. 8 MHz szerokości, wygląda na multipleks DVB-T (zrzut z pełnego zakresu); nie demodulowany.
- **Skoki poziomu szumu przy ok. 135, 333 i 650 MHz** ❓: najpewniej przełączanie pasm tunera (333 MHz to zmiana rejestru `0x80` w `tune()`).

## Nieudane testy

- **Obciążenie DRAM** (`dd` na drugim VPE): próbkowanie spada do 6,9 MS/s z taką liczbą przerw, że nie powstaje ani jedno FFT. Wpływ aktywności pamięci na widmo jest niezmierzony. Prążków DDR (ok. 533 MHz, rejestry `0x110D30` = `0xE781`, `0x110D32` = `0x0026`, tryb 1066 z `004_MIU_init_ddr2.h` 🔍) na liście nie ma, ale pod progiem mogą być.

## Powtórzenie

```sh
specan -S -v -a 4 45 885          # lista prążków; IFSPUR = stała IF
specan -R -a 8 214 214            # surowe prążki jednego strojenia, obie możliwe wartości RF
devmem 0x1F2260B8 16 0            # TMDS wył. (obraz znika do restartu)
```
