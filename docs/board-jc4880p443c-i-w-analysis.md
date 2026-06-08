# JC4880P443C_I_W Board Analysis

Local board docs reviewed from `../JC4880P443C_I_W`.

## Board Summary

The JC4880P443C_I_W is a 4.3 inch ESP32-P4 display module with an ESP32-C6 wireless coprocessor. It is designed for HMI/multimedia work, not as a Linux SBC.

Confirmed from local specification and examples:

| Area | Detail |
| --- | --- |
| Main controller | ESP32-P4 module, GUITION JC-ESP32P4-M3 |
| Wireless | ESP32-C6 coprocessor, ESP-Hosted style integration needed |
| Display | 4.3 inch IPS TFT, ST7701S, `480x800` |
| Touch | GT911 capacitive touch over I2C |
| Memory | 32 MB PSRAM, 16 MB flash |
| Storage | TF/microSD slot |
| Audio | ES8311 codec plus speaker amplifier circuit |
| Camera | MIPI CSI connector, OV02C10 datasheet included |
| USB | Full-speed Type-C and high-speed Type-C circuits present |
| Expansion | 2x13 2.54 mm header, UART, I2C, GPIO, C6 lines |
| Power | 5 V input, lithium battery circuit, 3.3 V rails |

Additional BSP reference: the external repo `../upstream/esp32_p4_jc4880p433c_bsp` contains a close ESP-IDF BSP candidate. It should be treated as a source to adapt and verify, not as automatically proven for our exact `JC4880P443C_I_W` board variant.

Important startup note from the user manual: after upload, the device runs the program; to upload another program reliably, reset power so it enters flashing mode again.

## Display And Touch

From Arduino LVGL examples:

| Function | Pin / setting |
| --- | --- |
| LCD resolution | `LCD_H_RES 480`, `LCD_V_RES 800` |
| LCD driver | ST7701S over MIPI DSI |
| MIPI DSI PHY LDO | channel 3, 2500 mV |
| Backlight | GPIO 23 in `st7701_lcd.cpp` |
| LCD reset | GPIO 5 in `st7701_lcd.cpp`; `pins_config.h` leaves reset as `-1`, so verify this in practice |
| Touch controller | GT911 |
| Touch I2C SDA/SCL | GPIO 7 / GPIO 8 |
| Touch I2C address | `0x5D` in GT911 driver |
| Touch reset/int | Not connected in examples (`-1`) |
| Rotation support | LVGL v9 example enables 90 degree rotation with PPA |

Porting implication: the DJ UI should be rendered natively in LVGL, likely landscape `800x480` after rotation, because the CDJ-style layout is wider than tall.

The external BSP strengthens the ESP-IDF path for display/touch:

- It has ST7701S MIPI-DSI init commands and explicit `480x800` timing.
- It defaults to 2 DSI lanes, 500 Mbps lane bitrate, 34 MHz DPI clock, RGB565, and two frame buffers.
- It uses GPIO 23 for PWM backlight and GPIO 5 for LCD reset.
- It uses the same shared I2C bus on GPIO 7/8 for GT911.

## Audio And Storage

From `arduino_examples/mp3_player/mp3_player.ino`:

| Function | Pin / setting |
| --- | --- |
| SD D0 | GPIO 39 |
| SD D1 | GPIO 40 |
| SD D2 | GPIO 41 |
| SD D3 | GPIO 42 |
| SD CMD | GPIO 44 |
| SD CLK | GPIO 43 |
| Codec | ES8311 |
| Codec I2C address | `0x18` |
| Codec I2C SDA/SCL | GPIO 7 / GPIO 8 |
| I2S MCLK | GPIO 13 |
| I2S BCLK | GPIO 12 |
| I2S DIN | GPIO 48 |
| I2S LRCK/WS | GPIO 10 |
| I2S DOUT | GPIO 9 |
| Speaker amp / PA | GPIO 11 |

From `Instructions.txt`: sample video playback uses a TF card with the MJPEG folder copied to the card, and the speaker must be externally connected. It says the TF card should be under 32 GB and FAT32 for the supplied demo.

Status (verified on hardware, 2026-05):

- **USB mass storage is the media path and works** — the Rekordbox drive mounts at `/usb`
  (USB Host MSC → FATFS) and the library loads. microSD (GPIO39–44) is reserved for
  config/cache; `bsp_sd_init()` mounts FATFS at `/sd` on P4 SDMMC slot 0.
- **ES8311 + I2S playback works** via `esp_codec_dev` (minimp3 decode → ES8311). The audio
  pins in the table above are confirmed. Note: MP3s are preloaded into PSRAM and decoded with
  `fmemopen` — streaming from USB during playback tripped a USB-DWC channel assert.
- For DJ line output, still verify whether the ES8311 DAC can be routed safely to RCA line
  level, or add a small external I2S DAC/line driver. The board speaker amp is not a finished
  RCA output.

