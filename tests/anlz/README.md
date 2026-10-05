# ANLZ Parser Tests

Documentation status: current host-test fixture guide, reviewed 2026-10-06.

PC test harness for `rekordbox_anlz.c` — runs on the host without any ESP32 hardware.

## Build requirements

- Linux / macOS: `gcc` (standard)
- Windows: MinGW-w64 (`winget install msys2.msys2`, then `pacman -S mingw-w64-ucrt-x86_64-gcc`)

## Build and run unit tests

```bash
make test
```

Example output from the earlier fixture set (counts are historical):

```
Pajoniiir ANLZ Parser Test
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
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
idf.py --version # must report ESP-IDF v6.0.2
cd firmware/main-deck-p4
idf.py build
```

For CI-equivalent host coverage, run `tests/run_p4_host_tests.ps1` from the
repository root. `-ListSuites` lists current selectable suites. The current
parser adds 1-based PQTZ downbeats, separate bounded memory cues, validated PVBR
and PWV4 color previews; valid/corrupt fixtures do not establish real-media
hardware acceptance. Shared library changes require both P4 board builds.
