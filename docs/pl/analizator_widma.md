# Analizator widma na ekranie: `tools/board/specan.c`

Widmo RF na żywo z FFT, ślad i wodospad na `/dev/fb0`, sterowane z konsoli szeregowej i myszą USB. Sprawdzone na płycie 2026-11-04 przez grabber HDMI ([grabber_hdmi.md](grabber_hdmi.md)). Tło pomiaru: [demodulator_if.md](demodulator_if.md).

## Uruchomienie

```sh
# PC: SDK musl z rootfs_mini (na systemie z flasha nie ma gcc)
rootfs_mini/output/host/bin/mipsel-buildroot-linux-musl-gcc -O2 -static -pthread -o specan tools/board/specan.c -lm
python3 tools/serial/upload.py specan /tmp/specan

# MBoot (Enter/CR przy starcie, tools/serial/catch_mboot.py reboot): ADC i 8051 muszą działać przed Linuksem
tuner_init
run bootcmd

# Linux: Ctrl-C zamyka Dooma, potem
chmod +x /tmp/specan; /tmp/specan 88 108
```

Bez `tuner_init` program kończy się komunikatem, bo ADC czyta wartość stałą. `/tmp` znika po restarcie; programu nie ma w rootfs we flashu.

Opcje: `-n FFT` (domyślnie 4096; powyżej brakuje odcinków bez przerw), `-a AVG` (liczba FFT na strojenie, każde to 2 bloki rzeczywiste; domyślnie 2), `-t KROK_MHZ`, `-s SETTLE_MS` (domyślnie 20), `-v` (szczyty na konsoli), `-S` (lista prążków, bez ekranu), `-D` (z `-S`: zrzut całego śladu), `-M` (średnia w kolumnie), `-R` (surowe prążki każdego strojenia), `-C` (wyjście zegara tunera wyłączone).

Sterowanie (w tym radio FM i mysz): [odbior_fm.md](odbior_fm.md). Reszta klawiszy: `a`/`d` przesuwanie widoku, góra/dół lub `w`/`s` zoom, `1`–`4` zakresy (FM, VHF, UHF, całość), `z` tryb kółka, `v` VBW, `h` max hold, `m` max/średnia w kolumnie, `+`/`-` poziom odniesienia, `p` pauza, `q` wyjście.

## Zoom kółkiem i VBW (2026-11-04)

**Tryby kółka:** środkowy przycisk myszy albo `z` przełącza między `WHEEL: MARKER` (kółko przesuwa marker) a `WHEEL: ZOOM`. W trybie zoom jeden ząbek zmienia zakres 1,5 raza, a częstotliwość pod kursorem zostaje w tym samym miejscu ekranu. Poza wykresem zoom liczy się od środka widoku. Najwęższy zakres to 200 kHz. Stary ślad zostaje na ekranie do czasu, aż dojdzie do niego przemiatanie; wodospad zaczyna się od nowa. Podczas słuchania każda zmiana widoku zatrzymuje radio na jedno przemiatanie, jak wcześniej `w`/`s`.

Krok markera zależy od zakresu: 100 kHz, poniżej 5 MHz 10 kHz, poniżej 500 kHz 1 kHz. Ta sama siatka obowiązuje przy kliknięciu.

**RBW i VBW:** zoom nie zmienia RBW, które zawsze wynosi ok. 3,3 kHz (FFT 4096), więc w wąskim widoku pojedynczy prążek FFT zajmuje wiele pikseli. Tryb MAX bierze jeszcze ±1 prążek, stąd płaski wierzchołek prążka. `v` przełącza liczbę FFT na strojenie 1, 2, 4, 8, 16, 32 i znów 1. Na ekranie jest VBW liczone jako RBW / liczba bloków (każde FFT to 2 bloki): 1664 Hz do 52 Hz. To przybliżenie uśredniania FFT, nie filtr po detektorze. Na 88–108 MHz przemiatanie trwa ok. 1,6 s przy 1 FFT, 8,5 s przy 32 FFT. Przy 32 FFT szum jest wyraźnie gładszy i widać kilka słabszych prążków ginących w nim przy domyślnym ustawieniu (przy 88,6, 91,1, 95,4, 103,6 MHz); co to za sygnały, nie sprawdzono ❓.

**Długość bufora:** jedno przechwycenie to najwyżej 8 FFT, a `measure()` powtarza je do uzbierania liczby FFT. Z buforem 2 MB (32 FFT naraz) każde okno miało przerwę i wychodziło 0 FFT ✅, z 1 MB (16 FFT) działało ✅. Przyczyna to prawdopodobnie zasięg TLB ❓.

