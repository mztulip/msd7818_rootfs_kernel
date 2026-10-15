# Sondy w oryginalnym oprogramowaniu: jak podglądać rejestry i pamięć „na żywo”

Ta technika pozwoliła ustalić, jak oryginalny firmware dekodera (eCos) uruchamia dźwięk. Zrzuty rejestrów podczas grania MP3 i WAV, ślad czasowy skrzynek DSP i przeszukiwanie pamięci zostały zrobione **wewnątrz działającego oryginału**, bez zmieniania flasha. Sprawdzone na płytce (2026-10-14).

## Problem

Oryginalny firmware nie ma powłoki, debugera ani sieci. Na UART jest tylko konsola demo SDK MStara (znak `>>`). Z jej poleceń działa tylko `Help`, które wypisuje listę niedziałających poleceń. Chcemy zobaczyć, co firmware ustawia w rejestrach, kiedy robi coś ciekawego, np. gra MP3, i porównać to z naszym kodem pod Linuksem.

## Pomysł w jednym zdaniu

MBoot ładuje oryginał do RAM, ale go jeszcze nie uruchamia. Wtedy podmieniamy w RAM funkcję obsługującą `Help` na skok do własnego kodu, a potem startujemy oryginał. Każde `Help` w konsoli `>>` uruchamia nasz kod **w środku działającego firmware'u**, także w trakcie odtwarzania muzyki.

```
 SPI flash                RAM (64 MB)
 ┌──────────┐ spi_rdc   ┌─────────────────────────────────────────┐
 │ LZMA app │ ────────► │ 0x80B00000  spakowany obraz             │
 │ @0x8001C │ LzmaDec   │ 0x80000180  oryginał (eCos) rozpakowany │
 └──────────┘ ───────►  │   0x80142468  obsługa "Help"  ◄── łatka: skok
                        │ 0xA3800000  NASZA SONDA (mw.l z MBoota) │
                        └─────────────────────────────────────────┘
 go 0x80000224 → oryginał startuje normalnie → w konsoli ">>" piszemy Help → skacze do sondy
```

Flash zostaje nietknięty. Po wyłączeniu zasilania nie ma śladu po łatce.

## Kluczowe adresy (oryginał z tego dekodera)

| Co | Adres | Skąd wiadomo |
|---|---|---|
| obraz aplikacji w SPI flash | `0x8001C`, 0x289977 B (LZMA) | `bootcmd` MBoota |
| oryginał w RAM | od `0x80000180`, start `go 0x80000224` | `bootcmd` MBoota |
| obsługa `Help` konsoli demo | `0x80142468` (204 B) | Ghidra: funkcja wypisująca „press "HelpAudio"…” |
| `printf` oryginału | `0x80244f74` | Ghidra (używany w setkach miejsc z napisami formatującymi) |
| miejsce na sondę | `0xA3800000` (fizycznie `0x3800000`) | pamięć dekodera wideo (MVD), nieużywana przy samym audio i menu |
| bufor na wyniki | `0xA3900000` | j.w. |

Gdyby firmware był inny (inna wersja), te adresy trzeba ustalić na nowo w Ghidrze (`binaries/ghidra_stock/`).

## Dlaczego tak, a nie inaczej: zasady i pułapki

1. **Zapisy z MBoota przez KSEG1 (`0xA…`), nie `0x8…`.** `mw.l` pod adres `0x80142468` trafiłby do cache procesora i mógłby nie dotrzeć do RAM przed startem oryginału. Alias `0xA0142468` to ten sam bajt pamięci, ale bez cache. Dlatego wszystkie komendy w `mw_cmds_*.txt` mają adresy `a0…`/`a38…`.
2. **Łatka wynosi tylko 16 B.** W miejscu `Help` wpisujemy cztery instrukcje: `lui t9, hi(sonda)`, `ori t9, lo(sonda)`, `jr t9`, `nop`. Sama sonda leży osobno pod `0xA3800000`, więc może mieć kilka KB.
   - Pierwsza wersja (`tools/baremetal/stockdump`, asembler) mieściła się w całości w 204 B funkcji `Help`. Trzymanie danych w napisach pomocy (`0x8037F074`) nie zadziałało, bo odtwarzacz nadpisuje ten obszar.
   - W skrypcie linkera trzeba dać `SUBALIGN(4)`, bo inaczej `ld` wyrównuje sekcję do 16 B i kod się przesuwa.
