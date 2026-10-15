# Funkcje BSP w zrzucie U-Boota

## Skąd te adresy

`mstar-mboot-master/MstarCore/bsp/k1/lib/*.a` to skompilowane biblioteki MStara dla Kronusa (K1) **z symbolami**. MBoot na płycie linkuje te same albo bardzo podobne wersje, więc kod funkcji w zrzucie jest identyczny bajt w bajt, poza adresami, które wstawia linker.

`tools/match_libs.py` bierze każdą funkcję z `.o`, maskuje bajty zmieniane przez relokacje (`R_MIPS_26`, `HI16`/`LO16`, `GOT16`, `CALL16`…) i szuka wzorca w zrzucie. Pomija funkcje krótsze niż 16 bajtów.

```sh
mkdir objs && cd objs
for l in libdrvHWI2C libapiSWI2C libdrvGPIO libdrvSAR libdrvIR libdrvMMIO; do
  mkdir $l; (cd $l && ar x ../../mstar-mboot-master/MstarCore/bsp/k1/lib/$l.a)
done
cd ..
python3 tools/match_libs.py binaries/RAM87_DU.RAM 87000000 objs/*/*.o
```

Wymaga `pyelftools`. Zrzut `RAM87_DU.RAM` zaczyna się od `0x87000000` (32 MB, `fatwrite usb 0 87000000 RAM87_DUMP 2000000`).

Wynik: 103 funkcje z ok. 200 znalezione. Poniżej 64 **jednoznaczne** (jedno trafienie, unikalny adres).

## Import do Ghidry

1. Załaduj `RAM87_DU.RAM` jako Raw Binary, język `MIPS:LE:32:default`, base `0x87000000`.
2. Script Manager → uruchom `tools/ghidra_mstar_labels.py`. Skrypt tworzy funkcje i nadaje im nazwy.

Skrypt pomija `HAL_HWI2C_Read4Byte`/`Write4Byte` (opis niżej).

## Jednoznaczne dopasowania

| Adres | Funkcja | Obiekt |
|---|---|---|
| `0x87617C34` | `MDrv_MMIO_Init` | drvMMIO.o |
| `0x87617CD4` | `MDrv_MMIO_GetBASE` | drvMMIO.o |
| `0x87617FD4` | `HAL_MMIO_GetBase` | halMMIO.o |
| `0x87618074` | `HAL_MMIO_GetIPBase` | halMMIO.o |
| `0x87619B8C` | `HAL_HWI2C_Read4Byte` | halHWI2C.o |
| `0x8761A078` | `mdrv_gpio_set_high` | drvGPIO.o |
| `0x8761A0A8` | `mdrv_gpio_set_low` | drvGPIO.o |
| `0x8761A0D8` | `mdrv_gpio_get_level` | drvGPIO.o |
| `0x8761A108` | `MDrv_GPIO_SetIOMapBase` | drvGPIO.o |
| `0x8761A1E4` | `HAL_Gpio_Set_High` | halGPIO.o |
| `0x8761A298` | `HAL_Gpio_Set_Low` | halGPIO.o |
| `0x8761A350` | `HAL_Gpio_Set_Input` | halGPIO.o |
| `0x8761A3C0` | `HAL_Gpio_Get_InOut` | halGPIO.o |
| `0x8761A42C` | `HAL_Gpio_Get_Level` | halGPIO.o |
| `0x8764BF68` | `_SAR_MailBoxCallbackHandler` | drvSAR.o |
| `0x8764BFFC` | `MDrv_SAR_Adc_Config` | drvSAR.o |
| `0x8764C0B4` | `MDrv_SAR_Kpd_Init` | drvSAR.o |
| `0x8764C3F8` | `_SAR_MailBoxHandler` | drvSAR.o |
| `0x8764C5D4` | `MDrv_SAR_Enable` | drvSAR.o |
| `0x8764C758` | `MDrv_SAR_Config` | drvSAR.o |
| `0x8764C8DC` | `MDrv_SAR_Init` | drvSAR.o |
| `0x8764CA90` | `HAL_SAR_WriteByteMask` | halSAR.o |
| `0x8764CB08` | `HAL_SAR_CfgChannelBound` | halSAR.o |
| `0x8764CCD4` | `HAL_SAR_GetChannelADC` | halSAR.o |
| `0x8764CD8C` | `HAL_SAR_CfgSingleChannel` | halSAR.o |
| `0x8764CDB4` | `HAL_SAR_CfgTriggerMode` | halSAR.o |
| `0x8764CDE0` | `HAL_SAR_CfgSingleChannelEn` | halSAR.o |
| `0x8764CE0C` | `HAL_SAR_CfgDigitOperMode` | halSAR.o |
| `0x8764CE38` | `HAL_SAR_CfgDigitPowerdown` | halSAR.o |
| `0x8764CE64` | `HAL_SAR_CfgStart` | halSAR.o |
| `0x8764CE90` | `HAL_SAR_CfgAtopPowerdown` | halSAR.o |
| `0x8764CEB8` | `HAL_SAR_CfgAtopFreeRun` | halSAR.o |
| `0x8764CEE4` | `HAL_SAR_CfgSelection` | halSAR.o |
| `0x8764CF10` | `HAL_SAR_CfgHighChannel` | halSAR.o |
| `0x8764CF3C` | `HAL_SAR_CfgClockSamplePeriod` | halSAR.o |
| `0x8764CF64` | `HAL_SAR_CfgAanlogInputSelect` | halSAR.o |
| `0x8764D018` | `HAL_SAR_CfgOutputEnable` | halSAR.o |
| `0x8764D0D0` | `HAL_SAR_CfgIntMask` | halSAR.o |
| `0x8764D0F8` | `HAL_SAR_CfgIntClear` | halSAR.o |
| `0x8764D938` | `_MDrv_HWI2C_SetClk` | drvHWI2C.o |
| `0x8764D9E8` | `MDrv_HWI2C_SetClk` | drvHWI2C.o |
| `0x8764DA0C` | `_MDrv_HWI2C_SelectPort` | drvHWI2C.o |
| `0x8764E060` | `HAL_HWI2C_WriteRegBit` | halHWI2C.o |
| `0x8764E1AC` | `HAL_HWI2C_Init_Chip` | halHWI2C.o |
| `0x8764E208` | `HAL_HWI2C_SetPortRegOffset` | halHWI2C.o |
| `0x8764E24C` | `HAL_HWI2C_GetPortIdxByPort` | halHWI2C.o |
| `0x8764E290` | `HAL_HWI2C_SelectPort` | halHWI2C.o |
| `0x8764E3A0` | `HAL_HWI2C_SetClk` | halHWI2C.o |
| `0x8764E444` | `HAL_HWI2C_Stop` | halHWI2C.o |
| `0x8764E488` | `HAL_HWI2C_NoAck` | halHWI2C.o |
| `0x8764E4CC` | `HAL_HWI2C_Is_INT` | halHWI2C.o |
| `0x8764E50C` | `HAL_HWI2C_WaitINT` | halHWI2C.o |
| `0x8764E634` | `HAL_HWI2C_Start` | halHWI2C.o |
| `0x8764E68C` | `HAL_HWI2C_Clear_INT` | halHWI2C.o |
| `0x8764E6D0` | `HAL_HWI2C_SendData` | halHWI2C.o |
| `0x8764E734` | `HAL_HWI2C_Reset` | halHWI2C.o |
| `0x8764E754` | `HAL_HWI2C_Master_Enable` | halHWI2C.o |
| `0x8764E7D0` | `HAL_HWI2C_Send_Byte` | halHWI2C.o |
| `0x8764E80C` | `HAL_HWI2C_Recv_Byte` | halHWI2C.o |
| `0x87687480` | `HAL_HWI2C_Write4Byte` | halHWI2C.o |
| `0x876882CC` | `_IR_MailBoxCallbackHandler` | drvIR.o |
| `0x8768839C` | `_IR_MailBoxHandler` | drvIR.o |
| `0x87688580` | `MDrv_IR_Config` | drvIR.o |
| `0x87688718` | `MDrv_IR_Init` | drvIR.o |

