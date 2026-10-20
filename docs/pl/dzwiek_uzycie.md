# Dźwięk z Linuksa: jak uruchomić ręcznie

Działa od 2026-10-14 (zweryfikowane na płytce: czysty ton 1 kHz i `test1k.wav` na HDMI). Jak to działa i skąd się wzięło, opisuje `docs/audio.md`. Tu są same kroki.

Dźwięk gra przez DSP SoC-a (DEC dekoduje PCM, SE wysyła na wyjścia), z kodem DSP z fabrycznego firmware'u. Programy potrzebują roota (`/dev/mem`). Za każdym startem programu oba DSP są ładowane i startowane od nowa, więc nic nie trzeba wcześniej inicjalizować. Naraz może działać tylko jeden program audio.

## Pliki (`tools/board/`)

| Plik | Co to |
|---|---|
| `mstar_audio.h` | biblioteka (tylko nagłówek): `maudio_open(rate, ch)`, `maudio_write(buf, frames)`, `maudio_close()` |
| `wavplay.c` | odtwarzacz WAV (16 bit PCM, mono/stereo, 8–48 kHz) |
| `audiotest3.c` | generator tonu (sinus) z diagnostyką rejestrów |
| `audio_init_table.h`, `dsp_fw.h`, `dec03_fw.h`, `se31_fw.h`, `xpcm_fw.h` | tablica inicjalizacji i kod DSP wyciągnięty z firmware'u stocka (nie publikować) |
| `extract_audio_fw.py` | generuje te pięć nagłówków z własnego zrzutu flasha: `python3 tools/board/extract_audio_fw.py binaries/wiwa_hd95_BACKUP.BIN tools/board` (wynik identyczny bajt w bajt z naszymi) |

Na płytce wszystko leży w `/root` i jest skompilowane (`/root/wavplay`, `/root/audiotest3`).

## Wgranie i kompilacja na płytce

Z PC, port szeregowy wolny (bez picocom):
```sh
cd tools/board
for f in mstar_audio.h wavplay.c audiotest3.c audio_init_table.h dsp_fw.h xpcm_fw.h; do
    python3 ../uart_upload.py $f /root/$f
done
gzip -9 -k dec03_fw.h se31_fw.h                        # duże, idą spakowane
python3 ../uart_upload.py dec03_fw.h.gz /root/dec03_fw.h.gz
python3 ../uart_upload.py se31_fw.h.gz /root/se31_fw.h.gz
```
Na płytce:
```sh
cd /root
gunzip -f dec03_fw.h.gz se31_fw.h.gz
gcc -O2 -o wavplay wavplay.c
gcc -O2 -o audiotest3 audiotest3.c -lm
sync                          # ext2 bez dziennika: bez sync binarka po wyłączeniu bywa zepsuta
```
Uwaga: nie uruchamiaj `uart_upload.py` w powłoce z PATH SDK Buildroota (`~/toolchains/mipsel/...`), bo jego `python3` nie ma pyserial.

Wyjście SCART/analog jest po starcie wyciszone przez GPIO44 (zostawia tak MBoot). `mstar_audio.h` i `audiotest3` same je odciszają (`analog_unmute()`), szczegóły w [audio.md](audio.md).

## Ton testowy

```sh
/root/audiotest3 10 1000            # 10 s, 1000 Hz, 48 kHz
/root/audiotest3 10 440 44100       # 440 Hz przy 44,1 kHz
```
Parametry: `[sekundy] [Hz] [częstotliwość próbkowania] [LEAD w bajtach, domyślnie 32768]`. Program co sekundę wypisuje stan DSP. `xPCM decoder loaded`, a potem rosnące `0x2dac` (żądania) i `0x2d66` (sekundy grania) oznaczają, że wszystko działa.

## Plik WAV

```sh
mkdir -p /mnt/fat && mount /dev/sda1 /mnt/fat         # partycja FAT pendrive'a
/root/wavplay /mnt/fat/test1k.wav
```
`wavplay` przyjmuje tylko 16-bitowe PCM WAV, mono lub stereo, 8–48 kHz. Plik może leżeć też na rootfs, np. w `/home`.

## Muzyka z MP3

Na płytce nie ma dekodera MP3 (mpg123, ffmpeg itp.). Na razie trzeba przekonwertować plik na PC:
```sh
ffmpeg -i zniwa_1.mp3 -ac 2 -ar 44100 -sample_fmt s16 zniwa_1.wav
```
Wynik (ok. 10 MB na minutę) wgraj na partycję FAT pendrive'a. Pendrive musi być wtedy podłączony do PC, nie przez UART. Potem uruchom `wavplay /mnt/fat/zniwa_1.wav`.

Do zrobienia później: sprzętowe MP3 w DSP. Stock gra MP3 dekoderem DEC 0x07 `mp3` (CM 0x1C26 B pod 0x200, PM 0x2E92 B pod 0xFFF, X1 0x2A3C B pod 0x8600, X2 0x5070 B pod 0x12600; wybór: `0x8000f348(5)` → `AlgReloadCode(7)`). Surowe bajty MP3 podaje się tym samym protokołem żądań co PCM (bez parametrów xPCM). Pliki MP3 na płytce leżą w `/home/mp3/`.

## Doom z dźwiękiem (działa, 2026-10-15)

Efekty dźwiękowe grają naraz na HDMI i na SCART (zweryfikowane na płytce). Kod jest w `fbDOOM/fbdoom/i_mstarsound.c` (commit `cf08a57` w forku fbDOOM): 8 kanałów, próbki DMX z WAD-a przeliczane na 48 kHz stereo, głośność i panorama, miks w osobnym wątku przez `mstar_audio.h`, opóźnienie ok. 64 ms (`MAUDIO_LEAD` 12288 B). Muzyki jeszcze nie ma.

