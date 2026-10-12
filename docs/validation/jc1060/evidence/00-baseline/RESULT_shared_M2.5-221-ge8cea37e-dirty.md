# JC1060 result: 0/O/A/B1 shared-core image first session

| Field | Value |
| --- | --- |
| Date / operator | 2026-10-12 / kay |
| Plan scenarios | 0.2, 0.3, O1, O4, A2 (partial), A5 (partial), B1 (one direction) |
| Board / panel revision | JC1060P470C_I_W_Y / not captured (A1) |
| `/api/status` board, project | not reachable (no HTTP server, see O4); UART: `main-deck-jc1060` |
| Version string | `M2.5-221-ge8cea37e-dirty` (dirty = `dependencies.lock` hash fix, 2 hashes) |
| Source SHA | `e8cea37e` + uncommitted `dependencies.lock` change |
| ELF SHA-256 | `2d64b587b3efe683cc469aa12acdd0867e40a2d30ebaa6c0e87a540f2d621906` (app .bin `7517b54866d6d61486b3c029959a2421ea636aeead43c9d5be9c8312cc8055bd`) |
| Flash method | wired (esptool 5.5.0), bootloader 0x2000, ptable 0x8000, otadata 0x10000, app 0x20000, each verified by hash |
| Peripherals | DDJ-400 (boots 1-2 only), SD02G 1910 MB, USB stick (22 tracks), no hub in the passing run |
| Build flags | regular |

Pre-flash backup of the 16 MiB flash (v343): `~/.cache/pajoniiir-hil/flash_backup_fork-ref_v343.bin`, SHA-256 `2a19881041d57465be6b772460901451b6b3ee3b206f6d27021409c2e46a041a` (not in git).

## Results

| Scenario | Result | Evidence file | Notes |
| --- | --- | --- | --- |
| 0.2 | PARTIAL | `boot_shared_M2.5-221-ge8cea37e-dirty_reset2.log` | 4 images written and hash-verified; new 16 MiB table in boot log; boots. No readback of whole flash after write, NVS preservation not checked |
| 0.3 | NOT RUN | | `/api/status` unreachable. Identity only from UART `fw_health` line |
| 0.4 | NOT RUN | | needs OTA path |
| O1 | PARTIAL | boot logs | Boot log readable at 115200 on ttyUSB0 incl. ROM/bootloader/app; adapter unplug/replug and TX/RX GPIO record not done. First capture after RST missed the boot (late attach) |
| O4 | FAIL | | No HTTP at 192.168.100.83: port 80 refused, `pajoniiir.local` unresolved, ping OK. `web_server_start()` is only called from `wifi_link.c`, which is not started on this board (wifi=false, boot_network_required=false) |
| A2 | PARTIAL | boot logs | 3 boots observed (1 POWERON after flash, then 2 more), UI reached per operator, no panic/WDT/`alloc FAILED`; 10 cold boots and boot-time record not done |
| A3 | PARTIAL | | Operator: display clean, no flicker/artefacts. Geometry/colour-bar test not run |
| A4 | PARTIAL | | Operator: touch responds. Full-screen grid test not run |
| A5 | PARTIAL | | Operator: tab navigation works, Library shows tracks. Screensaver return, each tab not itemised |
| B1 | PARTIAL | `B-usb-roles/boot3_stick_direct_ddj_unplugged.log` | Stick directly on storage port: mounts, `USB media library loaded: 22 tracks`. Stick behind a hub: rejected (`rejecting MSC addr=3 outside the USB0 direct root`, `ESP_ERR_NOT_SUPPORTED`). Reverse (stick on controller port) not tested |
| B2 | PARTIAL | `B-usb-roles/boot4_stick_behind_hub_ddj_direct.log` | DDJ-400 (MIDI, port 0) and stick (22 tracks) enumerate together at 8.6 s, no fault. `validate_p4_dual_usb_log.py` cannot judge: image emits no `PHASE1 STATUS` lines and the tool requires `msc_direct`, which a hub wiring contradicts. No duration or replug evidence |
| B3 | PASS (after fix) | `B-usb-roles/boot4_stick_behind_hub_ddj_direct.log` | Image 2 (ELF SHA-256 `791f3faf38999d5ea28c23cf734df90a2f727ca1ceccd9706a61afe58712c01d`, app `bf9a9bf2060a1af98334b7348dfcaf74ed1722df8f709be464bd4a8248c81499`): self-powered hub on storage connector, stick behind it, DDJ-400 direct. `USB media library loaded: 22 tracks` at 8.6 s, no `rejecting MSC`, no panic. Fix: `board_capabilities.storage_via_hub` (JC1060 only) skips the root check in `usb_storage_shared.c`, because the USB stack reports `parent.dev_hdl == NULL` behind a hub (hub.c:235, IDF-10023), so the root cannot be resolved. Hot-plug (B4/B5) and brownout not tested |

## Anomalies

