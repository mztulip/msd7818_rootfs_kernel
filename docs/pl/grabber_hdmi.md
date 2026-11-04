# Grabber HDMI: podgląd obrazu

Grabber USB 3 UltraSemi `345f:2130` (UVC, obraz bez dodatkowych sterowników). Zweryfikowane 2026-11-04: podgląd działa, obraz z płytki widoczny.

```sh
ffplay -f v4l2 -input_format yuyv422 -video_size 1920x1080 -framerate 50 \
  -fflags nobuffer -flags low_delay -framedrop \
  /dev/v4l/by-id/usb-UltraSemi_USB3_Video_20210623-video-index0
```

- Komenda pokazuje tylko obraz, bez dźwięku.
- `-framerate 50` pasuje do płytki (HDMI 50 Hz). Dla źródła 60 Hz ustaw `-framerate 60`.
- Ścieżka `by-id` nie zmienia się po ponownym podłączeniu, w przeciwieństwie do numeru `/dev/video2`.
- Urządzenie może otworzyć naraz tylko jeden program. Przy włączonym podglądzie inne próby odczytu kończą się błędem `Device or resource busy`.

## Obraz i dźwięk razem

```sh
ffmpeg -hide_banner -loglevel error \
  -f v4l2 -input_format yuyv422 -video_size 1920x1080 -framerate 50 -thread_queue_size 64 \
  -i /dev/v4l/by-id/usb-UltraSemi_USB3_Video_20210623-video-index0 \
  -f alsa -sample_rate 48000 -ch_layout stereo -thread_queue_size 1024 -i hw:CARD=Video,DEV=0 \
  -c:v rawvideo -c:a pcm_s16le -f nut - | \
  ffplay -hide_banner -fflags nobuffer -flags low_delay -framedrop -i -
```

Koniec: `q` w oknie albo `Ctrl+C` w terminalu. Sprawdzone 2026-11-04: `ffplay` startuje z obu strumieniami, A-V ok. −5 ms. Ton z płytki przechodzi przez ten potok tak samo jak przy nagraniu bez potoku (tor do `ffplay` zastąpiony wtedy zapisem do pliku, żeby nie grać na głośnikach). `mpv` z `--audio-file` odrzucone: wejście ALSA kończyło w stanie XRUN.

## Dźwięk

Karta ALSA `Video`, tylko S16_LE, 2 kanały, 48 kHz. W FFmpeg 9 wejście ALSA nie ma opcji `-channels`, trzeba podać `-ch_layout`:

```sh
ffplay -nodisp -f alsa -sample_rate 48000 -ch_layout stereo -i hw:CARD=Video,DEV=0
```

Zweryfikowane 2026-11-04 nagraniem z wejścia grabbera, z płytki przez HDMI:

- `owntest` (1 kHz, potem 440/660 Hz, amplituda 16384): w nagraniu właściwe częstotliwości i pik 16384, poza tonem same zera. Tor jest cyfrowy, próbki dochodzą bez zmiany poziomu.
- fbDOOM ze starego rootfs (2026-10-20, bez muzyki), strzelanie: pik 4096, poza grą może być cisza.
- fbDOOM z `rootfs_20261104.img` we flashu: muzyka pik ok. 2200, muzyka ze strzałami ok. 5400, po `Ctrl+C` same zera.
