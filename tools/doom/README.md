# Mały, otwarty WAD do Dooma we flashu

`doom1.wad` (shareware id Software) to freeware, nie open source. Zamiennik: **Freedoom** (licencja BSD-3). Pełny `freedoom1.wad` ma 28,8 MB, a we flashu na cały rootfs są 2 MB (WAD może zająć tyle co dziś `doom1.wad`: ok. 1,36 MB po XZ), więc `mkminiwad.py` robi z niego okrojony IWAD.

```
python3 mkminiwad.py ~/Downloads/freedoom-0.13.0/freedoom1.wad freedoom_e1m1.wad 1
#  freedoom_e1m1.wad: 1 maps, 1238 lumps, 4.09 MB, xz 1.32 MB
```

| Liczba map (E1M1…) | Rozmiar | Po XZ |
|---|---|---|
| 1 | 4,1 MB | **1,32 MB** (mieści się zamiast `doom1.wad`) |
| 2 | 5,5 MB | 1,71 MB |
| 3 | 6,0 MB | 1,84 MB |
| 5 | 6,8 MB | 2,05 MB |
| 9, bez cięcia sprite'ów i dźwięków | 11,5 MB | 3,43 MB |

Mapy Freedooma są dużo bardziej szczegółowe niż oryginalne, więc nawet jedna waży sporo (sama E1M1 z teksturami ok. 0,35 MB po XZ).

## Autorzy i licencja Freedooma

`freedoom_e1m1.wad` to **zmodyfikowana (okrojona) wersja Freedoom: Phase 1 v0.13.0** (`freedoom1.wad` z wydania z 2024-01-29).

- Projekt: **Freedoom**, https://freedoom.github.io/ , źródła: https://github.com/freedoom/freedoom
- Copyright © 2001-2024 Contributors to the Freedoom project. All rights reserved.
- Licencja: BSD-3-Clause, pełny tekst w `FREEDOOM_COPYING.txt` (kopia `COPYING.txt` z wydania).
- Twórcy (grafika, tekstury, sprite'y, dźwięki, mapy): `FREEDOOM_CREDITS.txt` (kopia `CREDITS.txt`). Autorzy muzyki: `FREEDOOM_CREDITS-MUSIC.txt` (kopia `CREDITS-MUSIC.txt`). W tym WAD-zie gra m.in. muzyka E1M1 „Stanky Leg Specialist” (Lola „BlueWorrior” Harvey), ekranu tytułowego „Fight for Freedom” (Korp, na podstawie „Fountain Dance” Tylera „Picklehammer” Pantelli), przerwy między poziomami „YOU LIVE!!!!” (Stratos) i zakończenia „Distance” (Korp). Wydanie nie podaje autorów poszczególnych map, tylko zbiorczo w `CREDITS.txt`.

Warunki licencji przy rozpowszechnianiu (np. WAD w obrazie rootfs albo w publicznym repo):
- dołączyć notkę copyright, warunki i zastrzeżenie, czyli `FREEDOOM_COPYING.txt` (w rootfs obok WAD-a, np. `/home/FREEDOOM_COPYING.txt`); dobrze też dołączyć oba pliki `CREDITS`;
- nie używać nazwy projektu Freedoom ani jego autorów do promowania naszej pracy. Piszemy więc tylko, że to zmodyfikowany Freedoom, a nie wersja Freedoom od jego autorów. Nasze zmiany opisuje sekcja niżej, a robi je `mkminiwad.py` (nasz kod).

## Co skrypt zostawia (2026-10-20)

- wybrane mapy E1M1…E1Mn i ich muzykę; bez E3M1/E4M1 silnik uznaje WAD za **shareware** (w menu tylko epizod 1, lista przełączników z epizodu 1);
- tylko tekstury ścian użyte na mapach (wpisy `TEXTURE1/2` + łatki), przełączniki epizodu 1 (`P_InitSwitchList` wymaga ich wszystkich) i całe zakresy animacji z `animdefs` (`p_spec.c`), jeśli choć jedna klatka jest użyta;
- tylko użyte płaszczyzny + `F_SKY1`, `FLOOR4_8` (tło końca E1), `FLOOR7_2` (tło ramki wokół widoku, `r_draw.c`; bez niej gra kończy się błędem `W_GetNumForName: FLOOR7_2 not found!` przy zmniejszonym widoku, sprawdzone na płytce 2026-10-20) i całe animowane zakresy;
- sprite'y bez potworów nieobecnych na mapach (dla E1M1–E1M5: bez Lost Soul, Cacodemona, Barona, Cyberdemona, Spider Mastermind i ich pocisków) i bez nieużywanych zestawów (`PLYC`, `MANF`, `BOSF`). Bronie zostają wszystkie, bo `IDKFA` je daje, a brak sprite'u broni kończy się błędem;
- wszystkie dźwięki przepróbkowane do 11 025 Hz (jak w oryginalnym Doomie; Freedoom ma wiele w 22/44 kHz, mikser `i_mstarsound.c` przyjmuje każdą częstotliwość). Dźwięki usuniętych potworów zastąpione krótką ciszą (brak lumpu dźwięku = błąd przy odtworzeniu);
- `DEMO1..3` zastąpione pustymi demami dla E1M1 (oryginalne grają mapy, których nie ma); bez ekranów E2/E3 (`WIMAP1/2`, `WIA1*`, `WIA2*`, `VICTORY2`, `ENDPIC`, `PFUB*`, `BOSSBACK`, `INTERPIC`, `END0..6`) i dźwięków PC speakera (`DP*`).

Sprawdzone na PC: każda tekstura z mapy jest w `TEXTURE1`, każda łatka tych tekstur i każda płaszczyzna są w pliku. **Na płytce (2026-10-20):** działa w systemie z flasha (polecenie `doom`) po dodaniu `FLOOR7_2`; sprawdzony start i ekran tytułowy, przejścia E1M1 do końca nikt jeszcze nie grał.

## Zmiana w fbDOOM

`g_game.c` (`G_DoCompleted`): gdy następnej mapy epizodu nie ma w WAD-zie, gra przechodzi do zakończenia epizodu (`ga_victory`: tekst końca E1 na tle `FLOOR4_8`) zamiast kończyć się błędem `W_GetNumForName: E1M2 not found`. Bez tej zmiany WAD też działa, tylko wyjście z E1M1 kończy grę błędem.

## Uruchomienie

`fbdoom -iwad freedoom_e1m1.wad` (nazwa pliku dowolna; typ gry silnik rozpoznaje po lumpach). Pełne `freedoom1.wad`/`freedoom2.wad` na pendrivie: `-mb 16`, bo domyślna strefa pamięci w fbDOOM ma tylko 6 MB, a mapy Freedooma są duże.
