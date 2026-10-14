# JC8012P4A1 result, group A (display and boot), 2026-10-14

Image: `main-deck-jc8012p4a1` built from `jc8012p4a1-v3` (IDF 6.0.2 container), flashed through the CH340 port.
Observations by the operator on the unit; no photos yet.

| # | Check | Result |
| --- | --- | --- |
| A1 | Boots without panic | pass for the 10 s read (no panic); `board_id` line not checked |
| A2 | Landscape orientation (rotation 270), colours | pass |
| A3 | Backlight dims and restores | pass after the fix: first run failed (GPIO on/off, the LEDC driver faults on chip v3.2); the backlight now uses the MCPWM at 25 kHz and the dimmer works |
| A4 | UI layout at 1280x800 | pass (overview, library, settings: no cut-off or overlap reported) |
| A5 | Touch | pass with `swap_xy` only; the GSL3680 driver works |

Touch flags tried (rotation 270): swap_xy + mirror_x inverted; swap_xy + mirror_y mirrored left/right; all three rotated 180 degrees; **swap_xy alone is correct**.

Boot log notes: SD mount fails 3 times with 0x107 (no card inserted, not E9); `usb_host_mgr: recovering USB1 reason=1` loops with nothing plugged in (8 fast cycles, then every 30 s).

## Group B (USB roles), 2026-10-14

Cabling first run: DDJ-400 on the FS port, stick behind the hub on the HS port: the DDJ-400 was rejected (`reject MIDI controller outside USB1 direct root: port=1`). After swapping the cables the roles of `board_capabilities.c` (controller root 0, storage root 1) are confirmed with no code change.

| # | Check | Result |
| --- | --- | --- |
| B1 | DDJ-400 enumerates on the HS port (root 0) | pass after a fix: MIDI and USB audio (UAC1, 4ch, 44.1 kHz, 16 bit, endpoint MPS 576). The first run failed with `UAC unavailable ... ESP_ERR_NOT_SUPPORTED` |
| B2 | Stick mounts on the FS port, library scans | pass: `USB media library loaded: 22 tracks`; read speed at 12 Mbit/s not measured |
| B3 | Port to root mapping | HS port = root 0 (controller), FS port = root 1 (storage); matches the table |

SD card inserted with the controller profile: mounts first try (SD02G, SDSC, 1910 MB, 4-bit, 20 MHz); `profile=local 'DDJ-400'`. Operator: the DDJ-400 MIDI controls and LEDs work. One boot only: the E9 warm-reset count (D1) is not done.

ESP-Hosted: host 2.13.0 against C6 firmware 2.12.0 (`Version mismatch`), and two `service_log: sync barrier acknowledgement timed out` per boot; not investigated.

### USB audio fix (2026-10-14)

Cause: the project replaces the esp-usb FIFO split on the HS controller (`apply_espressif_usb_fifo_patch.cmake`); the periodic OUT FIFO was 160 lines (640 bytes) on the JC1060 only and 128 lines (512 bytes) elsewhere. The DDJ-400 audio OUT endpoint has MPS 576, so the driver refused it: `HCD DWC: EP MPS (576) exceeds supported limit (512)`, then `Claiming interface error: ESP_ERR_NOT_SUPPORTED`. The descriptors were parsed correctly (dumped on the unit): the format selection was not the problem.

Fix: JC8012P4A1 joins the JC1060 in the patch (160 lines) and in the 640-byte packet admission of `controller_usb_audio_stream.c`. Setting `CONFIG_USB_HOST_HW_BUFFER_BIAS_PERIODIC_OUT` alone did nothing, because the patch replaces that computation.

Group C, first listening (operator): a track loaded and playing; the sound is correct on the controller, on MAIN and on the cue (PFL). Not done: the 10 min and 60 min soak with `monitor_p4_reliability.ps1`, the counters, the NS4150 silence check.

## Group D (storage and Wi-Fi), 2026-10-14, unattended

| # | Check | Result |
| --- | --- | --- |
| D1 | microSD mounts at every boot | **fail, E9 reproduced**: 10 warm resets (RTS on CHIP_PU, reset reason 1), SD02G card inserted. Boots 1, 3, 5, 7, 9 failed all 3 mount attempts (`0x107` timeout, each time `sd_host_del_sdmmc_controller ... host controller with slot registered`); boots 2, 4, 6, 8, 10 mounted at the first try. Strict alternation, always a failure after a good boot. No panic. 10 cold boots not done (needs a power cycle by hand) |
| D2 | Wi-Fi AP+STA WPA2 default ON | partial: from an INFO-level build the log shows `softap started`, `web AP started: ssid=Pajoniiir-JC8012`, DHCP server on 192.168.4.1, HTTP server on port 80, mDNS `pajoniiir.local`. No error in 60 s. Not checked from a client: the PC has no Wi-Fi interface. STA join not tested |
| D3 | Audio counters with web polling | not run (needs a client on the AP and playing decks) |
| D4 | Wi-Fi toggle off and on | not run on purpose (the reference fork panicked on the JC1060; recovery image needed) |

On a boot where the SD mount fails, the retries (3 x about 3 s) delay the start by about 7 s. With a good boot the DDJ-400 profile from the SD is loaded (`profile=local`); with a failed one it falls back to `profile=unsupported`, so the controller has no mapping until the next reset.

ESP-Hosted: host 2.13.0 and C6 firmware 2.12.0 (`Version mismatch`); `service_log: sync barrier acknowledgement timed out` seen at about 13 s and 26 s at WARN level in several boots, but not in the 60 s INFO-level boot.
