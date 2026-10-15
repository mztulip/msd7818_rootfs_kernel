# Miganie obrazu w Doomie: priorytety pamięci (MIU)

Opis dochodzenia, dlaczego w fbDOOM ekran co chwilę „się rozjeżdżał”, i jak to naprawić. Napisany dla kogoś, kto nie zna budowy tego układu. ✅ Przyczyna i poprawka sprawdzone na płycie 2026-10-14.

## Objaw

Po dodaniu podwójnego buforowania ([sterownik_gopfb.md](sterownik_gopfb.md)) i przyspieszeniu fbDOOM ([doom.md](doom.md)) co chwilę pojawiały się błyski: obraz przesunięty w bok, „śmieci” z innej części obrazu nałożone na normalny, fragmenty środka gry w czarnych ramkach po bokach. Wyglądało to jak telewizor analogowy, który gubi synchronizację. Działo się to w demie, w grze, a nawet na nieruchomym obrazie w grze.

## Trochę teorii: kto korzysta z pamięci

Płyta ma jedną kość pamięci DRAM (64 MB), z której korzysta naraz wiele bloków układu:
- **procesor** (MIPS): program, dane, rysowanie Dooma;
- **GOP** (warstwa grafiki): **ciągle czyta** obraz z pamięci i wysyła go na HDMI. Dla 1920×1080, 2 bajty na piksel, 50 razy na sekundę to ok. **207 MB/s**, bez przerwy;
- USB, dekoder wideo, dźwięk, demodulator itd.

DRAM w danej chwili obsługuje tylko jedno żądanie. O tym, kto ma pierwszeństwo, decyduje **arbiter** w kontrolerze pamięci, który MStar nazywa **MIU** (Memory Interface Unit). Każdy blok to dla MIU **klient**. Klienci są podzieleni na **grupy** po 16.

GOP ma mały bufor FIFO na dane, które za chwilę trafią na ekran. Jeśli przez jakiś czas nie dostanie z pamięci kolejnych danych (bo pamięć obsługuje kogoś innego), FIFO się opróżnia, a GOP wysyła na HDMI złe piksele albo przesunięte linie. Mówi się wtedy o **niedoborze** (underflow). W pamięci obraz jest dobry, psuje się tylko to, co trafiło na ekran.

DRAM najszybciej obsługuje dostępy **po kolei** (ciągła kopia). Najwolniej obsługuje dostępy **rozrzucone**, bo każdy oznacza otwarcie innego „wiersza” pamięci, co kosztuje czas. Doom rysuje ściany **kolumnami**: piksel, potem 320 bajtów dalej, potem znów 320 bajtów dalej… Każdy zapis trafia gdzie indziej. Dla pamięci to najgorszy możliwy wzór.

## Jak doszliśmy do przyczyny (kolejne hipotezy)

Każdy test robiłem na płycie, a Ty patrzyłeś na ekran. Programy są w `tools/board/`.

