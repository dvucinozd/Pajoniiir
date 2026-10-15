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
| D1 | microSD mounts at every boot | **fail, E9 reproduced (fixed afterwards, see D1 follow-up)**: 10 warm resets (RTS on CHIP_PU, reset reason 1), SD02G card inserted. Boots 1, 3, 5, 7, 9 failed all 3 mount attempts (`0x107` timeout, each time `sd_host_del_sdmmc_controller ... host controller with slot registered`); boots 2, 4, 6, 8, 10 mounted at the first try. Strict alternation, always a failure after a good boot. No panic. 10 cold boots not done (needs a power cycle by hand) |
| D2 | Wi-Fi AP+STA WPA2 default ON | partial (AP and web UI pass): from an INFO-level build the log shows `softap started`, `web AP started: ssid=Pajoniiir-JC8012`, DHCP server on 192.168.4.1, HTTP server on port 80, mDNS `pajoniiir.local`. No error in 60 s. Operator, from a phone on the AP (password in `wifi_link.h`): the web page is shown at http://192.168.4.1. Not tested: `pajoniiir.local`, STA join |
| D3 | Audio counters with web polling | not run (needs a client on the AP and playing decks) |
| D4 | Wi-Fi toggle off and on | not run on purpose (the reference fork panicked on the JC1060; recovery image needed) |

### D1 follow-up, 2026-10-15: E9 root cause and fix

Cause: a warm reset does not power the card down. With the LDO (channel 4) off the card is still kept alive through the pull-ups on CLK/CMD/D0-D3, so after a good mount it comes back in its previous 4-bit state and the next boot fails (`sdmmc_init_sd_ssr` then `send_op_cond` timeouts, `0x107`); the failed boot leaves the card idle, hence the strict alternation.

Eliminated by test (10 warm resets each, alternation unchanged): switching the LDO off for 250 ms alone; holding the C6 reset line (GPIO54) low during the mount.

Fix (`bsp_sd_init`, JC8012P4A1 only): release the LDO and drive CLK/CMD/D0-D3 low for 150 ms before the first mount. Result: 8/8 at 500 ms, then 20/20 and 6/6 at 150 ms warm resets, SD mounted at the first attempt every time, `profile=local`, SD up at about 4.0 s. 10 cold boots not done. Not applied to JC1060/JC4880 (same shared file, not tested there; JC1060 shows the same E9 symptom and is the first candidate).

On a boot where the SD mount fails, the retries (3 x about 3 s) delay the start by about 7 s. With a good boot the DDJ-400 profile from the SD is loaded (`profile=local`); with a failed one it falls back to `profile=unsupported`, so the controller has no mapping until the next reset.

ESP-Hosted: host 2.13.0 and C6 firmware 2.12.0 (`Version mismatch`); `service_log: sync barrier acknowledgement timed out` seen at about 13 s and 26 s at WARN level in several boots, but not in the 60 s INFO-level boot.

## Group C/D3 soak attempt, 2026-10-14 (not validated)

`tools/monitor_p4_reliability.ps1` now accepts `-ExpectedBoard jc8012p4a1` (`-ExpectedLibraryTracks 22` for this stick). A 30 s run with the decks stopped passes. A 10 min Observe run with both decks playing (web polling at 4 Hz over the AP) stopped at 397 s of 600: `status API unavailable for at least 15 seconds` (`/api/status`, then `/api/diagnostic-log`, both 15 s timeouts).

- Before the outage: both decks playing, UAC streaming, counters at 0 (`output_late`, `uac_dropped`, `uac_overflow`, `uac_underflow`, `service_log_dropped`); status latency peaks 1.5 to 5.2 s.
- Cause **not established**. The PC Wi-Fi card (Realtek RTL8812AE, `rtl8821ae`) lost the AP at the same time (`Connection to AP ... lost`) and then failed with `Init MAC failed` / `not ready ... after DPC`, so the outage may be on the PC side. The board was then restarted by opening its serial port (the serial open resets it), so whether it had crashed is unknown. The crash dump present afterwards is the old LEDC fault (other ELF hash).
- A set of six resets that tried to correlate the SD mount result with the AP being visible is **invalid**: the PC card could no longer scan.
- Redo needed: serial capture opened before the run and not touched during it, a reliable Wi-Fi client, then 10 min and 60 min.
- The operator had to restart the board once because the SD card was not detected (consistent with E9, D1).

## Power and Wi-Fi AP lost after a PC restart, 2026-10-14