**Rysowanie w trakcie pomiaru:** przy domyślnym VBW ekran jest rysowany najwyżej raz na strojenie, tak jak przed zmianą. Pierwsza wersja rysowała po każdym FFT, co przy ruchu kursora wydłużało przemiatanie 88–108 MHz z 1,8 s do 3,7 s ✅. Teraz przy wąskim VBW wejście jest czytane co 50 ms, a klatka rysowana najwyżej co 80 ms. Pomiar z ruchem kursora z konsoli, 25 ruchów/s ✅: domyślny VBW przemiatanie 1,82 s, opóźnienie mediana 39 ms, maks. 51 (wersja sprzed zmiany: 1,81 s, 37 i 51). Prawdziwa mysz, wersja sprzed zmiany: opóźnienie mediana 41 ms, maks. 51; przemiatanie 1,56–1,79 s ✅. Nowej wersji z myszą nie mierzono, użytkownik nie widzi na oko różnicy między nimi ✅.

Sprawdzone na płycie przez konsolę szeregową i zrzuty `/dev/fb0` ✅: zoom wokół kursora do 200 kHz i z powrotem, marker, VBW do 52 Hz bez komunikatów o przerwach, zoom przy włączonym radiu (po przemiataniu radio wraca, pilot 101,0 MHz +27 do +31 dB). Kółko i przełączanie trybów prawdziwą myszą: działa według użytkownika ✅. Test bez myszy: kompilacja z `-DPTR_TEST`, `y`/`u` z konsoli zastępują kółko.

## Jak to działa

- Próbki jak w `dmdts`: `0x112098` przy `0x101E39` = 0, ok. 9,09 MS/s, znacznik CP0 Count przy każdej (dokładnie 276 MHz; fs liczone przy każdym pomiarze, bo waha się o 0,1 % między strojeniami). Okna FFT tylko z odcinków bez przerw (odstęp ponad 1,5 okresu odrzuca okno).
- FFT całkowitoliczbowe (CPU bez FPU), okno Hanna, 2 bloki rzeczywiste w jednym FFT zespolonym: `|X1|²+|X2|² = (|Z(k)|²+|Z(N-k)|²)/2`. Dla N = 4096: bin 2,2 kHz, RBW 3,3 kHz.
- **Składanie wokół 4,54 MHz:** każdy punkt RF jest mierzony z kilku strojeń (krok 1–3 MHz, odległość od strojenia 0,12–3,2 MHz) i wygrywa niski odczyt: najmniejszy, a od 4 odczytów drugi najmniejszy, bo jedno złe strojenie (219 MHz) kasowałoby prawdziwy prążek. Odbicie lustrzane wypada przy każdym strojeniu gdzie indziej, a prawdziwy sygnał zostaje. W symulacji `-DHOST` zero duchów; tak samo znika przeciek LO.
- **Filtr IF:** przy starcie mediana każdego binu z 24 strojeń od 50 do 860 MHz, wygładzona; 0 dB = mediana z 1,5–4 MHz. Na płycie pasmo od ok. 1 do 4,2 MHz jest płaskie w granicach ±1 dB, poniżej 1 MHz spada (−10 dB przy 0,6 MHz).
- Skala: dBFS względem sinusa o pełnej amplitudzie ADC, moc w binie. Sprawdzone tylko w symulacji (ton 40/128: oczekiwane −10,1, wskazane −11).

Ograniczenie metody: minimum po strojeniach wycina sygnały, które zmieniają się między strojeniami (każde strojenie to inna chwila, całe przemiatanie to ok. sekundy). Stała nośna przechodzi w całości, modulacja FM i inne sygnały zmienne mogą wyjść węższe i słabsze ❓.

Czasy na płycie: ok. 60 ms na strojenie przy `-a 2`; 88–108 MHz ok. 1,7 s, 44–880 MHz ok. 17 s.

## Wyniki (2026-11-04)

Prawie wszystkie prążki widoczne bez sygnału zewnętrznego są własnymi zakłóceniami toru: grzebień 6 MHz związany z kwarcem tunera, zegar HDMI, przeciek LO. Rzeczywista IF to ok. 4,5953 MHz. Opis i dowody: [falszywe_sygnaly.md](falszywe_sygnaly.md).

Antena: kawałek kabla na ziemi. Garbów multipleksów DVB-T nie widać ❓. Stacje FM w domyślnym trybie MAX wyglądają jak pojedyncze słabe prążki; lepiej widać je w średniej z długim uśrednianiem (`-S -D -M -a 32`). Odbiór stacji: [odbior_fm.md](odbior_fm.md).

## Test na PC

`gcc -DHOST -O2 -o specan_host tools/board/specan.c -lm && ./specan_host -v -x 2 88 108` zastępuje tuner i ADC symulacją (tony, multipleks 474 MHz, przerwy w próbkowaniu) i zapisuje `specan_host.ppm`.