- No HTTP server on Ethernet (O4): code gap, not a log-level effect.
- Stick behind hub refused by design (see B3). Contradicts the plan's B3 assumption.
- SD mount unstable between boots: boot 2 failed 3/3 (`0x107`, `sd_host_del_sdmmc_controller: host controller with slot registered`), boot 3 mounted. v343 mounted on attempt 2/5 (retry count here is 3). Relates to E9.
- DDJ-400: UAC unavailable (`ESP_ERR_NOT_SUPPORTED`), MIDI active; profile `unsupported` while SD was not mounted.
- `usb_host_mgr: recovering USB1 reason=1` repeating every 30 s with no storage device on the right root.
- Board power-cycles (`POWERON`) when a USB connector is changed; reason not established.
- Different USB stack from the v343 run: fork-ref carried local patches in `managed_components/usb`; the shared lock pins `dvucinozd/esp-usb` `cc65dc26`.

## Decision

Group O has a FAIL (O4): the plan says stop. Operator decision pending between (a) continuing the UART-only scenarios (A, B1, C MIDI, E on SD) with O4 recorded as FAIL and a fix list, or (b) restoring the v343 backup and fixing HTTP-over-Ethernet first.

Work items (O8): HTTP over Ethernet without Wi-Fi; storage behind a self-powered hub; SD mount reliability; DDJ-400 UAC on the shared core; boot log level for acceptance sessions.

## Update 2026-10-12 (image 2)

- B3 fixed (see table). Cost: on JC1060 a stick on the controller connector is no longer refused (B1 reverse check not possible with this wiring).
- Still open: UAC `ESP_ERR_NOT_SUPPORTED` with DDJ-400; `profile=unsupported` for DDJ-400 although SD02G mounted (no `/sd/controllers` profile line in the log); O4 HTTP.
- Not built: JC4880 and M3 (shared files `board_capabilities`, `usb_storage_shared.c` changed); host tests passed before the final simplification, not rerun.
- O8 work item: no `PHASE1 STATUS` instrumentation; dual-USB validator unusable on the shared image.

## Update 2026-10-12 (profile and UAC)

- C1: PASS (profile). SD held the fork-ref S3CP v2 profile (6176 B, `profile=unsupported`); replaced with repo v4 `controllers/pioneer_ddj_400/profile.s3bin` (6048 B, SHA-256 `ae2d1271e6b19f28916324393b54b9e64d94f8a69f8c32f8c2e94f76ae2d10fe`, recompiled identical from `profile.json`). Boot log `C-ddj400/boot5_profile_v4_sd.log`: `profile=local 'DDJ-400'`. Labels in UI match not yet checked.
- C2: first PLAY press on deck 1 gave no screen change and no UART line (WARN level hides MIDI), taken with the old profile; not repeated with the v4 profile.
- D1/D2 blocked: `UAC unavailable ... ESP_ERR_NOT_SUPPORTED` comes from `select_stream_format()` (`controller_usb_audio_stream.c:210`), not from the root filter. Fork-ref notes (`docs/jc1060-audio-glitches.md`): DDJ-400 is HS, UAC1, alt1 = 4ch S16LE 44.1k, alt2 = 4ch S24_3LE, no feedback endpoint. Shared core: HS goes to `controller_uac2_parse` only (UAC2), FS generic needs async/feedback. Probable gap (descriptor not captured on this unit).

## Update 2026-10-12 (C2/C3 session 1, profile v4, no audio output)

Evidence: `C-ddj400/c2c3_session1_uart.log` (8 lines) plus operator observation of the screen. UART at WARN does not trace PLAY/CUE/pitch/pads/faders, so these rest on the operator only.

| Control | Result | Basis |
| --- | --- | --- |
| PLAY | PARTIAL | operator: OK on screen |
| CUE | PARTIAL | operator: OK |
| Pitch fader | PARTIAL | operator: OK |
| Pads (hot cues) | PARTIAL | operator: OK |
| Channel faders | PARTIAL | operator: values move |
| SYNC, jog VINYL/CDJ, loop in/out | NOT RUN / not reported | UART: `loop out ignored: invalid in/out 0/0 ms` (x4), `scratch begin D1 rejected: loaded=0` |
| Crossfader, EQ, filter, trim | NOT REPORTED | |

C2 and C3 stay PARTIAL: the plan requires evidence in this tree, and no audio sink exists (`output sink fault: main=ESP_ERR_INVALID_STATE hp=ESP_ERR_NOT_SUPPORTED`). One `audio: no audio frame found` at 251 s, track unknown.

## Update 2026-10-12 (image 4: HS UAC1 fallback + interval fix)

Image 4: app SHA-256 `71d6d28a2778e126bce97ce9fc65f7834a1f218a32515bcabb71b244b644b95a`, ELF `762193d2a2480958d3a5a5c9333d397c862448c4d734c5cb44548296f533aae7`, flashed wired at 0x20000 (hash verified). Logs: `D-audio/boot7_uac1_interval_fix.log`, `D-audio/d1_first_play_uart.log` (empty: no fault line during the play).

