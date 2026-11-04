# Odbiór radia FM przez ADC demodulatora

Stan 2026-11-04: ✅ radio na żywo na HDMI w `specan`, z anteną z kawałka kabla leżącego na ziemi. Odsłuch z pliku użytkownik potwierdził: to stacja radiowa. Dźwięk jest mono i mocno zaszumiony (słaba antena).

## Radio na żywo: `specan`

```sh
# PC
rootfs_mini/output/host/bin/mipsel-buildroot-linux-musl-gcc -O2 -static -pthread -o specan tools/board/specan.c -lm
python3 tools/serial/upload.py specan /tmp/specan
# płyta, po MBoot tuner_init; Ctrl-C zamyka Dooma
chmod +x /tmp/specan; /tmp/specan 88 108            # albo -F 101000: od razu radio
```

Sterowanie: mysz USB (klik lewym = słuchaj, kółko = marker ±100 kHz albo zoom, środkowy = przełącz tryb kółka, prawy = wyłącz radio; [analizator_widma.md](analizator_widma.md)), strzałki lewo/prawo = marker ±100 kHz, Enter = radio wł./wył., `r` = nowe przemiatanie, `+`/`-` = głośność podczas słuchania, `c` = oznaczenia prążków własnych, `,`/`.` = wzmocnienie tunera, `[`/`]` = wzmocnienie ADC, `b` = filtr tunera (bez zysku, patrz [wzmocnienia_toru.md](wzmocnienia_toru.md)); panel: CH+/CH− marker, POWER radio. Mysz i klawiaturę USB można wpiąć w trakcie pracy. ✅ serial; ✅ mysz przez hub USB (krzyżyk, klik, dźwięk, użytkownik 2026-11-04); ❓ klawiatura USB i panel nie sprawdzone. Podczas słuchania przemiatanie stoi; ekran pokazuje `PILOT` (19 kHz względem 17 kHz, okno 0,5 s, skacze przy słabych stacjach) i `RX`.

**USB po przełożeniu urządzenia (2026-11-04):** po wyjęciu pendrive'a każde następne urządzenie kończyło się `error -145` (`ETIMEDOUT`), mimo poprawnie wykrytej prędkości, także pendrive high-speed. Pomaga przeładowanie sterownika: `tools/board/usbfix.sh` ✅ (kombinacja mysz + klawiatura `09DA:7036`, full-speed, wstaje jako `event1`–`event4`). Bit UTMI „HS rx robust” (reg `0x09` bit 7, fiz. `0x1F205411`), który MBoot przełącza zależnie od prędkości, nie ma tu znaczenia: enumeracja przechodzi przy obu ustawieniach ✅. Przy awarii działał `specan`. Później, po tym przeładowaniu i również przy działającym `specan`, wpięcie huba i kilkukrotne przełożenie myszy działało bez `usbfix.sh` ✅ (według użytkownika; `dmesg` nieoglądany). Awaria nie jest więc skutkiem samego działania `specan` ani każdego przełożenia. Dokładny warunek (może wyjęcie urządzenia zainicjowanego przez MBoot) nieznany ❓.

**Płynność interfejsu (2026-11-04):** wykres rysowany wierszami, wejście czytane także podczas ustalania tunera, a zmiany widoku i markera wykonywane dopiero między strojeniami. Pomiar czasu od ruchu kursora do końca przełączenia klatki (`-v`, kompilacja z `-DPTR_TEST`: klawisz `x` na serialu przesuwa kursor, 25 ruchów/s): przemiatanie mediana 44 ms, maks. 47; radio mediana 53 ms, maks. 55 ✅. W tym teście żadnych trzasków w dźwięku i żadnych śladów kursora na ekranie (grabber) ✅. Prawdziwej myszy po tej zmianie jeszcze nie sprawdzono ❓. Opóźnienia sprzed zmiany dla przemiatania nie mierzono; czasy rysowania: [akceleracja_grafiki.md](akceleracja_grafiki.md).

Tor (wszystko na liczbach całkowitych):

- **VPE1, FIFO:** tylko próbkowanie i mieszanie jak w `fmrx`, 120 kS/s I/Q do pierścienia. Ok. 5,5 MS/s, puste przedziały ok. 2 % ✅.
- **VPE0, FIFO:** FIR ±45 kHz, dyskryminator (`fast_atan2`), 80 Hz górnoprzepustowy, de-emfaza 50 µs, 7 kHz dolnoprzepustowy (2 biquady), 120 → 48 kHz, `own_audio.h`. Zegary I/Q i HDMI są z jednego kwarcu, więc zapas w pierścieniu nie dryfuje ✅ (stały ok. 3000–3400 przedziałów).

Znalezione przy uruchamianiu:

- **Trzaski dokładnie co 1 s** (także w `fmrx`) to przerwa ok. 50 ms: domyślny limit jądra `sched_rt_runtime_us` (950 ms na 1000 ms) zatrzymuje wątek RT zużywający 100 % CPU ✅. `specan` ustawia na czas pracy −1 i przywraca przy wyjściu. Żeby wątki jądra na VPE1 mogły ruszyć, próbnik co 0,25 s na chwilę przechodzi na `SCHED_OTHER` i robi `sched_yield` (`nanosleep` przy 250 Hz kosztował pełne 4 ms). Najdłuższa przerwa teraz ok. 1 ms; 28 s nagrania z HDMI bez wykrytych trzasków ✅.
- W przerwie (przerwanie zegara co 4 ms, ok. 130 µs) filtry dostają swoją wygładzoną wartość zamiast chwilowej wartości dyskryminatora. To wyraźnie poprawiło kontrast dźwięku.
- Radio stroi tuner do najbliższego pełnego MHz ok. 2 MHz nad stacją (IF 2,1–3,1 MHz).

Pliki: `binaries/fm/radio_hdmi_101.wav` (HDMI przez grabber), `binaries/fm/f101_8s.wav` (demodulacja na PC).

## Nagranie i demodulacja na PC

```sh
# PC
rootfs_mini/output/host/bin/mipsel-buildroot-linux-musl-gcc -O2 -static -o fmrx tools/board/fmrx.c -lm
python3 tools/serial/upload.py fmrx /tmp/fmrx
# płyta (po MBoot tuner_init; RF w kHz, strojenie w MHz, sekundy, plik, ticki na próbkę)
chmod +x /tmp/fmrx; /tmp/fmrx 101000 103 8 /tmp/a.i8 2300
# PC (8 s przy 120 kS/s: 1,9 MB, ok. 3,5 min przez UART)
python3 tools/serial/uart_get.py /tmp/a.i8 f101.i8
python3 tools/fm_demod.py f101.i8 f101          # f101.wav, f101.png, testy pilota
```

Strojenie: ok. 2 MHz powyżej stacji, żeby IF ok. 2,6 MHz leżała poniżej złożenia widma i z dala od grzebienia 6 MHz ([falszywe_sygnaly.md](falszywe_sygnaly.md)).

## Jak działa `tools/board/fmrx.c`

Miesza każdą próbkę z NCO (tablica 256 wpisów) w pętli próbkowania i uśrednia I/Q w stałych przedziałach czasu. Fazę NCO przesuwa o czas ze znacznika CP0 Count tej próbki, więc przerwy na przerwania nie gubią fazy. Mieszanie obniża próbkowanie z 9,09 do ok. 5,8 MS/s; to bez znaczenia, bo przedziały są liczone czasem. Co 4 ms przerwanie zegara zabiera część próbek ✅ (w `fmrx` 5,6 % pustych przedziałów, w tym limit RT); `fm_demod.py` interpoluje I/Q przez te dziury, a w dźwięku zostają prążki 250 Hz i wielokrotności, ok. +5–8 dB nad otoczeniem.

## Wyniki

Dowód, że to radio: **pilot stereo 19 kHz po demodulacji** (mierzone 18 999 Hz; kontrola 17 i 23 kHz: brak). Żaden prążek z toru nie daje takiego tonu w demodulowanym FM.

| Stacja MHz | Nagranie | Pilot nad otoczeniem |
|---|---|---|
| 101,0 | 8 s, ±45 kHz | **+27 dB** |
| 101,0 | 1 s | +17 dB |
| 91,1 | 1 s | +12,5 dB |
| 99,9 | 1 s | +10,6 dB |
| 98,1 | 1 s | +4,6 dB ❓ |

101,0 MHz, 8 s, filtr ±45 kHz: 0,1–0,5 kHz +10 dB, 0,5–1 kHz +10 dB, 1–2 kHz +5 dB, 2–5 kHz +2 dB względem 5–15 kHz. Program jest więc nad szumem głównie poniżej 2 kHz. Moc w kanale tylko ok. 3 dB nad szumem, widmo RF to dzwon ok. ±30 kHz i silny wąski prążek na nośnej.

Spektrogram nagrania (`binaries/fm/f101_8s_spec.png`) pokazuje harmoniczne głosu ze zmienną wysokością ✅.

Niewyjaśnione ❓: prążek na −69 kHz od nośnej w nagraniu 101,0 MHz. RDS (57 kHz) niewidoczny. `fmrx` nadal ma trzaski co 1 s (limit RT, wyżej).

Pomysły na później: lepsza antena (największy zysk), demodulacja PLL, widmo samego kanału podczas słuchania, stereo.