## Niejednoznaczne i podejrzane

- `HAL_HWI2C_Read4Byte` → `0x87619B8C` i `HAL_HWI2C_Write4Byte` → `0x87687480` leżą daleko od reszty HWI2C (`0x8764D…`–`0x8764E…`). To prawdopodobnie identyczne funkcje pomocnicze z innego modułu (każdy HAL MStara ma własne kopie `ReadByte`/`WriteByte`). Nazwy są poprawne co do działania, ale nie co do modułu.
- `MDrv_HWI2C_ReadBytes` / `MDrv_HWI2C_WriteBytes` → `0x8764D4B8` i `0x8764D834`. Obie mają identyczny kod (różnią się tylko wywoływaną funkcją), więc trzeba je rozróżnić w Ghidrze po tym, co wołają.
- `MDrv_HWI2C_SelectPort` i `SelectPort1` mają ten sam kod → `0x8764DB40`, więc w binarce jest tylko jedna z nich.
- `*_GetLibVer` ze wszystkich bibliotek → `0x876598A4`: identyczny kod, nieistotne.
- Krótkie funkcje (`*_SetDbgLevel`, `*_SetIOMapBase`, `HAL_HWI2C_ReadByte` itp.) mają po kilka trafień. Pełna lista jest w wyniku skryptu.

## Czego tu nie ma (zostaje dla Ghidry)

- Sterownik tunera **MxL5007T** i `Tuner_I2C_Write` (string pod `0x87695B40`). To kod spoza bibliotek K1.
- Komenda `tuner_init` (`cmd_tuner.c`, stringi około `0x8768DE60`).
- `MDrv_IIC_InitPort0/1` (`drvIIC.c`, stringi około `0x87691ED0`). Źródło jest w `MstarCore/src/drivers/iic/drvIIC.c`, a wywołuje `MDrv_HWI2C_Init`.
- Biblioteki K1 nieprzeszukane jeszcze tym skryptem (np. `libdrvDMD_INTERN_DVBT`, demodulator).