3. **Nie ma libc.** Sonda jest kompilowana z `-ffreestanding -nostdlib`. Do wypisywania służy `printf` oryginału, wywoływany przez wskaźnik na funkcję.
4. **`printf` oryginału obsługuje niezawodnie tylko 3 argumenty.** Przy czwartym wypisywał śmieci. Dłuższe linie składamy z kilku wywołań.
5. **Wypisywanie jest wolne** (115200 bodów ≈ 11 KB/s) i zmienia czas działania. Do śladów czasowych sonda najpierw zbiera zmiany do RAM (`0xA3900000`), a wypisuje je dopiero na końcu (`stocktrace.c`, `setuptrace.c`).
6. **Duże obszary pamięci czytamy przez cache (KSEG0, `0x8…`).** Przy czytaniu bez cache (`0xA…`) muzyka zaczynała się rwać, bo sonda zabierała pasmo pamięci odtwarzaczowi. Rejestry czytamy zawsze przez `0xBF000000 + reg·2`.
7. **Czas mierzymy licznikiem CP0 Count** (`mfc0 $9`), który tyka 276 razy na mikrosekundę (552 MHz / 2). Sonda działa w kontekście konsoli eCos, więc jej pętle blokują tylko ten wątek. Muzyka gra dalej, bo dekodują DSP.
8. **Pendrive z Linuksem: lepiej wyjąć**, gdy działa oryginał. Raz uszkodził się wtedy `vmlinux.bin` na FAT. Pliki do odtwarzania mogą być na tym samym pendrivie, ale włóż go dopiero po starcie oryginału i wyjmij przed powrotem do Linuksa.
9. Od strony sondy oryginał **nie ma `reboot`**, więc do powrotu do Linuksa trzeba odłączyć zasilanie.

## Krok po kroku

### 1. Napisz sondę

Skopiuj szablon i zmień nazwę funkcji na nazwę pliku:
```sh
cd tools/baremetal/stockprobe
cp szablon.c moja.c
sed -i 's/szablon/moja/g' moja.c          # funkcja wejściowa musi się nazywać jak plik
```
Szablon wypisuje kilka rejestrów, czeka 0,5 s na liczniku CPU i wypisuje jeszcze jeden rejestr. Pomocnicze funkcje:
- `reg(0x112D6A)` czyta 16-bitowy rejestr RIU o adresie jak w źródłach MStara;
- `PRINTF(...)` to `printf` oryginału (max 3 argumenty);
- `count()` to licznik czasu.

Na początku i na końcu wypisz znaczniki, np. `MOJA BEGIN` i `MOJA END`. Po nich skrypt pozna koniec wyniku.

### 2. Zbuduj

```sh
./build_probe.sh moja
```
Powstają `moja.bin` (kod pod `0xA3800000`) i `mw_cmds_moja.txt`: komendy `mw.l` z kodem, a na końcu cztery słowa łatki skoku pod `0xA0142468`. Potrzebny jest kompilator `mipsel-linux-gnu-gcc` (GCC 12.2, `mipsel_gcc_12.2/`).

### 3. Złap MBoota

Port szeregowy musi być wolny (zamknij picocom).
```sh
WAIT=180 python3 tools/serial/catch_mboot.py       # potem włącz zasilanie dekodera (pendrive wyjęty)
```
Skrypt wysyła Enter, aż pojawi się `<< MStar >>#`. Z działającego Linuksa można też zrobić `python3 tools/serial/catch_mboot.py reboot`.

### 4. Załaduj oryginał z sondą i uruchom

