# fbDOOM: podwójne buforowanie, sterowanie przez serial, `openvt`

fbDOOM (`fbDOOM/fbdoom/`, git na bazie https://github.com/maximevince/fbDOOM, gałąź `master` + 3 nasze commity; wcześniej kopia z forka vvaltchev/fbDOOM, a notatka `opis_DOOM` podaje nyorem/fbdoom) zmodyfikowany pod sterownik `mstar-gopfb` ([sterownik_gopfb.md](sterownik_gopfb.md)). Opis zmian i użycia po angielsku jest w `fbDOOM/README.md` (do forka na GitHubie). Zmienione pliki: `i_video_fbdev.c` (rysowanie, podwójny bufor, `-fps`) i `i_input_tty.c` (klawisze z konsoli szeregowej). ✅ Sprawdzone na płycie 2026-10-14: podwójny bufor i pomiary wydajności. Tryb klawiszy z serialu włącza się (komunikat `Serial keys on stdin`), ale samo granie przez serial czeka jeszcze na test ❓.

## Budowanie i wgranie

```sh
cd fbDOOM/fbdoom
export PATH=~/toolchains/mipsel/mipsel-buildroot-linux-gnu/bin:$PATH   # SDK Buildroota
make -j6
mipsel-buildroot-linux-gnu-strip -o fbdoom_dbl fbdoom                  # 2,1 MB -> 0,5 MB
gzip -9 -k fbdoom_dbl
```
Uwaga: w tej samej powłoce `python3` z SDK przesłania systemowy (nie ma `pyserial`), więc `uart_upload.py` uruchamiaj w osobnej powłoce:
```sh
python3 tools/uart_upload.py fbDOOM/fbdoom/fbdoom_dbl.gz /tmp/fbdoom_dbl.gz
# na płycie:
gunzip -c /tmp/fbdoom_dbl.gz > /home/fbdoom_dbl && chmod +x /home/fbdoom_dbl
```
Na płycie w `/home` są też `doom1.wad` i stary `fbdoom`.

## Uruchomienie

```sh
cd /home
./fbdoom_dbl -iwad doom1.wad
```
Podczas gry konsola wirtualna jest w trybie `KD_GRAPHICS` (`/dev/tty0`), więc konsola tekstowa nie rysuje po ekranie, a przy wyjściu wraca `KD_TEXT`. Ręczne `echo 0 > /sys/class/vtconsole/vtcon1/bind` nie jest już potrzebne. Bez tego przy podwójnym buforze migało: kursor i komunikaty jądra trafiały do bufora 0, a ten jest na ekranie co drugą klatkę. Pomiar `flipcheck` (1000 przełączeń) pokazał, że sam sterownik nie gubi vsync: wszystkie odstępy to ok. 20 ms, a `FBIO_WAITFORVSYNC` czekał 10–20 ms.

Opcje (nowe):

| Opcja | Działanie |
|---|---|
| (brak) | podwójny bufor + czekanie na vsync, gdy fb ma `yres_virtual ≥ 2·yres` |
| `-singlebuffer` | rysowanie w widocznym buforze (rwie się) |
| `-novsync` | podwójny bufor, ale bez czekania na vsync |
| `-fps` | co 5 s wypisuje liczbę klatek na sekundę (na stderr) |
| `-serialkeys` | wymuś sterowanie z konsoli szeregowej |
| `-vtkeys` | wymuś klawiaturę konsoli wirtualnej (USB), nawet przy starcie z serialu |
| `-keyhold N` | ile ms trzymać klawisz po pierwszym naciśnięciu w trybie serialowym (domyślnie 300) |
| `-scaling N` | powiększenie (domyślnie 5: 320×200 → 1600×1000) |

## Sterowanie przez konsolę szeregową (bez klawiatury USB)

Uruchomiony z konsoli szeregowej (picocom, minicom…) Doom widzi, że standardowe wejście to terminal, ale nie konsola wirtualna. Wtedy sam przełącza się na **klawisze ze standardowego wejścia** (tryb surowy terminala). Ten sam tryb włącza się, gdy nie znajdzie klawiatury VT:

| Klawisz w terminalu | W grze |
|---|---|
| strzałki albo `W` `S` `A` `D` | ruch / obrót |
| `Q` / `E` | krok w bok w lewo / w prawo |
| `F` | strzał |
| spacja | użyj (drzwi, przełączniki) |
| `1`–`7` | broń |
| `Tab` | mapa |
| `Enter`, `Esc` | menu |
| `Y` / `N` | odpowiedzi w menu |
| **`Ctrl-C`** | wyjście z gry (terminal wraca do normalnego trybu); działa też menu Quit Game → `Y` |

Terminal wysyła tylko **naciśnięcia** (i autopowtarzanie przy trzymaniu), nigdy puszczenia. Puszczenie jest więc emulowane:
- po pierwszym naciśnięciu klawisz jest „trzymany” 300 ms (`-keyhold`);
- każde powtórzenie przedłuża to o 120 ms;
- gdy powtórzenia ustaną, gra dostaje „puszczenie”.

Trzymanie strzałki daje ciągły ruch, a krótkie stuknięcie mały krok. Dwóch klawiszy naraz (np. strzał w biegu) nie da się trzymać, bo terminal powtarza tylko ostatni.

Komunikaty jądra też idą na `ttyS0`. Gdy przeszkadzają: `dmesg -n 1`.

## `openvt`: gra na osobnej konsoli wirtualnej (z klawiaturą USB)

Do zwykłej klawiatury USB Doom potrzebuje **terminala wirtualnego** (`/dev/ttyN`) jako terminala sterującego. Czyta wtedy surowe kody klawiszy (`KDSKBMODE` → `K_MEDIUMRAW`), z naciśnięciami i puszczeniami, i kilka klawiszy naraz działa. Uruchomiony z konsoli szeregowej ma terminal sterujący `ttyS0` i klawiatury nie widzi.

`openvt` (BusyBox) uruchamia program na wskazanej konsoli wirtualnej, która staje się jego terminalem sterującym:
```sh
openvt -c 2 -s -w -- sh -c 'cd /home; ./fbdoom_dbl -iwad doom1.wad'
```
- `-c 2`: na konsoli `tty2` (na `tty1` jest już getty z logowaniem);
- `-s`: przełącz ekran na tę konsolę (jak Ctrl-Alt-F2);
- `-w`: czekaj na zakończenie programu (bez tego `openvt` od razu wraca);
- `--`: dalej jest polecenie z argumentami.

Klawiatura USB steruje grą, a konsola szeregowa jest wolna do podglądu, np. `top`. Po wyjściu z gry (Backspace) wróć na pierwszą konsolę: `chvt 1`. Gdyby gra się zawiesiła: `killall fbdoom_dbl; chvt 1`.

Uwaga: wyjście programu (`printf`) idzie wtedy na `tty2`, a nie na serial. Żeby je zobaczyć, przekieruj do pliku: `... > /tmp/doom.log 2>&1`. Plik zapełni się dopiero po wyjściu, bo stdout jest buforowany.

## Wydajność (zmierzone, `-fps`, demo z ekranu tytułowego)

| Wersja | kl./s |
|---|---|
| oryginalna ścieżka rysowania (kolor i powiększenie bajt po bajcie, `write()`) | **~6** |
| nowa ścieżka 16 bpp, `-singlebuffer` | ~50 |
| nowa ścieżka, podwójny bufor, `-novsync` | ~49 |
| nowa ścieżka, podwójny bufor + vsync (domyślnie) | **~35**, bez rwania |

- Gra liczy logikę 35 razy na sekundę, więc 35 kl./s z vsync to pełna płynność. Wyjście HDMI ma 50 Hz.
- Skąd przyspieszenie (pomiary `fbbench`): kopiowanie klatki 1600×1000 do pamięci fb trwa 12 ms (269 MB/s), powiększenie ×5 z zapisami 16-bitowymi 9 ms. Stary kod liczył kolor osobno dla każdego bajtu i każdego powtórzenia piksela (~160 ms na klatkę).
- Nowa ścieżka: paleta od razu w formacie fb (`palette16[]`, liczona w `I_SetPalette`). Jedna powiększona linia powstaje w RAM i jest kopiowana `memcpy` 5 razy wprost do `mmap` fb.

## Wyjście z gry

W oryginale `I_Quit()` wywołuje funkcje sprzątające, ale `exit(0)` jest w `#if ORIGCODE`, więc po „Quit Game → Y” gra działała dalej (wyglądało to jak restart). Upstream obszedł to tylko w `M_QuitResponse`. Teraz `I_Quit()` zawsze kończy program, a przywrócenie terminala/klawiatury i powrót ekranu na bufor 0 (tam rysuje konsola tekstowa) robią funkcje `atexit()`. ✅ Sprawdzone: Esc, 5× strzałka w dół, Enter, `Y` → program kończy się z kodem 0, a terminal działa normalnie.
