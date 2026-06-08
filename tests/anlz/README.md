# ANLZ Parser Tests

PC test harness for `rekordbox_anlz.c` — runs on the host without any ESP32 hardware.

## Build requirements

- Linux / macOS: `gcc` (standard)
- Windows: MinGW-w64 (`winget install msys2.msys2`, then `pacman -S mingw-w64-ucrt-x86_64-gcc`)

## Build and run unit tests

```bash
make test
```

Expected output (all PASS):

```
CDJ100S-XXX ANLZ Parser Test
============================

=== Building synthetic test files ===
  Created: test_synth.dat
  Created: test_synth.ext

=== anlz_parse_dat() ===
  parse_dat returns ESP_OK                           PASS
  audio_path correct                                 PASS
  BPM = 128                                          PASS
  ...
Results: 28/28 passed  — ALL PASSED
```

## Run with a real Rekordbox USB drive

Copy `ANLZ0000.DAT` (and optionally `ANLZ0000.EXT`) from a Rekordbox-formatted USB
drive (`PIONEER/USBANLZ/<artist>/<track>/`) to your PC, then:

```bash
./test_anlz /path/to/ANLZ0000.DAT /path/to/ANLZ0000.EXT
```

## ESP-IDF firmware build

The parser is part of the `library` component and builds as part of the P4 firmware:

```powershell
cd firmware/main-deck-p4
idf.py build
```