```sh
python3 tools/serial/run_stockprobe.py tools/baremetal/stockprobe/mw_cmds_moja.txt
```
Skrypt wykonuje w MBoocie:
```
spi_rdc 0x80B00000 0x8001C 0x289977                       # spakowany obraz z flasha do RAM
LzmaDec 0x80B00000 0x289977 0x80000180 0x81000000         # rozpakowanie (oryginał w RAM)
mw.l a3800000 ...   (cała sonda)                           # nasz kod
mw.l a0142468 3c19a380 / a014246c 3739xxxx / a0142470 03200008 / a0142474 0   # łatka Help
go 0x80000224                                              # start oryginału
```
To samo można wpisać ręcznie w picocom: wkleić zawartość pliku `mw_cmds_moja.txt` między `LzmaDec` a `go`.

### 5. Doprowadź oryginał do interesującego stanu

Np. włóż pendrive z muzyką, wejdź pilotem (albo pilotem z telefonu) do odtwarzacza i włącz plik. Oryginał startuje około 20 s.

### 6. Uruchom sondę i zapisz wynik

```sh
python3 tools/serial/stock_help.py binaries/ghidra_stock/moja_wynik.txt "MOJA END" 120
```
Skrypt wysyła `Help` i zbiera wszystko do znacznika końca. Można też po prostu wpisać `Help` w picocom i skopiować wynik. Sondę można uruchamiać wielokrotnie, np. raz przed, a raz w trakcie grania.

### 7. Porównaj z Linuksem

Pod Linuksem te same zakresy rejestrów zrzuca `tools/board/riudump.c` w tym samym formacie co `stockdump`, więc wystarczy `diff`. Przykład: `binaries/ghidra_stock/diff_stockplay_vs_linux_audiotest.txt`. Takie porównania wskazały kolejne brakujące kroki: start DSP, dekoder xPCM, ustawienia SE (`docs/audio.md`).

## Sondy, które powstały (`tools/baremetal/`)

| Sonda | Co robi | Wynik (`binaries/ghidra_stock/`) |
|---|---|---|
| `stockdump/stockdump.S` | asembler, mieści się w 204 B `Help`: zrzut zakresów rejestrów audio i HDMI | `stock_regs_after_boot.txt`, `stock_regs_playing_mp3.txt`, `stock_regs_playing_wav.txt` |
| `stockprobe/stockprobe.c` | rejestry i przeszukiwanie DRAM w poszukiwaniu próbek `test1k.wav` (znaleziony bufor PCM dekodera) | `stock_probe_wav.txt` |
| `stockprobe/stocktrace.c` | ślad czasowy: najszybsze możliwe próbkowanie skrzynek DSP przez 2 s, zmiany do RAM, wypisanie na końcu | `stock_trace_wav2.txt` |
| `stockprobe/memprobe.c` | wskaźniki i zrzuty hex pamięci DSP wokół wskaźnika odczytu | `stock_memprobe_wav.txt` |
| `stockprobe/setuptrace.c` | 25 s śledzenia zmian (max 300 na rejestr), czeka 2 s i wypisuje `GO`, żeby zdążyć włączyć plik | `stock_setuptrace_wav.txt` |
| `stockprobe/szablon.c` | szablon do nowych sond | — |

Starsze sondy mają własne skrypty (`build.sh`, `build_trace.sh`, `build_mem.sh`, `build_setup.sh`). Nowe buduje się ogólnym `build_probe.sh NAZWA`.

## Jak wybrać, co podglądać

Adresy funkcji i rejestrów biorą się z dekompilacji oryginału w Ghidrze (`binaries/ghidra_stock/all.c`, 11 497 funkcji). Szuka się po napisach (np. `HAL_AUDIO_AlgReloadCode`) i po zapisach do rejestrów (`FUN_8000ced4(0x2d9c, …)`). Sonda potwierdza na żywo to, co widać w kodzie: kolejność zapisów, wartości i czas.

Przykład z dźwięku:
1. W kodzie widać, że odtwarzacz zgłasza „dane gotowe” przez `0x2D8C` i komendę `0xE0`.
2. `stocktrace` pokazał, że dzieje się to co 64 ms razem ze zmianą wskaźnika `0x2D6A`.
3. Z obu faktów razem powstał protokół podawania danych w `mstar_audio.h`.