| # | Hipoteza | Test | Wynik |
|---|---|---|---|
| 1 | konsola tekstowa rysuje po buforze (kursor) | Doom przełącza konsolę w `KD_GRAPHICS`, potem test z odpiętą konsolą | dalej miga ❌ |
| 2 | sterownik gubi vsync | `flipcheck`: 1000 przełączeń, pomiar odstępów | wszystkie ok. 20 ms, nic nie ginie ❌ |
| 3 | za mała przepustowość przy zapisie całych klatek | `bwtest`: kopia 270 MB/s do ukrytego bufora, na ekranie stała szachownica | spokojnie ❌ |
| 4 | przełączenie w złym momencie klatki | `phaseflip`: przełączanie 1–19,6 ms po vsync, oba bufory identyczne | spokojnie ❌ |
| 5 | rejestry adresu zmieniają się „na żywo” | zapis adresu bez triggera (`devmem`) | obraz się nie ruszył: rejestry są buforowane ❌ |
| 6 | stary bufor jest jeszcze wyświetlany po vsync | Doom z opóźnieniem 8 ms / 20,5 ms przed rysowaniem | przy 8 ms miganie tylko na dole, przy 20,5 ms dalej miga ❌ (częściowy trop) |
| 7 | zły sposób przełączania | podejrzane w bibliotece MStar (GFLIP): przełączanie w przerwaniu vsync z „force write”. Przepisałem sterownik na ten sposób | dalej miga ❌ (ale sposób lepszy, został) |
| 8 | Doom psuje pamięć obrazu | `fbborder`: w trakcie gry sprawdza, czy czarne ramki w pamięci są czarne | w pamięci wszystko czyste: **psuje się tylko wyświetlanie** |
| 9 | czerń przezroczysta, prześwituje inna warstwa | `keytest`: czarny ekran + zapis do ukrytego bufora | czarny, nic nie prześwituje ❌ |
| 10 | Doom bez rysowania | Doom liczy demo, ale nie dotyka ekranu (`-nodraw`); na ekranie stała szachownica | **szachownica miga!** Winne jest samo obciążenie, a nie wyświetlanie Dooma |
| 11 | inne warstwy GOP czytają pamięć | `gopdump`: zrzut wszystkich banków GOP | okna GOP0–2 wyłączone ❌ |
| 12 | sam procesor (temperatura, zasilanie) | 2 pętle `while :; do :; done` | spokojnie ❌ |
| 13 | rodzaj dostępu do pamięci | `randhog`: losowe zapisy / losowe odczyty / **zapisy kolumnami co 320 B** | zapisy losowe: raz mignęło, odczyty: spokojnie, **kolumny: chaos na ekranie** ✅ |

Wniosek z testów 10 i 13: **rozrzucone zapisy procesora (jak przy rysowaniu kolumnami w Doomie) zabierają pamięć GOP-owi**. GOP nie nadąża z czytaniem obrazu, więc na HDMI idą przesunięte i złe linie. Zwykłe kopiowanie (test 3) jest dla pamięci łatwe, dlatego wcześniej nie migało. Mniejszy obraz w Doomie migał mocniej, bo gra liczyła wtedy więcej klatek na sekundę, a więc robiła więcej rozrzuconych zapisów.

Test 13 to **powtarzalny test bez Dooma**:
```sh
/root/bwtest 1                                    # szachownica na ekranie (konsola odpięta)
/root/randhog 20 8 c & /root/randhog 20 8 c       # 2 procesy, zapisy kolumnami: chaos na ekranie
```

## Rejestry MIU, które znalazłem

Źródło: biblioteka MStar `mstar-mboot-master/MstarCore/bsp/k1/lib/libdrvMIU.a` (ma symbole). Plik `halMIU.o` deasemblowałem `mipsel-linux-gnu-objdump -dr`:
- `HAL_MIU_SetHPriorityMask` zapisuje bit klienta w rejestrze **`0x1248 + grupa × 0x20`**: maska wysokiego priorytetu, 1 = bez priorytetu;
- `HAL_MIU_SetGroupPriority` zapisuje bajt w rejestrze **`0x12FE`** (kolejność grup) i przełącza w nim bit 8, co zatwierdza zmianę;
- tablica `clientTbl` (48 wpisów `grupa<<4 | bit`) pokazuje, że klienci GOP (zapis, odczyt GOP0/1/2) są w **grupie 1, bity 4–7**. Ten sam układ jest w nagłówkach innych układów MStar (`MIUSEL_*`).

Adresy dla procesora: MIU0 to bank `0x1012`, czyli w Linuksie fizycznie **`0x1F202400`** (rejestr `0x12xx` → `0x1F200000 + 0x12xx × 2`).

| Rejestr | Adres (Linux) | Przed | Znaczenie |
|---|---|---|---|
| `0x1248` | `0x1F202490` | `0xFFFF` | grupa 0: maska wysokiego priorytetu |
| `0x1268` | `0x1F2024D0` | `0xFFFF` | **grupa 1 (GOP)** |
| `0x1288` | `0x1F202510` | `0xFFFF` | grupa 2 |
| `0x12A8` | `0x1F202550` | `0xFFFF` | grupa 3 |
| `0x12FE` | `0x1F2025FC` | `0xE0E4` | młodszy bajt: kolejność grup (4 × 2 bity), `0xE4` = 0,1,2,3; bit 8 zatwierdza |

