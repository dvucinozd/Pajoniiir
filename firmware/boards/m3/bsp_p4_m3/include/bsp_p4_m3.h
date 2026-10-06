#pragma once

// Board support for JC-ESP32P4-M3-DEV (ESP32-P4 + 5.0" MIPI-DSI 800x480 + FT5426 + PCM5102A).
//
// Pin reference for JC-ESP32P4-M3-DEV:
//   MIPI-DSI (J2 15-pin FPC):
//     DSI Lane 1: DSI_A_DATA1_N / DSI_A_DATA1_P (Pins 2, 3; unused by accepted profile)
//     DSI Clock:  DSI_A_CLK_N   / DSI_A_CLK_P   (Pins 5, 6)
//     DSI Lane 0: DSI_A_DATA0_N / DSI_A_DATA0_P (Pins 8, 9; active)
//     Shared I2C: ES_I2C_SCL (Pin 11), ES_I2C_SDA (Pin 12)
//                 FT5426 touch addr 0x38, panel power/backlight addr 0x45
//     Power:      +3.3V (Pins 14, 15), GND (Pins 1, 4, 7, 10, 13)
//     One lane is hardware-accepted for this DSI506/DYL0023 example; bridge
//     identity and a vendor-level electrical specification remain unknown.
//
//   SDMMC (MicroSD slot 0):
//     D0..D3:     GPIO39..42, CMD: GPIO44, CLK: GPIO43 (LDO channel 4)
//
//   PCM5102A Master Audio DAC (I2S Unit 1):
//     BCLK: GPIO1, WS/LRCK: GPIO2, DOUT: GPIO3
//
//   ESP32-C6 Coprocessor (SDIO slot 1):
//     D0..D3: GPIO14..17, CLK: GPIO18, CMD: GPIO19, RST: GPIO54

#include "board_adapter.h"
#include "bsp_scanout.h"