Budowanie (na PC, w czystej powłoce, żeby PATH SDK nie przesłonił Pythona z pyserial przy późniejszym `uart_upload.py`):
```sh
cd fbDOOM/fbdoom
(export PATH=~/toolchains/mipsel/mipsel-buildroot-linux-gnu/bin:$PATH
 make clean; make NOSDL=1 MSTAR_SOUND=1      # clean konieczny: Makefile nie przebudowuje po zmianie flag (inaczej i_sound.o bez dźwięku); bierze mstar_audio.h i nagłówki DSP z ../../tools/board
 mipsel-buildroot-linux-gnu-strip -o fbdoom_snd fbdoom)    # na płytce jako /home/fbdoom (jedyna wersja)
gzip -9 -k fbdoom_snd
python3 ../../tools/uart_upload.py fbdoom_snd.gz /home/fbdoom.gz
```
Na płytce:
```sh
cd /home && gunzip -f fbdoom.gz && chmod +x fbdoom && sync
./fbdoom -iwad doom1.wad                 # z konsoli szeregowej (picocom)
openvt -c 2 -s -w -- sh -c 'cd /home; ./fbdoom -iwad doom1.wad'    # z klawiaturą USB
```
`-nosound` albo `-nosfx` wyłącza dźwięk. Start trwa kilka sekund dłużej, bo ładuje kod do obu DSP. Po `I_MStar_InitSound: starting the DSPs` gra nie wypisuje już nic więcej, i tak ma być.

Binarki z dźwiękiem **nie publikujemy**, bo zawiera kod DSP MStara z fabrycznego firmware'u. W forku fbDOOM jest tylko moduł, a nagłówki z kodem DSP trzeba wyciągnąć z własnego zrzutu flasha (`docs/audio.md`).

## Pomysły na MP3

Teraz MP3 trzeba konwertować na PC (`ffmpeg`, sekcja wyżej). Możliwe drogi na samej płytce, od najciekawszej:

1. **Sprzętowy dekoder MP3 w DSP**, tak jak robi to stock. CPU prawie nic nie robi.
   - Zamiast xPCM ładujemy dekoder DEC **0x07 `mp3`**. Wybór w stocku: `0x8000f348(5)` → `AlgReloadCode(7)`; handshake 0xF0/0x33, 0xF1/0x77 jak dla xPCM.

     | Segment | Rozmiar | Adres DSP |
     |---|---|---|
     | CM | 0x1C26 B | 0x200 |
     | PM | 0x2E92 B | 0xFFF |
     | X1 | 0x2A3C B | 0x8600 |
     | X2 | 0x5070 B | 0x12600 |

     X1 i X2 trafiają do DRAM pod adres baza DEC + adres·3, tak jak przy 0x31/0x03. Dane są w `binaries/audio_dsp/07_mp3_*.bin`; brać prefiks o podanej długości, bo pliki są ×3 za długie.
   - Parametrów xPCM nie ustawiamy, bo DSP sam czyta nagłówki ramek MP3.
   - Na żądania DSP (0x2DAC, 0x2D6A, 0x2D6C) podajemy **surowe bajty pliku**, bez dekodowania; protokół jak w `maudio_write`.
   - Zamiast tempa czasu rzeczywistego wystarczy odpowiadać na żądania, bo DSP sam wie, ile bajtów zużył. Trzeba to sprawdzić, bo w stocku odtwarzacz karmi co ~64 ms.
   - Koniec pliku: mailbox 5 (0x2D8A) = rozmiar ostatniej, krótkiej porcji / 8 (`0x802bf9d0`).
   - Częstotliwość, liczbę kanałów i czas odtwarzania DSP podaje w mailboxach (0x2D66 = sekundy, 0x2D68 = ms/4; reszta do ustalenia przez `GetCommAudioInfo` w `all.c`, okolice `0x8001194c`).
   - Do sprawdzenia też ID dekodera w 0x2DBB po załadowaniu (dla xPCM 0xC0).

   Najszybciej byłoby dopisać do `mstar_audio.h` funkcję `maudio_open_mp3()` i program `mp3play`.
2. **`mpg123` z Buildroota plus prosty `pcmplay`**, czyli odpowiednik `wavplay`, który czyta surowe PCM ze stdin: `mpg123 -s plik.mp3 | pcmplay 44100 2`. Wymaga przebudowania rootfs z pakietem mpg123. CPU 34Kc ma FPU, a mpg123 ma też dekoder stałoprzecinkowy.
3. **`minimp3`**, jednoplikowy dekoder (CC0): wkompilować go w `wavplay`, kompilacja na płytce przez gcc. Nie trzeba przebudowywać rootfs.

## Pomysł na muzykę w Doomie

Muzyka Dooma (lumpy `D_*`, format MUS) to nuty, nie nagranie. fbDOOM ma `i_oplmusic.c` (emulacja syntezatora FM OPL2/OPL3, jak AdLib/Sound Blaster). Trzeba dołożyć emulator OPL (biblioteka `opl` z Chocolate Doom, `opl3.c` Nuked OPL3 albo DBOPL) i miksować jego próbki w tym samym wątku co efekty, w `MixThread` w `i_mstarsound.c`. Do sprawdzenia, czy emulacja OPL3 zmieści się w czasie CPU obok gry; DBOPL jest lżejszy od Nuked.
