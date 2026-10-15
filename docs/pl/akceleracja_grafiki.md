# Akceleracja grafiki w MSD7818: co jest w chipie i pomysły na wykorzystanie

Oznaczenia: ✅ potwierdzone na płytce, 🔍 z kodu, ❓ hipoteza. Stan na 2026-10-15.

## Co jest w chipie

**GPU 3D: brak.** W BSP Kronusa (`mstar-mboot-master/MstarCore/bsp/k1/lib/`) nie ma biblioteki 3D ani śladu po Mali. To tani układ do dekodera DVB-T. Rdzenie 3D MStar dawał dopiero w układach do telewizorów 🔍.

Bloki 2D i obrazu:

| Blok | Do czego | Ślad | Pod Linuksem |
|---|---|---|---|
| **GE** (Graphics Engine) | blitter 2D: kopiowanie i wypełnianie prostokątów, mieszanie z alfą, rozciąganie, obrót, konwersja formatów kolorów, linie | `libapiGFX.a` w BSP (z symbolami); log MBoota: `GE_SetOnePixelMode`; oryginalne menu rysuje przez GE 🔍 | nieużywany |
| **GOP** (Graphics Output Path) | sprzętowe warstwy nakładane na obraz: okna GWIN, skalowanie, alfa, kolor przezroczysty | `libapiGOP.a`, [grafika_gop.md](grafika_gop.md) | ✅ GOP3 = nasz `/dev/fb0` (`mstar-gopfb`, podwójny bufor, vsync) |
| **JPD** | sprzętowy dekoder JPEG | MBoot dekoduje nim logo (`JPD_SetStatus` w logu), `libapiJPEG.a` | nieużywany |
| **MVD / VDEC, MVOP, XC** | dekoder wideo MPEG-2/H.264, wyjście wideo, skaler obrazu wideo | `libapiVDEC.a`, `libdrvMVOP.a`, `libapiXC.a` | nieużywane |

Dziś Linux i fbDOOM rysują wszystko procesorem.

## Pomysł 1: skalowanie w GOP dla Dooma (największy zysk)

### Teraz

Doom liczy klatkę 320×200 (8 bit, paleta). `i_video_fbdev.c` zamienia ją na RGB565, powiększa ×5 do 1600×1000 i kopiuje 3,2 MB do framebuffera. Pomiar na płytce ✅ (`fbDOOM/README.md`):
- powiększanie ×5 z 16-bitowymi zapisami: ok. **9 ms** na klatkę;
- kopiowanie 1600×1000 do framebuffera: ok. **12 ms** (269 MB/s).

Przy 35 klatkach/s to około 3/4 czasu procesora tylko na przesuwanie pikseli. Do tego ten ruch w pamięci był przyczyną zakłóceń obrazu, które trzeba było naprawiać priorytetem MIU ([miganie_obrazu_miu.md](miganie_obrazu_miu.md)).

### Pomysł

Niech GOP sam rozciąga mały bufor na ekran:
- bufor GWIN o rozmiarze klatki Dooma, 320×200 RGB565, czyli **128 KB** zamiast 3,2 MB;
- **okno rozciągania** = rozmiar źródła, a **współczynniki skali** dobrane tak, żeby wyjście miało np. 1440×1080 (proporcje 4:3 jak na monitorze z 1993 r., cała wysokość ekranu) albo 1600×1000 jak dziś;
- procesor robi tylko konwersję palety, 64 000 pikseli na klatkę. Kopiowania 3,2 MB nie ma.

Rejestry skalowania są już rozpoznane ([grafika_gop.md](grafika_gop.md), „Bank 0: rozciąganie i skalowanie” i wyniki dla banku 9 = GOP3) 🔍✅:
- okno rozciągania: `0x1F60` (szerokość/2) i `0x1F62` (wysokość). Dla GOP3 MBoot ustawia `0x03C0` (1920) i `0x0438` (1080) ✅;
- skala H `0x1F6A` i skala V `0x1F6C`: `ceil(src × 4096 / dst)`, `0x1000` = 1:1;
- przykład: 320 → 1440 to skala H = `ceil(320·4096/1440)` = `0x038F`; 200 → 1080 to skala V = `0x02F7`.

### Szacowany zysk ❓

Z ok. 21 ms na klatkę (powiększanie + kopiowanie) zostaje poniżej 1 ms (konwersja palety do 128 KB). Doom miałby zapas do 35 klatek/s nawet z dźwiękiem i muzyką OPL. Prawie znika też obciążenie pamięci, a z nim ryzyko zakłóceń obrazu.

### Plan

1. **Test ręczny przez `devmem`** (jak w „Testy do zrobienia” w `grafika_gop.md`):
   - na działającym Linuksie zmniejszyć okno GWIN do 320×200 i ustawić pitch 640;
   - ustawić okno rozciągania i skalę w banku 9 (force write `0x1FFE` bit 9);
   - sprawdzić, czy obraz rozciąga się na cały ekran.
2. **Jakość powiększania:** czy GOP interpoluje (rozmyte piksele), czy powiela piksele (ostre bloki, jak w oryginale). `0x1F50` bit 10 może przełączać tryb filtra ❓. Dla Dooma lepsze są ostre piksele.
3. **Sterownik:** `mstar-gopfb` przyjmuje `FBIOPUT_VSCREENINFO` z mniejszym `xres`/`yres` i sam programuje okno, pitch, okno rozciągania i skalę, przełączając w przerwaniu vsync jak przy podwójnym buforze. Po wyjściu z gry wraca do 1920×1080 dla konsoli.
4. **fbDOOM:** gdy framebuffer ma 320×200, nie skaluje (`scaling = 1`), więc obecna ścieżka 16 bpp działa bez zmian. Wystarczy przed startem zrobić `fbset 320x200` albo dodać opcję w grze.

## Pomysł 2: sterownik GE (blitter 2D)

GE potrafi sprzętowo kopiować i wypełniać prostokąty, mieszać z alfą i konwertować formaty. Pod Linuksem przydałby się do:
- **konsoli tekstowej:** przyspieszone `fb_copyarea` (przewijanie) i `fb_fillrect` (czyszczenie) w `mstar-gopfb`;
- **programów rysujących dużo prostokątów:** proste GUI, wykresy (`rfscan`).

Rejestry i kolejność wywołań można odczytać z `libapiGFX.a` (ma symbole) i z oryginalnego firmware'u, który rysuje menu przez GE. Ta sama metoda zadziałała przy audio: dekompilacja plus sondy na płytce ([sondy_stock.md](sondy_stock.md)).

Dla Dooma GE daje mniej niż pomysł 1: rozciąganie przez GE dalej zapisywałoby 3,2 MB do pamięci na klatkę, tylko zamiast procesora robiłby to blitter. Mógłby za to zastąpić procesor przy konwersji palety ❓, jeśli GE obsługuje formaty z paletą (I8 → RGB565).

## Pomysł 3 (dalej): sprzętowy JPEG

JPD dekoduje JPEG-i, MBoot robi tak z logo. Pod Linuksem mógłby szybko wyświetlać zdjęcia, np. przeglądarka z pendrive'a. Bez sterownika i bez rozpoznanych rejestrów to na razie tylko ciekawostka ❓.
