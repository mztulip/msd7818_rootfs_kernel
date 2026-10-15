# Jak powstał sterownik framebuffera z podwójnym buforowaniem (`mstar-gopfb`)

Opis krok po kroku, od zbadania sprzętu do działającego sterownika. Każdy krok da się powtórzyć na płycie. Źródło sterownika: `linux_msd781x/drivers/video/fbdev/mstar-gopfb.c`, publicznie w https://github.com/mztulip/linux_msd781x/blob/mstar_v6_7_rebase/drivers/video/fbdev/mstar-gopfb.c. Szczegóły rejestrów są w [grafika_gop.md](grafika_gop.md).

Stan: ✅ działa na WIWA HD95, domyślny kernel od 2026-10-14 (`vmlinux.bin` #9, entry `0x805e87e4`).

**Aktualizacja (2026-10-14, później):** kroki 9–10 opisują pierwszą wersję (przełączanie triggerem, odpytywanie ACK). W fbDOOM pojawiało się miganie. Przyczyną okazał się arbitraż pamięci, a nie sposób przełączania ([miganie_obrazu_miu.md](miganie_obrazu_miu.md)). Obecna wersja sterownika (commit „flip in the vsync IRQ, raise GOP memory priority”, kernel `vmlinux_gopmiu.bin`, entry `0x805e8b04`):
- **przełącza jak MStar GFLIP:** `FBIOPAN_DISPLAY` tylko zapamiętuje adres, a obsługa przerwania vsync GOP (IRQ 20, bank 9 rejestr `0x1F10`: bity `[1:0]` = 2 włączają, bit 8 = flaga vsync, kasowana przez maskowanie i odmaskowanie) wpisuje go z bitem force write (`0x1FFE` bit 9). `FBIO_WAITFORVSYNC` śpi na kolejce oczekujących (`wait_event`) zamiast odpytywać;
- **ustawia priorytety MIU:** drugi zakres `reg` w DTS (`<0x202400 0x200>`) i `gopfb_miu_priority()`.

## 0. Punkt wyjścia i cel

- MBoot wyświetla logo przez warstwę graficzną **GOP** i ustawia wyjście HDMI 1080p. `logofix` zostawia tę warstwę w RGB565 na buforze `0x2100000` ([grafika_gop.md](grafika_gop.md#logo-wiwa-przy-starcie-logofix-)).
- Linux używał `simple-framebuffer` (simpledrm). Ten sterownik pisze w pamięć, którą pokazuje GOP, i nic więcej nie potrafi: nie ma drugiego bufora ani vsync, więc animacje się rwą (tearing).
- Cel: zwykły sterownik fbdev (`/dev/fb0`), który
  - ma 2 bufory (`yres_virtual = 2 × yres`),
  - przełącza bufor na vsync (`FBIOPAN_DISPLAY`),
  - pozwala poczekać na vsync (`FBIO_WAITFORVSYNC`),
  - **nie rusza timingu HDMI** (zostaje taki, jak ustawił MBoot).

Wybrałem fbdev, a nie DRM. fbdev to jeden plik i ok. 300 linii, a programy (fbDOOM, własne narzędzia) już go używają. Przy DRM trzeba by przenieść cały potok z `drivers/gpu/drm/mstar` (ARM, Infinity), czyli dużo więcej pracy.

## 1. Skąd wiadomo, jak działa GOP

1. **Biblioteka z symbolami.** `mstar-mboot-master/MstarCore/bsp/k1/lib/libapiGOP.a` (Kronus) ma nazwy funkcji. Kod z `GOP_GWIN_TriggerRegWriteIn`, `GOP_SetGop23GwinInfo`, `MDrv_GOP_GWIN_SetWinFmt` daje:
   - rejestry GOP to `0x1F00–0x1FFF` w RIU, **bankowane**: rejestr `0x1FFE` wybiera bank (bity `[3:0]`);
   - bit 10 w `0x1FFE` to **trigger**: zapisane rejestry zostaną zatrzaśnięte na najbliższym vsync;
   - bity 12–15 to **ACK** dla GOP0–GOP3: `1` = czeka na vsync, `0` = zastosowane;
   - procedura: zapisz `bank | 0x400`, potem `bank`, i czekaj, aż ACK spadnie.
2. **Przeliczenie adresu.** Rejestr `0x1Fxx` w RIU non-PM ma adres CPU `0xBF200000 + xx·2` (w MBoocie) albo fizyczny `0x1F200000 + …` (w Linuksie przez `devmem`). `0x1F00` → `0x1F203E00`, `0x1FFE` → `0x1F203FFC`. Ogólna reguła jest w [riu.md](riu.md).

## 2. Znalezienie właściwej warstwy (na płycie, `devmem`)

Bufor Linuksa leży pod `0x2100000`. Adres w GOP jest w jednostkach 8 bajtów, więc szukamy wartości `0x2100000 / 8 = 0x420000`, czyli `0x0042` w starszym słowie adresu. Odczyt wszystkich banków (w MBoocie `mw.w bf203ffc 700N; md bf203e00 80` dla N = 0…D) pokazał:
- **bank `0xA`**: `0x1F00` = `0x3F11` (bit 0 = włączone, `[7:4]` = format 1 = RGB565), `0x1F02`/`0x1F04` = `0x0000`/`0x0042`, `0x1F12` = `0x01E0` (pitch 3840/8) → to jest **okno (GWIN) GOP3**;
- **bank `0x9`**: okno rozciągania 1920×1080, skala 1:1 → **sterowanie GOP3**;
- GOP0 (banki 0/1) jest pusty. Wbrew pierwszemu przypuszczeniu (`docs` sprzed testów) Linux nie jest na GOP0.

Rejestr `0x1FFE` w spoczynku ma wartość `0x7000` (bity ACK GOP0–2 ustawione, GOP3 = 0). Górne bity zostawiamy, zmieniamy tylko numer banku i trigger.

## 3. Ręczne przełączenie bufora (`devmem`)

Eksperyment, który pokazał, jak sterownik ma to robić:
```sh
devmem 0x1F203FFC 16 0x700A          # bank A (okno GOP3)
devmem 0x1F203E04 16 0x7700          # młodsze słowo adresu: 0x437700 = 200 linii niżej
devmem 0x1F203E08 16 0x0043          # starsze słowo
devmem 0x1F203FFC 16 0x7409          # bank 9 + trigger
devmem 0x1F203FFC 16 0x7009          # bank 9
devmem 0x1F203FFC 16                 # 0xF009 → po chwili 0x7009: zastosowane na vsync
```
Dwie pułapki, na które trafiłem:
- trigger trzeba ustawić **z bankiem sterującym GOP3 (9)**, a nie z bankiem okna (A). Z bankiem A nic się nie dzieje;
- **nie wolno zmienić banku, zanim bit 15 spadnie do 0**, bo zmiana zostanie anulowana. Za pierwszym razem powrót do starego adresu „nie zadziałał” właśnie dlatego.

Gdy zostawimy adres bufora poza zarezerwowaną pamięcią, dół ekranu pokazuje „szum”, czyli losową pamięć. Drugi bufor musi więc być w zarezerwowanym obszarze.

## 4. Test w przestrzeni użytkownika: `tools/board/gopflip.c`

Zanim powstał sterownik, ten sam mechanizm sprawdziłem w C przez `/dev/mem` (`mmap` strony `0x1F203000` z rejestrami i obu buforów):
```c
static int vsync_latch(void)            /* trigger + czekanie na ACK */
{
	wr(BANKSEL, 0x7000 | 0x400 | 0x9);
	wr(BANKSEL, 0x7000 | 0x9);
	while (rd(BANKSEL) & 0x8000)
		;
	wr(BANKSEL, 0x7000);
}

static void show(int b)                 /* pokaż bufor b od najbliższego vsync */
{
	uint32_t a = buf_phys[b] >> 3;

	wr(BANKSEL, 0x7000 | 0xa);
	wr(GWIN_ADDR_L, a & 0xffff);
	wr(GWIN_ADDR_H, a >> 16);
	vsync_latch();
}
```
Drugi bufor tymczasowo w zarezerwowanym w DTS obszarze `tsukasa@3c00000` (4 MB, mieści jedną klatkę 1080p).

Wyniki:
- `gopflip vsync 120` → **20,000 ms, 50 Hz**. Wyjście to 1080p50, a czekanie na ACK działa jako dokładny vsync.
- `gopflip single`: rysowanie w widocznym buforze, 602 kl./s, pasek się rwie.
- `gopflip double`: rysowanie w ukrytym buforze i przełączanie, dokładnie 50 kl./s, pasek równy (potwierdzone na ekranie).

To dowód, że sprzęt robi to, czego potrzebujemy. Sterownik tylko opakowuje te same zapisy w API fbdev.

## 5. Pamięć na bufory (DTS)

- Klatka: 1920 × 1080 × 2 B = **4 147 200 B** (`0x3F4800`), dwie klatki = 8 294 400 B.
- Stara rezerwacja `fb-memory@2100000` miała 4 MB. Powiększyłem ją do **9 MB** (`0x900000`, do `0x2A00000`; następny zarezerwowany obszar `ve-memory` zaczyna się od `0x3200000`) i dodałem etykietę, żeby sterownik mógł ją wskazać:
  ```dts
  reserved-memory {
  	fb_memory: fb-memory@2100000 {
  		reg = <0x2100000 0x900000>;
  	};
  };
  ```
- Bufor 0 zaczyna się od `0x2100000`, czyli tam, gdzie MBoot rysuje logo. Dzięki temu logo zostaje na ekranie aż do pierwszego rysunku konsoli.
- Koszt: Linux ma 40 MB wolnej pamięci (`free -m`).

## 6. Węzeł urządzenia (DTS)

Rejestry GOP leżą w obszarze RIU, który w DTS jest szyną `riu@1f000000` (`ranges` mapuje adresy dzieci od `0x1F000000`). Stąd `reg = <0x203e00 0x200>`:
```dts
&riu {
	gop3-fb@203e00 {
		compatible = "mstar,msd7816-gop3-fb";
		reg = <0x203e00 0x200>;          /* 0x1F203E00: rejestry 0x1F00–0x1FFF */
		memory-region = <&fb_memory>;
		width = <1920>;
		height = <1080>;
	};
};
```
W `kronus.dtsi` jest już węzeł `gop@203e00`, ale w bloku `#if 0`, więc nie da się na nim oprzeć. Węzeł `framebuffer@2100000` (`simple-framebuffer`) **usunąłem**, żeby simpledrm nie zajmował tej samej pamięci.

## 7. Szkielet sterownika

Zwykły `platform_driver` dopasowywany przez `compatible` (`module_platform_driver`). Stan sterownika w `info->par`:
```c
struct gopfb {
	void __iomem *regs;		/* 0x1F203E00 */
	phys_addr_t base;		/* fizyczny adres bufora 0 */
	spinlock_t lock;		/* wybór banku + sekwencja triggera */
	u32 pseudo_palette[16];		/* kolory konsoli */
};
```
Stałe rejestrów to przesunięcia od `0x1F203E00`: rejestr `0x1Fxx` → `(xx) * 2`, np. `0x1F02` → `0x004`, `0x1FFE` → `0x1FC`.

## 8. `probe`: krok po kroku

1. **Parametry z DTS:** `width`, `height` (`of_property_read_u32`), pitch = `width * 2`, klatka = `pitch * height`.
2. **Pamięć:** `of_parse_phandle(np, "memory-region", 0)` + `of_reserved_mem_lookup()` dają `base` i `size`. Liczba buforów = `min(buffers, size / klatka)`, gdzie `buffers` to parametr modułu (domyślnie 2).
3. **Mapowania:**
   - rejestry przez `devm_platform_ioremap_resource(pdev, 0)`;
   - bufory przez `devm_ioremap(base, klatka * N)`. Na MIPS daje to dostęp nieskeszowany (KSEG1), więc GOP od razu widzi to, co zapisał procesor, bez opróżniania cache.
4. **`fb_info`** (`framebuffer_alloc`):
   - `fix`: `smem_start = base`, `smem_len = klatka * N`, `line_length = pitch`, `visual = FB_VISUAL_TRUECOLOR`, `ypanstep = 1`;
   - `var`: `xres`/`yres` = 1920×1080, **`yres_virtual = 1080 * N`**, 16 bpp, pola RGB565: red `{11, 5}`, green `{5, 6}`, blue `{0, 5}`;
   - `fbops = &gopfb_ops` z `FB_DEFAULT_IOMEM_OPS` (gotowe `read`/`write`/`mmap`/rysowanie dla pamięci „I/O”), `pseudo_palette`.
5. **Przejęcie warstwy:** pod spinlockiem, po odczekaniu na brak zaległego ACK:
   - bank A: format RGB565 w `0x1F00`, adres bufora 0 w `0x1F02`/`0x1F04`, pitch w `0x1F12`;
   - trigger.

   Jeśli MBoot zostawił format 2 (ARGB4444 po `osd_create`), sterownik wypisuje ostrzeżenie.
6. `register_framebuffer(info)` i komunikat `fb0: 1920x1080 RGB565, 2 buffers at 0x02100000`.

## 9. `fb_pan_display`: przełączanie bufora

```c
static int gopfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct gopfb *g = info->par;
	u32 addr = (g->base + var->yoffset * info->fix.line_length) >> 3;
	...
	spin_lock_irqsave(&g->lock, flags);
	ret = gopfb_wait_idle_atomic(g);          /* poprzedni trigger musi się wykonać */
	if (!ret) {
		writew(BANK_IDLE | BANK_GOP3_GWIN, g->regs + GOP_BANKSEL);
		writew(addr & 0xffff, g->regs + GOP_GWIN_ADDR_L);
		writew(addr >> 16, g->regs + GOP_GWIN_ADDR_H);
		gopfb_trigger(g);                 /* bank 9 | 0x400, potem bank 9 */
	}
	spin_unlock_irqrestore(&g->lock, flags);
	return ret;
}
```
Decyzje:
- `yoffset × 3840` jest zawsze wielokrotnością 8, więc każdy `yoffset` daje poprawny adres (`ypanstep = 1`);
- funkcja **nie czeka na vsync**. Fbdev zakłada, że panning działa „od następnej klatki”, a na przełączenie czeka się osobno przez `FBIO_WAITFORVSYNC`. Czeka tylko na poprzedni trigger (najwyżej 20 ms), bo zmiana banku przed ACK anuluje zapis (krok 3);
- po triggerze **bank zostaje na 9**, dopóki nie przyjdzie ACK. Kolejne wywołanie i tak najpierw na to czeka;
- `pan_display` może być wołany z kontekstu atomowego (konsola), stąd spinlock i `readw_poll_timeout_atomic()` (co 10 µs, limit 100 ms).

## 10. `FBIO_WAITFORVSYNC`

```c
static int gopfb_wait_for_vsync(struct gopfb *g)
{
	spin_lock_irqsave(&g->lock, flags);
	if (!gopfb_pending(g))
		gopfb_trigger(g);	/* nic nie czeka: pusty trigger = czekanie na vsync */
	spin_unlock_irqrestore(&g->lock, flags);

	return readw_poll_timeout(g->regs + GOP_BANKSEL, v, !(v & BANK_ACK_GOP3),
				  500, VSYNC_TIMEOUT_US);
}
```
- Gdy czeka przełączenie z `pan_display`, czekamy na **nie**. Po powrocie stary bufor na pewno nie jest już wyświetlany i można w nim rysować.
- Gdy nic nie czeka, trigger bez zmian w rejestrach to po prostu czekanie na najbliższy vsync.
- Tu jesteśmy w kontekście procesu (ioctl), więc odpytujemy z usypianiem (`readw_poll_timeout`, co 0,5 ms) i bez trzymania blokady.
- Obsługa w `fb_ioctl`: `case FBIO_WAITFORVSYNC`, a inne polecenia dostają `-ENOTTY`.

## 11. Pozostałe `fb_ops`

- `fb_setcolreg`: wypełnia 16 kolorów `pseudo_palette` w formacie RGB565 (potrzebne konsoli, tak jak w `simplefb.c`).
- `fb_check_var`: tryb jest stały, więc odrzucamy zmianę rozdzielczości, bpp i rozmiaru wirtualnego. Zmieniać się może tylko `yoffset`.
- `info->flags = 0`, bez `FBINFO_HWACCEL_YPAN`. Konsola (fbcon) przewija wtedy przez przerysowanie, a nie przez panning, więc nie miesza w buforach aplikacji.

## 12. Kconfig, Makefile, konfiguracja

`drivers/video/fbdev/Kconfig`:
```kconfig
config FB_MSTAR_GOP
	tristate "MStar Kronus GOP3 framebuffer (double buffered)"
	depends on FB && OF
	depends on MSTAR_MIPS || COMPILE_TEST
	select FB_IOMEM_HELPERS
```
`drivers/video/fbdev/Makefile`: `obj-$(CONFIG_FB_MSTAR_GOP) += mstar-gopfb.o`.

`.config`: `CONFIG_FB_MSTAR_GOP=y` (wkompilowany, bo konsola musi działać od startu) i `# CONFIG_DRM_SIMPLEDRM is not set`. Potem `make olddefconfig`. Konfiguracja jest w `arch/mips/configs/wiwa_hd95_defconfig` (`make savedefconfig`).

Obawa: wbudowany sterownik I2C powodował drganie obrazu HDMI ([i2c.md](i2c.md)). Ten sterownik wbudowany **nie powoduje drgania** (sprawdzone na ekranie).

## 13. Budowanie i uruchomienie

```sh
cd linux_msd781x
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- -j6
make ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- dtbs
cp vmlinux vmlinux_dtb
mipsel-linux-gnu-objcopy --update-section .appended_dtb=arch/mips/boot/dts/mstar/msd7816-test.dtb vmlinux_dtb
mipsel-linux-gnu-objcopy -O binary vmlinux_dtb vmlinux_gop.bin
readelf -h vmlinux | grep Entry          # 0x805e87e4
```
Wgranie ([przesylanie_uart.md](przesylanie_uart.md)), a potem w MBoocie najpierw ręcznie, bez zmiany autostartu:
```
usb start
fatload usb 0 0x80F00000 logofix.bin; go 0x80F00000
fatload usb 0 0x80000000 vmlinux_gop.bin; go 0x805e87e4
```

## 14. Sprawdzenie

```sh
dmesg | grep gopfb          # mstar-gopfb 1f203e00.gop3-fb: fb0: 1920x1080 RGB565, 2 buffers at 0x02100000
cat /proc/fb                # 0 mstar-gop3
gcc -O2 -o fbflip fbflip.c  # tools/board/fbflip.c
./fbflip vsync 100          # 1920x1080 virtual 1920x2160 ... period 19.952 ms, 50.12 Hz
echo 0 > /sys/class/vtconsole/vtcon1/bind
./fbflip single 1500        # 403 kl./s, pasek się rwie
./fbflip 500                # podwójne buforowanie, pasek równy
echo 1 > /sys/class/vtconsole/vtcon1/bind
```
Wszystko potwierdzone na płycie 2026-10-14. Moduły I2C i panelu ładują się dalej, a tuner odpowiada `0x14`.

## 15. Użycie w programie

```c
int fd = open("/dev/fb0", O_RDWR);
struct fb_var_screeninfo var;  struct fb_fix_screeninfo fix;  uint32_t zero = 0;
ioctl(fd, FBIOGET_VSCREENINFO, &var);                  /* yres_virtual = 2 * yres */
ioctl(fd, FBIOGET_FSCREENINFO, &fix);
uint16_t *mem = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
int back = 1;
for (;;) {
	uint16_t *buf = mem + back * var.xres * var.yres;
	/* ... narysuj klatkę w buf ... */
	var.yoffset = back * var.yres;
	ioctl(fd, FBIOPAN_DISPLAY, &var);              /* pokaż ją od najbliższego vsync */
	ioctl(fd, FBIO_WAITFORVSYNC, &zero);           /* stary bufor jest już wolny */
	back ^= 1;
}
```

## 16. Co dalej

- **Przerwanie vsync zamiast odpytywania.** W wyłączonym węźle `gop@203e00` w `kronus.dtsi` jest `interrupts-extended = <&intc_irq 20 …>` ❓. Wtedy `FBIO_WAITFORVSYNC` mogłoby spać na `wait_event` zamiast odpytywać co 0,5 ms.
- **Mniejsza rozdzielczość ze skalowaniem GOP** (np. 960×540 rozciągane do 1080p, rejestry w banku 9: okno rozciągania i skala), co da szybszego Dooma i 3 bufory w tej samej pamięci.
- **fbDOOM / chocolate-doom:** rysowanie w ukrytym buforze i `FBIOPAN_DISPLAY`.
