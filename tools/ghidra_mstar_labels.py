# Ghidra script: nadaje etykiety funkcjom MStar BSP (K1) znalezionym w RAM87_DU.RAM.
# Wymaga programu załadowanego jako Raw Binary, MIPS:LE:32, base 0x87000000.
# Lista wygenerowana przez tools/match_libs.py (tylko jednoznaczne trafienia).
# @category MStar
from ghidra.program.model.symbol import SourceType

LABELS = [
    (0x87617C34, "MDrv_MMIO_Init"),
    (0x87617CD4, "MDrv_MMIO_GetBASE"),
    (0x87617FD4, "HAL_MMIO_GetBase"),
    (0x87618074, "HAL_MMIO_GetIPBase"),
    (0x8761A078, "mdrv_gpio_set_high"),
    (0x8761A0A8, "mdrv_gpio_set_low"),
    (0x8761A0D8, "mdrv_gpio_get_level"),
    (0x8761A108, "MDrv_GPIO_SetIOMapBase"),
    (0x8761A1E4, "HAL_Gpio_Set_High"),
    (0x8761A298, "HAL_Gpio_Set_Low"),
    (0x8761A350, "HAL_Gpio_Set_Input"),
    (0x8761A3C0, "HAL_Gpio_Get_InOut"),
    (0x8761A42C, "HAL_Gpio_Get_Level"),
    (0x8764BF68, "_SAR_MailBoxCallbackHandler"),
    (0x8764BFFC, "MDrv_SAR_Adc_Config"),
    (0x8764C0B4, "MDrv_SAR_Kpd_Init"),
    (0x8764C3F8, "_SAR_MailBoxHandler"),
    (0x8764C5D4, "MDrv_SAR_Enable"),
    (0x8764C758, "MDrv_SAR_Config"),
    (0x8764C8DC, "MDrv_SAR_Init"),
    (0x8764CA90, "HAL_SAR_WriteByteMask"),
    (0x8764CB08, "HAL_SAR_CfgChannelBound"),
    (0x8764CCD4, "HAL_SAR_GetChannelADC"),
    (0x8764CD8C, "HAL_SAR_CfgSingleChannel"),
    (0x8764CDB4, "HAL_SAR_CfgTriggerMode"),
    (0x8764CDE0, "HAL_SAR_CfgSingleChannelEn"),
    (0x8764CE0C, "HAL_SAR_CfgDigitOperMode"),
    (0x8764CE38, "HAL_SAR_CfgDigitPowerdown"),
    (0x8764CE64, "HAL_SAR_CfgStart"),
    (0x8764CE90, "HAL_SAR_CfgAtopPowerdown"),
    (0x8764CEB8, "HAL_SAR_CfgAtopFreeRun"),
    (0x8764CEE4, "HAL_SAR_CfgSelection"),
    (0x8764CF10, "HAL_SAR_CfgHighChannel"),
    (0x8764CF3C, "HAL_SAR_CfgClockSamplePeriod"),
    (0x8764CF64, "HAL_SAR_CfgAanlogInputSelect"),
    (0x8764D018, "HAL_SAR_CfgOutputEnable"),
    (0x8764D0D0, "HAL_SAR_CfgIntMask"),
    (0x8764D0F8, "HAL_SAR_CfgIntClear"),
    (0x8764D938, "_MDrv_HWI2C_SetClk"),
    (0x8764D9E8, "MDrv_HWI2C_SetClk"),
    (0x8764DA0C, "_MDrv_HWI2C_SelectPort"),
    (0x8764E060, "HAL_HWI2C_WriteRegBit"),
    (0x8764E1AC, "HAL_HWI2C_Init_Chip"),
    (0x8764E208, "HAL_HWI2C_SetPortRegOffset"),
    (0x8764E24C, "HAL_HWI2C_GetPortIdxByPort"),
    (0x8764E290, "HAL_HWI2C_SelectPort"),
    (0x8764E3A0, "HAL_HWI2C_SetClk"),
    (0x8764E444, "HAL_HWI2C_Stop"),
    (0x8764E488, "HAL_HWI2C_NoAck"),
    (0x8764E4CC, "HAL_HWI2C_Is_INT"),
    (0x8764E50C, "HAL_HWI2C_WaitINT"),
    (0x8764E634, "HAL_HWI2C_Start"),
    (0x8764E68C, "HAL_HWI2C_Clear_INT"),
    (0x8764E6D0, "HAL_HWI2C_SendData"),
    (0x8764E734, "HAL_HWI2C_Reset"),
    (0x8764E754, "HAL_HWI2C_Master_Enable"),
    (0x8764E7D0, "HAL_HWI2C_Send_Byte"),
    (0x8764E80C, "HAL_HWI2C_Recv_Byte"),
    (0x876882CC, "_IR_MailBoxCallbackHandler"),
    (0x8768839C, "_IR_MailBoxHandler"),
    (0x87688580, "MDrv_IR_Config"),
    (0x87688718, "MDrv_IR_Init"),
]

for addr, name in LABELS:
    a = toAddr(addr)
    disassemble(a)
    f = getFunctionAt(a)
    if f is None:
        f = createFunction(a, name)
    if f is not None:
        f.setName(name, SourceType.USER_DEFINED)
    else:
        createLabel(a, name, True)
    print("%08X %s" % (addr, name))