Po starcie z MBoota **żaden klient nie miał wysokiego priorytetu** (same `0xFFFF`), a grupa z GOP była dopiero druga w kolejności. Oryginalne oprogramowanie dekodera pewnie ustawia to samo przy starcie aplikacji, a MBoot tego nie robi.

## Eksperymenty z priorytetami (test `randhog … c`)

| Ustawienie | Ekran |
|---|---|
| domyślne | chaos |
| wysoki priorytet tylko dla grupy 0, 2 albo 3 | chaos (bez zmian) |
| **wysoki priorytet dla grupy 1** (`0x1F2024D0` = `0x0000`) | górna część spokojna, dół dalej chaos |
| grupa 1 + kolejność grup 1,0,2,3 (`0x12FE` = `0xE0E1`), bez zatwierdzenia | pierwszy wiersz w miarę spokojny |
| **grupa 1 + kolejność 1,0,2,3 zatwierdzona bitem 8** (`0xE1E1`, potem `0xE0E1`) | **stabilnie**, ledwo widoczny drobny szum |

Doom z tymi ustawieniami: mały obraz prawie idealny (pojedyncza, ledwo widoczna linia), duży obraz spokojny. Krótkie linie widać tylko przy wczytywaniu gry, kiedy pracuje jeszcze USB.

Ręcznie (do testów, do restartu):
```sh
devmem 0x1F2024D0 16 0x0000      # grupa 1 (GOP): wysoki priorytet
devmem 0x1F2025FC 16 0xE1E1      # kolejność grup 1,0,2,3 + bit 8
devmem 0x1F2025FC 16 0xE0E1      # bit 8 z powrotem = zatwierdzenie
```
Przywrócenie: `0xFFFF` pod `0x1F2024D0`, `0xE1E4` i potem `0xE0E4` pod `0x1F2025FC`.

## Poprawka na stałe: w sterowniku `mstar-gopfb`

Węzeł w DTS dostał drugi zakres rejestrów (MIU0):
```dts
gop3-fb@203e00 {
	compatible = "mstar,msd7816-gop3-fb";
	reg = <0x203e00 0x200>,		/* GOP regs 0x1F00-0x1FFF */
	      <0x202400 0x200>;		/* MIU0 regs 0x1200-0x12FF (arbitration) */
	interrupts-extended = <&intc_irq 20 IRQ_TYPE_LEVEL_HIGH>;	/* GOP vsync */
	...
};
```
`probe` wywołuje `gopfb_miu_priority()`: zeruje maskę grupy 1, wpisuje kolejność `0xE1` z impulsem na bicie 8 i wypisuje w `dmesg`:
`MIU: group 1 high priority (mask 0xffff -> 0x0000), order 0xe1`.
Zakres mapowany jest zwykłym `devm_ioremap`, bez rezerwacji, bo węzeł `miu@202400` z `common.dtsi` opisuje ten sam obszar.

## Uwagi

- Wysoki priorytet ma cała grupa 1, czyli także pozostali jej klienci: wg innych układów MStar to dekoder wideo (MVOP), skaler i VE. Przy obecnym użyciu (Linux bez wideo) to nie przeszkadza. Do sprawdzenia, czy wystarczyłyby same bity 4–7 (maska `0xFF0F`) ❓.
- Nie wiadomo jeszcze, który bit to dokładnie odczyt GOP3 ❓. W tablicy MStar dla tego układu nie ma osobnego „GOP3_R”.
- Przy okazji sterownik przełącza bufory tak jak MStar GFLIP (przerwanie vsync + force write). Nie to było przyczyną, ale to sposób zgodny z producentem i zostaje.
- Narzędzia z dochodzenia (`tools/board/`): `flipcheck`, `bwtest`, `phaseflip`, `fullflip`, `fbborder`, `keytest`, `gopvsync`, `gopdump` (uwaga: przełączanie banków chwilowo zaburza obraz), `randhog`.
- Od 2026-10-14 to domyślny kernel: `vmlinux.bin` = `vmlinux_gopmiu.bin`, `bootlinux` kończy się na `go 0x805e8b04` (`saveenv`). Po resecie `dmesg` pokazuje linię `MIU: group 1 high priority … order 0xe1`.
