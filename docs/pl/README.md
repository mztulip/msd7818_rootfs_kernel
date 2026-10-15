# Dokumentacja MSD7818 (WIWA HD95)

> **Uwaga do tej kopii.** To notatki z prac nad dekoderem WIWA HD95 (MStar MSD7818), po polsku, kopiowane z roboczego katalogu projektu. Ścieżki w rodzaju `tools/…`, `binaries/…`, `fbDOOM/…` i `linux_msd781x/…` odnoszą się do tamtego katalogu, nie do tego repo:
> - źródła kernela: https://github.com/mztulip/linux_msd781x (gałąź `mstar_v6_7_rebase`);
> - fbDOOM: https://github.com/mztulip/fbDOOM;
> - gotowe binarki i angielskie opisy: katalog wyżej (`../`).
>
> Zrzutów flasha, firmware'u DSP i innego kodu MStara tu nie ma.
>
> *English: these are the Polish working notes of the project, copied as they are. The English write-ups are in `docs/` one level up.*


Ustalenia z analizy MBoota, zrzutu RAM (`binaries/RAM87_DU.RAM`) i bibliotek BSP MStara dla Kronusa (`mstar-mboot-master/MstarCore/bsp/k1/`).

Każda informacja ma oznaczenie pewności:
- ✅ **potwierdzone** na płycie (komenda w U-Boocie dała oczekiwany wynik),
- 🔍 **z kodu**: wynika z deasemblacji bibliotek albo ze źródeł, ale nie było testowane na płycie,
- ❓ **hipoteza**: do sprawdzenia.

| Plik | Zawartość |
|---|---|
| [firmware_dsp_pochodzenie.md](firmware_dsp_pochodzenie.md) | Skąd pochodzi kod DSP audio (zrzut flasha → LZMA → Ghidra → tabele `pau_info`) i dlaczego go nie publikujemy |
| [akceleracja_grafiki.md](akceleracja_grafiki.md) | Bloki graficzne w chipie (GE 2D, GOP, JPD, wideo; brak 3D) i pomysły: skalowanie GOP dla Dooma, sterownik GE |
| [rdzenie_firmware.md](rdzenie_firmware.md) | Przegląd rdzeni w SoC (MIPS, PM51, 8051 demodulatora, DSP audio DEC/SE), ich firmware, co zmieniono we flashu, a co tylko w RAM |
| [riu.md](riu.md) | Jak liczyć adresy rejestrów (RIU, PM i non-PM, przeliczanie z kodu MStara) |
| [mxl5007t.md](mxl5007t.md) | Tuner MxL5007T: odczyt/zapis rejestrów krok po kroku (devmem i i2ctransfer), inicjalizacja, strojenie |
| [i2c.md](i2c.md) | Sprzętowe I2C (MIIC): porty, rejestry, pinmux, tuner MxL5007T |
| [przyciski_sar.md](przyciski_sar.md) | Panel przedni: przyciski POWER/CH+/CH− i wyświetlacz 7-seg. na układzie FD650 (GPIO na pinach SAR); SAR ADC |
| [gpio.md](gpio.md) | Co wiadomo o GPIO |
| [demodulator_if.md](demodulator_if.md) | Tor IF → ADC → demodulator DVB-T (8051 + DSP), skrzynka pocztowa z Linuksa, rejestr sygnału `0x2098`, widmo RF przemiatane (`rfscan`) |
| [ir_pilot.md](ir_pilot.md) | Pilot IR: dekoder w PM, firmware PM51, rejestry, drogi pod Linuksem |
| [audio.md](audio.md) | Blok audio (Vivaldi/MAD, 2× DSP), czy MIPS może grać dźwięk |
| [grafika_gop.md](grafika_gop.md) | GOP: rejestry bufora, skalowania i vsync; plan lepszego framebuffera |
| [skanowanie_kanalow.md](skanowanie_kanalow.md) | Jak oryginalny system szuka kanałów DVB-T: tabele częstotliwości, maszyna stanów skanu, lock demodulatora, tablice PAT/PMT/SDT/NIT |
| [plan_mini_system.md](plan_mini_system.md) | Plan (nierozpoczęty): mini Linux z fbDOOM w flashu SPI zamiast oryginalnego systemu |
| [miganie_obrazu_miu.md](miganie_obrazu_miu.md) | Dlaczego obraz migał w Doomie: GOP przegrywał z procesorem w arbitrażu pamięci (MIU); dochodzenie krok po kroku i poprawka priorytetów |
| [doom.md](doom.md) | fbDOOM: podwójny bufor, ~35–50 kl./s, sterowanie przez konsolę szeregową, uruchamianie przez `openvt` |
| [sterownik_gopfb.md](sterownik_gopfb.md) | Jak krok po kroku powstał sterownik fbdev `mstar-gopfb` z podwójnym buforowaniem i vsync |
| [logo_startowe.md](logo_startowe.md) | Własne logo startowe MBoota: JPEG we flashu `0x70000`, `LOGO_BIN_SIZE`, wgranie i powrót do logo WIWA |
| [przesylanie_uart.md](przesylanie_uart.md) | Wgrywanie plików na płytę przez konsolę UART (gzip + base64 + md5), `tools/uart_upload.py` |
| [ghidra_funkcje.md](ghidra_funkcje.md) | Adresy funkcji BSP w U-Boocie i jak je odtworzyć |

Narzędzia:
- `tools/match_libs.py` szuka funkcji z plików `.o` w surowym zrzucie pamięci.
- `tools/ghidra_mstar_labels.py` to skrypt Ghidry, który nadaje nazwy znalezionym funkcjom.
- `tools/ghidra/Seed8051.java` i `Export8051.java` to skrypty do analizy firmware'u 8051 demodulatora bez interfejsu (opis w [demodulator_if.md](demodulator_if.md)).
- `tools/baremetal/logofix/` to program bare-metal uruchamiany z MBoota (`go 0x80F00000`) zamiast `osd_create`. Konwertuje logo WIWA do RGB565 i ustawia format GOP (opis w [grafika_gop.md](grafika_gop.md#logo-wiwa-przy-starcie-logofix-)).
- `tools/board/` to programy uruchamiane na płycie (kompilacja na płycie: `gcc -O2 -o X X.c [-lm]`). `frontpanel.c` obsługuje przyciski i wyświetlacz panelu, `regwatch.c` śledzi zmiany rejestrów, `dmdmbx.c` daje dostęp do XDATA 8051 demodulatora przez skrzynkę pocztową, a `rfscan.c` to widmo RF przemiatane tunerem MxL5007T i rysowane na ekranie (opis w [demodulator_if.md](demodulator_if.md)).
- `binaries/rfscan/` zawiera zapisane skany: bez anteny (linia bazowa) i z anteną.
- `binaries/demod_fw/` zawiera firmware 8051 (DVB-T, DVB-C) wycięty z `libdrvDEMOD.a`.
- [dzwiek_uzycie.md](dzwiek_uzycie.md): dźwięk z Linuksa w praktyce: `audiotest3`, `wavplay`, MP3 przez konwersję, Doom z dźwiękiem (działa), pomysły na MP3 i muzykę
- [sondy_stock.md](sondy_stock.md): jak podglądać rejestry i pamięć w działającym oryginalnym firmware (łatka `Help`, sondy pod `0xA3800000`, szablon, skrypty)
