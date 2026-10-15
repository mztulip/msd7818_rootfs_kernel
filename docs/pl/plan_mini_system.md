# Plan: mini system Linux w flashu SPI (z fbDOOM)

Ustalony 2026-10-14, **jeszcze nie rozpoczęty** (najpierw zajmujemy się dźwiękiem, [audio.md](audio.md)).

## Cel

- Kernel i rootfs we flashu SPI (4 MB), zamiast oryginalnego systemu (eCos). Oryginał można usunąć, bo pełna kopia flasha jest w `binaries/wiwa_hd95_BACKUP.BIN`.
- Rootfs tylko do odczytu: zmiany działają do restartu.
- Pełniejszy rootfs i programy na pendrivie: wgrywam proste skompilowane programy na pendrive i je uruchamiam.
- Jeśli się da: mała zapisywalna część na `/home`.
- fbDOOM z `doom1.wad` w flashu: wystarczy podpiąć klawiaturę USB i grać.
- Kernel na osobnej gałęzi `kernel_mini`: bez modułów, bez sieci; zostają konsola szeregowa, USB (pendrive i klawiatura) i framebuffer. Usunięcie kodu źródłowego niezwiązanego z platformą (inne architektury, sterowniki, których nigdy nie użyjemy).

## Budżet miejsca (zmierzone / szacowane)

Wolne po usunięciu oryginalnego systemu: `0x80000–0x3E0000` = **3,37 MB** (zostają: sboot + MBoot `0x0–0x70000`, logo `0x70000–0x80000`, środowisko MBoota `0x3E0000–0x400000`).

| Element | Teraz | Cel |
|---|---|---|
| kernel LZMA | 2,87 MB (pełna konfiguracja) | ok. 1,0–1,3 MB |
| BusyBox + biblioteki (statycznie, musl) | — | ok. 0,2 MB |
| fbdoom (xz) | 0,16 MB (dynamiczny, glibc + `libSDL`) | ok. 0,2 MB (statyczny, `NOSDL`) |
| doom1.wad (xz) | 4,2 MB → 1,36 MB | 1,36 MB |
| zapisywalne `/home` (JFFS2) | — | 128–256 KB |
| **Razem** | | **ok. 3,0–3,3 MB** |

## Budowa

1. **Jeden obraz: kernel + initramfs (cpio), kompresja LZMA.** MBoot ładuje go poleceniami, które już ma (oryginalny `bootcmd` robi to samo):
   ```
   spi_rdc 0x80B00000 <offset> <rozmiar>
   LzmaDec 0x80B00000 <rozmiar> 0x80000000 <limit>
   go <entry>
   ```
   Rootfs w RAM (initramfs), więc po restarcie zawsze wraca czysty system.
2. **Zapisywalne `/home`:** osobna partycja flasha z JFFS2. Wymaga działającego sterownika flasha SPI w Linuksie. Teraz `/proc/mtd` jest pusty: węzeł `isp` / `flash@0` w DTS jest, ale nic się nie rejestruje. To trzeba zbadać ❓. Plan B: bez zapisu.
3. **`kernel_mini`** (gałąź w `linux_msd781x`): konfiguracja od zera. Zostają `mstar-gopfb`, 8250 (UART), EHCI + usb-storage + HID, squashfs/ext2/vfat, initramfs (xz/lzma), MTD/SPI-NOR + JFFS2 (gdy zadziała). Bez sieci, modułów, ALSA i V4L. Osobno: usunięcie z drzewa nieużywanego kodu (inne `arch/`, sterowniki), żeby repo było mniejsze i czytelniejsze. Rozmiaru obrazu to nie zmienia.
4. **Rootfs:** Buildroot z **musl** i statycznym linkowaniem (osobna konfiguracja), BusyBox, fbdoom zbudowany z `NOSDL` i statycznie, `doom1.wad`. Skrypt startowy:
   - montuje `/home` (JFFS2) i pendrive (`/mnt/usb`, jeśli jest);
   - opcjonalnie od razu uruchamia Dooma na `tty1`.
5. **Zapis do flasha** (za zgodą, nieodwracalne bez kopii): `spi_wrc` z pendrive'a, nowy `bootcmd`. Przywrócenie oryginału: [logo_startowe.md](logo_startowe.md) (te same polecenia) + `binaries/wiwa_hd95_BACKUP.BIN`.

## Etapy

1. `kernel_mini` uruchamiany jeszcze z pendrive'a (`fatload`): sprawdzić rozmiar i działanie (konsola, USB, fb).
2. initramfs z BusyBoxem i fbDOOM (statycznie) wbudowany w kernel: start bez rootfs na pendrivie.
3. Obraz LZMA do flasha, start przez `spi_rdc` + `LzmaDec`.
4. Sterownik flasha w Linuksie i JFFS2 na `/home`.