Setup during the tests: the tablet is powered by the self-powered hub and, at the same time, by the PC through the USB-TTL port (the CH340 used for flashing and the serial log). The schematic ties the VBUS of the three USB-C connectors to `USB5V_IN` (BOARD_NOTES), so two supplies feed the same rail. The operator reports that the PC USB port alone cannot power the tablet: it resets in a loop.

Observation: after the PC was restarted, the AP `Pajoniiir-JC8012` was no longer visible on the phone or on the PC, although the serial log of every boot (INFO level, SD mounted or not) showed `softap started`, DHCP on 192.168.4.1 and `Wi-Fi remote enabled`. Resetting the board by RTS (about 10 boots) did not bring the AP back. The AP came back after the operator unplugged both supplies (hub and PC USB) for about 10 s and plugged them back in.

- Reading: the ESP32-C6 radio can be left in a state that a reset of the P4 (GPIO54 pulse) does not clear; a full power cut does. Cause not established. Candidates: the PC USB 5 V dropping and returning during the PC restart while the hub keeps feeding the same rail, or the two supplies fighting.
- Consequence for the plan: do not leave the PC USB as a second power source. Use a data-only serial cable (VBUS cut) or an isolator, or power from the PC USB only if it can supply the tablet (it cannot).
- Not tested: whether the AP is also lost by a warm reset alone without a PC restart (the earlier D2 and the 4 INFO boots say no).

## Group C, serial-only soak of about 11 min, 2026-10-14 (partial)

Both decks playing, DDJ-400 on the HS port, stick on the FS port, SD mounted, Wi-Fi AP up (started at 7.3 s); the tablet is powered by the hub and the PC USB at the same time (see the power note above). Evidence: the serial log only, because the PC Wi-Fi card is not reliable, so there are no counters from `monitor_p4_reliability.ps1`.

- 654 s of log from the last boot: no panic, abort, watchdog, brownout or `E (` line; no underrun, overflow or dropped-frame message (the only matches of the search are boot lines of the PSRAM and GPIO drivers).
- 427 decode lines for the two decks: decode time at most 41.7 ms for a block, ring between 78 528 and 93 696 frames, steady.
- The first capture of the run had a boot with the SD mount failing (E9); the operator reset the board, and the next boot mounted the SD.
- Not shown: audio counters (`output_late`, `uac_*`), the 60 min run, the NS4150 silence check, a client polling the web API. Group C stays partial.

## Display freeze under API polling, 2026-10-15 (cause found, fix applied)

Symptom: after 2 to 18 min of `/api/status` + `/api/library` + `/api/firmware` + `/api/resources` polling (the reliability monitor pattern), the LVGL task stops (UI frozen, audio and API still fine, no log line). The 60 min Observe soak with the monitor passed its audio counters (`output_late` 5, everything else 0) but the UI froze at about 17 min; the monitor does not check the UI.

Diagnosis: a probe task (LVGL heartbeat, `abort()` after 8 s) and a printout of the blocked LVGL task stack showed it waiting forever in `ppa_do_operation` (`xSemaphoreTake(trans_elm->sem, portMAX_DELAY)`) under `ui_overview_blit_wave_overlay_rgb565` -> `ppa_do_scale_rotate_mirror`. Known upstream bug: espressif/esp-idf #19023 and #19096 (270 degree RGB565 SRM into a DSI framebuffer that is being scanned out never completes), fixed on master by commit 469aa16 (DIG-734 workaround did not swap width/height and x/y scales for 90/270 degree), backport announced for 5.5, 6.0 and 6.1, not in 6.0.2.

Fix: `firmware/p4-core/components/esp_driver_ppa` is a copy of the IDF 6.0.2 component with that change in `ppa_srm.c`, added to `EXTRA_COMPONENT_DIRS` of `main-deck-jc8012p4a1` only. Drop it when the IDF in use carries 469aa16.

Result with the fix (instrumented image, probe armed): 45 min of the same polling at 4 Hz, 10 782 requests, 0 errors, 1.0 s max latency, both decks playing, UI render counter still rising at the end, no probe trigger. Before the fix the same test froze after 2, 6, 6 and 7 min. Not a proof (the bug depends on block geometry); the clean-image soak below confirms or not.

Noted on the way: the Task WDT at 1073 s in one run (httpd on CPU0 reading the coredump partition on each `/api/status`, `esp_core_dump_image_check`) is a separate effect, not reproduced after the cache experiment; not fixed.