## Expansion Header

From schematic `5-Schematic/4_USB&IO.png`, `JP1` exposes:

| JP1 pin | Signal |
| --- | --- |
| 1 | VCC3V3 |
| 2 | VCC5V |
| 3 | GND |
| 4 | GND |
| 5 | GPIO52 |
| 6 | GPIO33 |
| 7 | GPIO51 |
| 8 | GPIO31 |
| 9 | GPIO50 |
| 10 | GPIO30 |
| 11 | GPIO49 |
| 12 | GPIO29 |
| 13 | GPIO35 |
| 14 | GND |
| 15 | GPIO34 |
| 16 | VCC3V3 |
| 17 | GPIO32 |
| 18 | C6_U0RXD |
| 19 | GPIO28 |
| 20 | C6_U0TXD |
| 21 | FS_I2C_SDA |
| 22 | C6_IO9 |
| 23 | FS_I2C_SCL |
| 24 | C6_CHIP_PU |
| 25 | not clearly assigned in schematic image |
| 26 | not clearly assigned in schematic image |

Available direct ESP32-P4 GPIOs on the header appear to be:

`GPIO28`, `GPIO29`, `GPIO30`, `GPIO31`, `GPIO32`, `GPIO33`, `GPIO34`, `GPIO35`, `GPIO49`, `GPIO50`, `GPIO51`, `GPIO52`.

The external BSP WiFi notes state that `GPIO28`, `GPIO29`, `GPIO30`, `GPIO31`, `GPIO34`, `GPIO35`, `GPIO49`, `GPIO50`, `GPIO51`, and `GPIO52` are RMII/Ethernet-related pins and are not used for ESP32-C6 WiFi. They may therefore be usable for CDJ controls if Ethernet is out of scope, but each pin still needs bench verification before assignment.

This is not enough for the original XDJ100SX 23-signal contract if wired one-to-one.

Earlier single-board fallback I/O strategy:

- Use direct P4 GPIOs for timing-sensitive signals:
  - jog A/B,
  - browse A/B,
  - pitch ADC if an exposed pin supports ADC, otherwise use external ADC,
  - any interrupt lines from expanders.
- Use I2C GPIO expanders for low-rate buttons and LEDs:
  - `MCP23017`, `TCA9555`, or similar.
  - Keep LEDs on expander outputs through suitable resistors/transistors.
- If pitch cannot use an internal ADC pin on exposed JP1 GPIOs, use `ADS1115` or another external ADC over I2C.

Current preferred strategy: use a separate ESP32-S3 control/MIDI board for the CDJ panel instead of expanding the ESP32-P4. The P4 JP1 header should mainly carry the inter-board UART link, power/ground, and optional debug/reset signals.

## Existing Board Interfaces We Should Not Accidentally Consume

| Resource | Already used by board |
| --- | --- |
| GPIO 7/8 | Shared I2C for touch, codec, camera/control paths |
| GPIO 9/10/12/13/48/11 | Audio codec / I2S / PA |
| GPIO 23 | LCD backlight |
| GPIO 39-44 | SDMMC / TF card |
| GPIO 5 | LCD reset in example code |
| MIPI DSI lanes | Display |
| MIPI CSI lanes | Camera connector |
| USB signals | Upload, debug, host/device experiments |

Avoid assigning CDJ buttons/LEDs to these unless the schematic and examples prove the function is unused.

## Feasibility Assessment

What the board is good at:

- Native graphical UI with LVGL on a sharp `480x800` panel.
- Touch controls.
- SD-card media browsing.
- I2S audio decode/output experiments.
- USB host/device experiments with ESP-IDF when needed.
- A compact all-in-one control surface.

What the board is not:

- It is not a Raspberry Pi replacement for running Linux + Mixxx.
- It will not run the upstream Mixxx skin or JS mapping directly.
- It probably cannot support all CDJ front-panel signals without I/O expansion.

Resolved on hardware (2026-05):

- ✅ UART link pins: GPIO28 (P4 RX) / GPIO29 (P4 TX) on JP1 — confirmed end-to-end with the S3.
- ✅ USB mass-storage host: works on the HS USB-C port; stable once MP3s are preloaded to PSRAM
  (streaming during playback tripped a USB-DWC assert).
- ✅ Audio decode/output: MP3 via minimp3 → ES8311/I2S, 44.1 & 48 kHz.
- ✅ Display/touch: ST7701S MIPI-DSI (IDF 5.5 + `CONFIG_ESP32P4_REV_MIN_FULL=0`) + GT911.
- ✅ microSD/TF slot: GPIO39-44, P4 SDMMC slot 0, FATFS mounted at `/sd` with a 32 GB SDHC card.

Still open:

- Which JP1 GPIOs are ADC-capable (not needed — pitch fader lives on the S3).
- Audio line-output quality / RCA routing from the ES8311.
- Whether ESP32-C6 wireless is needed in the MVP or should wait.