- Cause of `UAC unavailable` (two blockers in `controller_usb_audio_stream.c`): (1) HS devices only tried UAC2; (2) `flx4_uac_format_supports_rate()` requires `interval == 1`, DDJ-400 reports HS `bInterval 4` (= 1 ms). Fix: HS falls back to UAC1 and maps interval 4 to 1 for selection. Verified against the real parser on a descriptor rebuilt from `C-ddj400/ddj400_lsusb_v.txt` (descriptor not captured raw; DDJ was no longer on the PC).
- D1: PARTIAL. Operator: sound comes out, clean, first minute or so, deck 1. No `output sink fault`/underrun line in the UART. Not done: both decks, MAIN ch1/2 vs cue ch3/4 identified, 10 min soak, D8 counters (no API).
- Open: output sample rate is fixed at 44.1 kHz on this path (DDJ-400 only offers 44.1 kHz), so 48 kHz material is resampled; D3/D9 need listening checks.

## Update 2026-10-12 (D1 two decks, C2/C3 session 2; image 4)

Evidence: `D-audio/d1_two_decks_10min_uart.log` (0 lines), `C-ddj400/c2c3_session2_uart.log` (0 lines). All results below rest on operator observation; UART at WARN traces none of these controls.

- D1: PARTIAL. Two decks playing, master and headphones audible, clean per operator. Log window 04:52:27-04:57:57 (5.5 min, not the 10 min required), no fault line. MAIN ch1/2 vs ch3/4 not distinguished.
- C2: SYNC OK, jog OK (VINYL), loop in/out OK (all operator, PARTIAL). Jog CDJ mode NOT RUN: operator could not find it; `profile.json` has no explicit vinyl/CDJ mode event (jog_touch, jog_search_touch, shift only), so the plan wording may not apply to this profile.
- C3: mixer OK per operator (crossfader, EQ, filter, trim not itemised): PARTIAL.
- Earlier refusals (`loop out ignored: invalid in/out 0/0`, `scratch begin D1 rejected: loaded=0`) were not repeated in this session.

## Update 2026-10-12 (C4, C5, C7; image 4)

Evidence: `C-ddj400/c7_replug_uart.log` (5 lines). C4/C5 rest on operator observation only.

- C4: PARTIAL. PFL/CUE sends sound to the headphones only, master unchanged (operator). PFL LED follow-up is part of C5.
- C5: PARTIAL. DDJ-400 LEDs follow the on-screen state (operator). Which LEDs were checked is not itemised.
- C7: FAIL (no automatic recovery). Unplug/replug during playback: `E audio: output sink fault: main=ESP_ERR_INVALID_STATE hp=ESP_ERR_NOT_SUPPORTED` at 885.1 s, `controller disconnected` at 885.2 s, controller redetected and profile `local` reloaded at 892.7 s, no `UAC unavailable`. Audio did NOT return by itself; it returned after reloading a track (operator). Code reading (not traced on hardware): `audio_output_mark_sink_fault()` (`audio_engine.c:3860`) stops loaded decks and sets `s_output_run = false`; the output task exits and is only restarted through `audio_output_service_ensure_started()` (open/sink-switch paths), not on controller reconnect.
- Work item (O8): resume playback output after controller reconnect without a manual reload; decks are left stopped with `AUDIO OUTPUT ERR`.

## Update 2026-10-12 (image 5: HTTP over Ethernet)

Image 5: app SHA-256 `266923fc8417a623fb812acea930aec15e8cc8c2821562fb38c1d41202d370b5`, ELF `0784ba17c769507842f2fd5743dd9961c39e6262242e23fb71848e5308c95f99`, wired flash at 0x20000 (hash verified). Evidence: `O-observability/boot8_http_ethernet.log`, `api_status_shared_img5.json`, `api_resources_shared_img5.json`, `api_ui-timing_shared_img5.json`.

- Fix: `main-deck-jc1060/main/app_main.c` starts `web_server_start()` after `pajoniiir_common_app_main()`; `web_server.c current_ap_ipv4()` falls back to the `ETH_DEF` netif address for the Host filter.
- O4: PASS for `/api/status`, `/api/resources`, `/api/ui-timing` at 192.168.100.83 (HTTP 200) and `pajoniiir.local/api/status` (200); forged `Host: evil.example` gets 403. No `HTTP server unavailable` in the UART. Regular log level, uptime 34 s at capture.
- 0.3: see ELF comparison below.
- `/api/resources`: allocation_failures 0, critical_allocation_failures 0, startup_phase ready.
- `/api/status` `uac_format`: 44100 Hz, 16 bit, high_speed true, streaming true, faulted false (DDJ-400 UAC1 path); `main_sink` usb; `sd_metrics` transfer_errors 0.
- Not covered: heap minima over time, O5 tool runs, O6/O7.
- 0.3: PARTIAL. `/api/status` reports `board_id` jc1060, `project` main-deck-jc1060, `source_sha` e8cea37e (= HEAD) with `source_dirty: true`. It does NOT report the running ELF SHA-256 (`app_elf_sha256_prefix` belongs to `crash_dump` and is empty). The ELF SHA-256 `0784ba17...` equals the one in the flashed .bin app descriptor (checked on the file, not read back from the unit). Work item: expose the running image's ELF SHA in `/api/status`.
