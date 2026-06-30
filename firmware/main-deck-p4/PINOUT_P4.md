# ESP32-P4 Pinout Inventory for JC4880P443C_I_W

This file is the source of truth for P4-side peripheral wiring.
Do not use `firmware/control-board-s3/PINOUT.md` for P4 peripherals.

## Occupied P4 pins in current firmware

| GPIO | Owner | Evidence | PCM5102A use |
| --- | --- | --- | --- |
| GPIO5 | LCD reset | `BSP_LCD_RST_GPIO` | Forbidden |
| GPIO23 | LCD backlight PWM | `BSP_LCD_BL_GPIO` | Forbidden |
| GPIO7 | Shared I2C SDA | `BSP_I2C_SDA_GPIO` | Forbidden |
| GPIO8 | Shared I2C SCL | `BSP_I2C_SCL_GPIO` | Forbidden |
| GPIO13 | ES8311 I2S MCLK | `BSP_I2S_MCLK_GPIO` | Forbidden |
| GPIO12 | ES8311 I2S BCLK | `BSP_I2S_BCLK_GPIO` | Forbidden |
| GPIO10 | ES8311 I2S WS/LRCK | `BSP_I2S_WS_GPIO` | Forbidden |
| GPIO9 | ES8311 I2S DOUT | `BSP_I2S_DOUT_GPIO` | Forbidden |
| GPIO48 | ES8311 I2S DIN | `BSP_I2S_DIN_GPIO` | Forbidden |
| GPIO11 | Speaker PA enable | `BSP_AUDIO_PA_GPIO` | Forbidden |
| GPIO39 | SDMMC D0 | `slot_config.d0` | Forbidden |
| GPIO40 | SDMMC D1 | `slot_config.d1` | Forbidden |
| GPIO41 | SDMMC D2 | `slot_config.d2` | Forbidden |
| GPIO42 | SDMMC D3 | `slot_config.d3` | Forbidden |
| GPIO43 | SDMMC CLK | `slot_config.clk` | Forbidden |
| GPIO44 | SDMMC CMD | `slot_config.cmd` | Forbidden |
| GPIO28 | Control link UART RX | `PIN_UART_RX` | Forbidden |
| GPIO29 | Control link UART TX | `PIN_UART_TX` | Forbidden |

## JP1 candidate pins from board analysis

| JP1 pin | GPIO | Candidate use | Status |
| --- | --- | --- | --- |
| 5 | GPIO52 | PCM5102A WS/LRCK | Candidate, requires bench verification |
| 7 | GPIO51 | PCM5102A DIN from P4 DOUT | Candidate, requires bench verification |
| 9 | GPIO50 | PCM5102A BCLK | Candidate, requires bench verification |
| 11 | GPIO49 | Optional PCM5102A SCK/MCLK | Not used in first bring-up |

## Rejected DAC pin proposals

| GPIO | Reason |
| --- | --- |
| GPIO22 | Not confirmed on the JC4880 expansion header in current repo docs |
| GPIO23 | Already used by LCD backlight PWM |
| GPIO24 | Not confirmed on the JC4880 expansion header in current repo docs |
| GPIO25 | Not confirmed on the JC4880 expansion header in current repo docs |

## PCM5102A module header

The photographed PCM5102MK/PCM5102A RCA breakout board exposes this six-pin
header:

```text
VCC
GND
GND
LRCK
DATA
BCK
```

`DATA` is the DAC input and connects to the P4 I2S DOUT signal. `LRCK` is I2S
WS. The module does not expose MCLK/SCK, so first bring-up uses
`I2S_GPIO_UNUSED`.

## PCM5102A wiring target after bench verification

| PCM board header | Signal meaning | P4 GPIO | JP1 pin | Firmware define |
| --- | --- | --- | --- | --- |
| BCK | I2S bit clock | GPIO50 | 9 | `BSP_PCM5102_BCLK_GPIO` |
| LRCK | I2S WS / left-right clock | GPIO52 | 5 | `BSP_PCM5102_WS_GPIO` |
| DATA | I2S serial data input, driven by P4 DOUT | GPIO51 | 7 | `BSP_PCM5102_DOUT_GPIO` |
| VCC | DAC board power | 3.3 V first; 5 V only if module requires it | 1 or 16 | board power |
| GND | board ground | GND | 3, 4, or 14 | board ground |
| GND | optional second ground return | GND | 3, 4, or 14 | board ground |
| MCLK/SCK | not exposed on this module | not connected | not connected | `I2S_GPIO_UNUSED` |

## Bench verification record

Before enabling PCM5102A firmware, verify continuity from JP1 to the DAC module
wiring and verify that LCD backlight, touch, SD, USB media, and ES8311 playback
still work with the DAC connected but idle.

| Date | Check | Result |
| --- | --- | --- |
| 2026-06-26 | GPIO50/GPIO52/GPIO51 assigned as PCM5102A candidate set | Not bench-verified |
| 2026-06-30 | PCM5102A board wired on GPIO50/GPIO52/GPIO51; RCA and onboard 3.5 mm outputs tested during playback | Pass |
