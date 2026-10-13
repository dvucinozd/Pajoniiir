# JC8012P4A1 hardware bring-up plan (draft, 2026-10-13)

One physical unit, no spare. Same rules as the JC1060 campaign
(`docs/validation/jc1060/HARDWARE_TEST_PLAN.md`): back up first, one change at a time, record
failures without silently fixing, **no `idf.py build` or flash without an explicit GO**.
Fill `RESULT_<image>.md` next to this file; keep raw evidence under `evidence/`.

## 0. Before power on

| # | Check | Pass |
| --- | --- | --- |
| 0.1 | Read the board label (V1/V2/V3) and photograph both sides; note the revision | done: V3, SKU 10153001-V3 (2638), chip v3.2 |
| 0.2 | Wiring for the first run: USB-C UART (CH340C) to the PC; DDJ-400 on the HS USB-C and the stick on the FS USB-C, each through a self-powered hub that also supplies the board, or note what was used | recorded |
| 0.3 | Back up the 16 MB flash with esptool before any write; record SHA-256 (`RECOVERY_IMAGE.md`) | done: SHA-256 51e4ff9b...50ec3b (see BOARD_NOTES) |
| 0.4 | Capture the vendor image's boot log (chip revision, flash size, PSRAM size) | done (see BOARD_NOTES) |

## A. Display and boot (no touch expected)

| # | Check | Pass |
| --- | --- | --- |
| A1 | Image boots, no panic, `board_id` `jc8012p4a1` in the log | log |
| A2 | Panel shows the UI in landscape, correct orientation (rotation 270 is an assumption), no tearing, colours right | photo |
| A3 | Backlight dims and restores | observed |
| A4 | UI layout at 1280 x 800: nothing cut off or overlapping on the overview, library and settings pages | photos |
| A5 | Touch: expected display-only (GSL3680 not integrated) | noted |

## B. USB roles

| # | Check | Pass |
| --- | --- | --- |
| B1 | DDJ-400 enumerates as MIDI + USB audio on the HS port (root 0) | log |
| B2 | Stick mounts on the FS port (root 1) behind the hub; library scans; note read speed at 12 Mbit/s | log |
| B3 | Swap ports to confirm which USB-C maps to which root; correct `board_capabilities.c` if wrong | recorded |

## C. Audio, controller and soak (as JC1060 groups C, D, H)

DDJ-400 control and LEDs; MAIN on USB channels 1/2 and cue on 3/4; two decks for 10 min, then 60 min,
with `tools/monitor_p4_reliability.ps1` in Observe mode and counters at 0; listening notes by the operator.
Confirm the codec amplifier (NS4150, GPIO20) stays silent.

## D. Storage and Wi-Fi (the open risks)

| # | Check | Pass |
| --- | --- | --- |
| D1 | microSD mounts at every boot, 10 warm resets and 10 cold boots; count failures (the JC1060 failed about every other warm reset with ESP-Hosted linked) | counts |
| D2 | Wi-Fi AP+STA WPA2 default ON: AP visible, web UI reachable, STA join, `pajoniiir.local` | log |
| D3 | Audio counters unchanged with Wi-Fi traffic (web polling at 4 Hz for 10 min) | counters |
| D4 | Wi-Fi toggle off and on **only with a recovery image ready** (the reference fork panicked on the JC1060) | record |

## E. Out of scope until later

OTA (needs the signing key), DJ Link over Wi-Fi, touch (GSL3680 driver and IDF 6.1), recorder.
