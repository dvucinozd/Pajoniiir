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

## Correction (0.3) and O5 first run

- Correction: the running image's ELF SHA-256 IS exposed, by `GET /api/firmware` (`image_elf_sha256`), not by `/api/status`. Value `0784ba17c769507842f2fd5743dd9961c39e6262242e23fb71848e5308c95f99` equals the ELF of the build flashed as image 5 (`O-observability/` capture below). 0.3 identity (board, project, source SHA, ELF SHA-256) therefore matches the build; `source_dirty` was true for that image.
- O5: container-to-board routing works on the default Docker bridge (no `--network host`), `monitor_p4_reliability.ps1 -ExpectedBoard jc1060 -BaseUrl http://192.168.100.83` reaches the board. First run FAILED by design on identity: `Wrong board, dirty or invalid image identity` because image 5 was built from a dirty tree (the script requires `source_dirty = false`). Needs a rebuild from a clean tree. The build flags dirty via `git status --porcelain`, which also counts untracked files.

## Image 6 (clean tree) and O5 Observe run

Image 6: built from clean HEAD `4ca426d8f0d68db317446281c6d9b6a1b34ad219`, app SHA-256 `8d808ebf2daf91c7cb09137e19b722f949add87055c59696e4b3fbab006bdaf5`, ELF `b73c4e95b9c8c7e27bd6cdec31c1ab298a7b8746972ab68afe0aed63970a3f53`, wired flash at 0x20000 (hash verified).

- 0.3: PASS. `GET /api/firmware`: `board_id` jc1060, `project` main-deck-jc1060, `source_sha` = HEAD, `source_dirty` false, `image_elf_sha256` = build ELF. (`running_version` still reads `M2.5-221-ge8cea37e-dirty`: `git describe` is evaluated at CMake configure time and was not re-run; cosmetic, the identity fields are correct.)
- O5: PASS for the Observe tool run. `monitor_p4_reliability.ps1 -ExpectedBoard jc1060 -Mode Observe -DurationSeconds 120 -ExpectedLibraryTracks 22` with expected source and ELF SHA: result pass, 480 status samples, 0 HTTP errors, max latency 44 ms (library), 22 library tracks, 0 hard failures, `physical_operator_acceptance` NOT RUN (script default). Evidence: `O-observability/o5_observe_120s_result.json`, `..._metadata.json` (raw `status.jsonl` 3.7 MB kept out of git, SHA-256 `31687fbecf4643bf9cfb713524fa7bd6cee72f975aa0ccd310257872765573a2`, in `tmp/o5-observe2/`).
- Counter to interpret: `uac_underflow` rose by 5,289,178 over 120 s with no deck playing (output_late 0, pcm underruns 0, uac_dropped 0). Probably the idle UAC ring padding silence, not a fault; must be re-read during playback (D1/D8) before drawing any conclusion.
- Still open for O5: `run_p4_uac_transition_stress.ps1` not run yet; TimingSoak and UsbRecovery modes not run.

## C7 status (2026-10-12)

C7: FAIL, no fix written. Behaviour options to decide with the other developer: (A) pause the decks cleanly on controller loss and resume on PLAY after reconnect; (B) resume automatically. Current code fails closed in `audio_output_mark_sink_fault()` (`audio_engine.c:3856`, call site 4311) and only a track LOAD restarts the output task. Any fix touches shared real-time audio code: JC4880 and M3 must be rebuilt and the C7 gestures repeated (unplug in playback, replug, PLAY).

## D7 UAC transition stress (image 6, clean HEAD 4ca426d8)

Command: `tools/run_p4_uac_transition_stress.ps1 -BaseUri http://192.168.100.83 -ExpectedVersion M2.5-221-ge8cea37e-dirty -Deck1TrackKey 2 -Deck2TrackKey 18 -Cycles 12` (default keys 115/18 do not exist in this 22-track library). Self-test passes. Evidence: `D-audio/d7_uac_transition_run1.json`, `..._run2.json`.

- D7: FAIL, reproducible. Both runs: cycle 1 clean (15 samples, both decks playing), then deck 1 not playing at `cycle_2_sample_1`; script stops: `both decks are not playing after the transition`. Run 1 at 21.9 s / 25.7 s of operations, 27.4 s total.
- Counters over both runs unchanged: `underflow_frames` 25245486 (constant while playing, so the +5.29 M seen by the Observe monitor with idle decks was idle padding), `overflow_frames` 0, `dropped_blocks` 0, `pcm1`/`pcm2` 0, `uac_data_loss` false, `uac_flags` 0, recovery failures 0. No `output sink fault`, deck or audio warning in the UART for that boot (WARN level).
- Cause not established. The failure is deterministic (same cycle, both runs) and not an audio-path fault by these counters; candidates not yet checked: CUE semantics of this engine/profile versus the script's assumed sequence (cycle 2 is the first with an existing cue point), loop_4 right after restart, the 300 ms hold. Needs INFO-level log or a manual replay of the same gestures.

## D7 cause found (code reading, 2026-10-12)

- Operator replayed the gestures by hand on the DDJ-400 (CUE, PLAY, 4-beat loop on deck 1, twice): worked both times. A third script run with `-CueHoldMilliseconds 1000` failed at the same sample (`cycle_2_sample_1`), so timing is not the cause.
- Cause: the web control action `cue` (`web_server.c:2566`) queues a single `CTRL_EV_BUTTON` press (`value = 1`) and never a release. `handle_cue_button()` (`deck_core.c:2033`) sets `cue_held = true` on press and ignores any further press (`if (state->cue_held) return;`) until a release (`value = 0`) arrives. Cycle 1: the press pauses and seeks to the cue point, PLAY restarts. Cycle 2: the press is ignored, so `play_pause` pauses the still-playing deck, which is the observed "deck 1 not playing". No hardware or audio-path fault (UAC/PCM counters constant).
- So D7 as scripted is a FAIL caused by a web API defect (CUE has no release, remote CUE works once per deck until a physical or UI release) in shared code, not by the JC1060 audio path. The UAC-transition criterion itself (no data loss over the transitions) is not yet exercised beyond cycle 1. The page `app.js`/`index.html` contains no CUE action (grep), so only API clients are affected.
- Candidate fix (not written): make the web `cue` action a tap, i.e. queue press then release. Affects all boards; needs JC4880/M3 builds and a rerun of D7.

## Image 7 (CUE fix) and D7 rerun

Image 7: clean HEAD `9f362cf3781ae19bc735ea2b32cdb6b599f2457b`, app SHA-256 `b6bddf9625b2b46a340c5cbed30614dd4529ed6a64a1ddc7a5b4ce40239136e8`, ELF `adc4961d721d95c90992cd8093f4af2b76c11457404dc936912998dddefcfba8`, wired flash at 0x20000 (hash verified). `/api/firmware`: source_dirty false, ELF SHA equals the build.

- D7: PASS. `run_p4_uac_transition_stress.ps1 -Deck1TrackKey 2 -Deck2TrackKey 18 -Cycles 12` (default hold 300 ms): 12 of 12 cycles, 182 samples, 76.3 s, both decks playing in every sample, `underflow_frames`/`overflow_frames`/`dropped_blocks`/`pcm1`/`pcm2` unchanged between baseline and final, `uac_data_loss` false, `uac_flags` 0, 0 recovery failures. Evidence: `D-audio/d7_uac_transition_run4_cue_fix_pass.json`, `D-audio/d7_run4_uart.log`.
- The earlier D7 FAIL (runs 1-3) is explained by the web `cue` action sending no release; fixed in commit `9f362cf3`. Not yet verified: operator listening during the run (no crackle), D8 over a longer window.
- JC4880 and M3 were compiled with the earlier shared changes but not with this CUE commit; rebuild them before merge.

## D2 (image 7)

D2: PARTIAL. Operator, two decks playing, headphones on the DDJ-400: PFL on deck 1 puts deck 1 in the headphones, the headphone level control is followed, no drift between master and cue. UART `D-audio/d2_cue_uart.log`: 0 lines (no fault). Follow-up (operator): PFL on deck 2 alone OK, PFL on both decks OK. Exact channel pair (ch 3/4) is inferred from the UAC 4-channel stream, not measured.

## D3 (waived scope)

D3: NOT RUN for WAV 16/24/32/float, FLAC, AIFF and sample rates other than 44.1 kHz: the operator's library holds only MP3 at 44.1 kHz (22 tracks), so those formats cannot be exercised on this unit. MP3 44.1 kHz plays (D1, D2, D7). MP3 CBR versus VBR and duration/pitch accuracy are not separately verified. Waived by the operator for this session, not converted to PASS.

## D4 Master Tempo (image 7)

D4: PARTIAL. Operator: Master Tempo on one deck, then both decks together; pitch stays stable and no artefacts at +/-10 % and +/-16 %. No `pcm_underrun1/2` (0), `output_late_count` 0 at the end of the session (API). Not measured: pitch accuracy, only listening.

Anomaly in the UART window (`D-audio/d4_master_tempo_uart.log`, 11 lines, uptime 661.5-665.3 s), not attributed to a specific gesture:
- `deck: queue full, drops=1` -> `drops=16` -> `drops=47` (type 1 = `CTRL_EV_JOG`; `CTRL_QUEUE_LEN` is 32, `deck_core.c:26`), interleaved with `audio: scratch begin D0 rejected: cue pre-roll pending` about every 0.4-1 s.
- Reading of `audio_engine.c:5792-5798` (not traced on hardware): a scratch begin waits up to 60 x 1 ms for `timeline_preroll_pending`, then rejects. Jog events retried during a pending cue pre-roll could back up the 32-slot queue and drop events. Hypothesis only.
- Relation to the tempo fader is unknown (type 1 events are jog). Needs: which deck/gesture produced it (jog touched during CUE?), and whether the drops lose real controller input.
- D4 anomaly follow-up (operator): the jog was possibly bumped during the session, which matches type-1 (jog) events. Treated as an accidental jog touch, not a lost command. Still worth keeping: 47 dropped jog events in under 4 s with scratch rejects shows the 32-slot queue can saturate under a sustained jog; no input loss was reported.

## D5 seek / loop / cue / beat jump (image 7, profile v4)

Evidence: `D-audio/d5_seek_loop_uart.log` (0 lines: nothing traced at WARN). Results are operator observations.

- D5: FAIL (two gestures), rest PARTIAL.
  - Loop in/out: OK. CUE preview: OK. Beat jump forward/back: OK. Seek by touch on screen: OK.
  - Loop 2x and 1/2 (resize): do NOT work.
  - Seek with the jog: does not work, "stops after a few seconds" (operator wording; to clarify whether the position moves then freezes, or the jog is ignored).
- Loop resize cause (profile data, not hardware-checked): neither the repo profile nor the fork-ref profile binds `loop_halve`/`loop_double`. The fork-ref README (`controllers/pioneer_ddj_400/README.md` there) says CUE/LOOP CALL `90/91 51` and `90/91 53` do "memory_call_prev/next, and inside an active loop: halve/double"; the shared profile lists exactly those two notes under `omitted_inputs` ("no current P4 semantic action"), together with memory_store/delete. The firmware has the events (`compile_profile.py:93-94` defines `loop_halve`=13, `loop_double`=14). So the resize buttons are unbound in this profile. Candidate fix: bind `90/91 51` to `deck{1,2}.loop_halve` and `90/91 53` to `deck{1,2}.loop_double` in `profile.json`, recompile `profile.s3bin`, copy to the SD. No firmware build needed. Not written.

## D5 follow-up (profile fix and jog seek analysis)

- Loop resize: fix committed (`97ca2d8d`): `90/91 51` -> `deck{1,2}.loop_halve`, `90/91 53` -> `deck{1,2}.loop_double`; `profile.s3bin` recompiled (6112 bytes, SHA-256 `2fc922ec2986249f4af7c7a077367e4bb5b9e9f22ced5b3d137e853d21f74408`). Not on hardware yet: the file must be copied to `<SD>/controllers/pioneer_ddj_400/profile.s3bin`. Loop resize stays FAIL until retested.
- Jog seek, operator detail: "moves a little, then freezes", same in both directions. Code reading only, no trace (WARN level, `ESP_LOGD` in `on_jog`): paused or held platter scrubs with `position_ms + delta * 3` and calls `audio_engine_deck_seek()` per event (`deck_core.c:2720-2724`) without checking its return value; shift+jog uses `on_jog_search()` (1 s per step, `deck_core.c:2729`). If a seek is rejected while another is in flight, the position would stop updating without a message. Hypothesis, not confirmed. Next data needed: paused or playing, plain jog or SHIFT+jog, and an INFO/DEBUG log or an `audio_engine_deck_seek` return code print.

## D5 retest (operator, profile v4 with halve/double copied to the SD)

- Loop 2x and 1/2: OK (operator). Loop resize no longer a FAIL; D5 loop part PARTIAL (operator only, no trace).
- Jog seek with the track PLAYING: by design `on_jog()` treats a plain jog during playback as a tempo nudge (`audio_engine_deck_jog_nudge`, `deck_core.c:2704-2715`), not a seek; the position only moves a little, then returns to normal. That matches "moves a little, then freezes" (operator). Seeking while playing is SHIFT+jog (`on_jog_search`, 1 s per step) or touching the platter (scratch). Not a confirmed defect. To run: SHIFT+jog while playing, and plain jog while paused (scrub, `position + delta*3`).

## D5 jog: plain jog in pause freezes (image 7), fork-ref precedent

- Operator: SHIFT+jog while playing: OK. Plain jog while PAUSED: "moves a little, then freezes", same in both directions. Plain jog while playing is a nudge by design.
- Precedent (fork-ref `docs/STARTUP_CHECKLIST.md` v326-v329): a jog/fader MIDI stream froze the UI because each MIDI IN completion resubmitted from the callback (~0.3 ms callback + ~0.27 ms submit, ~550/s on core 1). The fork fixed it in v329 by resubmitting from the task loop at most once per 3 ms.
- Shared core had no pacing (`controller_usb_host.c` resubmitted in the completion callback). Port written (commit following `5056bcf5`), host tests pass. Hypothesis: same cause; the earlier `deck: queue full, drops=47` of type JOG fits an overloaded core, but the cause on this unit is not measured. Needs flash + retest (plain jog in pause); if unchanged, the seek storm (`audio_engine_deck_seek` per event, `deck_core.c:2720`) is next.

## D5 jog scrub in pause, image 8 (MIDI IN pacing, HEAD b34a73b7, ELF 9adeffd0aac08de3420875006bbe4cdddc941dc48b103b172352539bf7d4a196)

Image 8 verified by `/api/firmware` (source_dirty false, ELF equals the build). Operator after the pacing change: plain jog in pause, "same behaviour" (position moves then stops). So the MIDI IN pacing did not cure it; it is kept as a measured-by-the-fork improvement, not as the fix.

Measured run (`D-audio/d5_jog_paused_poll.jsonl`, `/api/status` deck1 polled every ~100 ms for 45 s, operator turning the jog in pause from t = 0; `d5_jog_paused_uart.log`, 8 lines):
- The engine position DOES follow the jog continuously: 5779 -> 15744 ms in about 4.5 s (about 400 ms per 100 ms sample), then decreasing back, 66 position changes, no HTTP error, deck stays `READY`, never playing. It does not freeze.
- The position is repeatedly pulled back to about 9384-9444 ms (t = 9.5 s, 12.0 s, 12.8 s, 14.0 s), and holds plateaus of about 1 s.
- UART: `audio: scratch begin D0 rejected: cue pre-roll pending`, 8 times, one per platter touch. The scratch path is therefore unavailable in pause and `on_jog()` falls into the scrub path: `position += delta*3` then `audio_engine_deck_seek()` for every jog event (`deck_core.c:2720-2724`).
- Code reading: each user seek on an MP3 prepares an indexed seek and a cue pre-roll (`audio_engine.c:3236-3237`); `timeline_preroll_pending` stays set until the decoder publishes the history (`publish_cue_preroll`, `audio_engine.c:672-687`), which is what rejects the scratch begin. A seek per jog event likely outruns the decode task; later requests overwrite earlier ones (single `seek_requested` slot). Hypothesis, not demonstrated.
- Where the pull-back target comes from (about 9384 ms, possibly the cue point) is unknown.
- D5 jog scrub stays FAIL. Candidate fix, not written: coalesce scrub seeks in `deck_core` (accumulate delta, one seek every N ms with the latest target) instead of one per event.

## D5/D6 jog: reverted attempts and reading of the scratch limit (image 9)

- The MIDI IN pacing (`126c275e`) and the scrub seek coalescing (`6be2adca`) had no effect on the symptom on the unit (operator, image 8 and image 9, same behaviour). Both reverted (`3e33a70f`, `d74262c4`, `git revert`, history kept). `firmware/` equals the tree of `9f362cf3` again; the DDJ-400 profile fix (loop halve/double) is kept.
- The board still runs image 9 (coalescing build, ELF `4977cf88cf424956679e409d2f374d066869cc443032934753aae380630a6424`) until it is rebuilt and reflashed from the reverted tree.
- Operator finding: on the platter top the scratch plays but stops; on the side ring the position seeks but is silent.
- Reading of the code (not reproduced with a test): the scratch reads the capture buffer of `AE_TIMELINE_CAPACITY_FRAMES` = 192000 frames per deck (boot log: `PCM timeline D0/D1 ... capacity=192000 frames`, about 4.35 s at 44.1 kHz); outside that window there is nothing to render. In pause, `audio_engine_deck_scratch_end()` restores the origin position (`return_paused`, `audio_engine.c:5914-5923`), which matches the return on release. A paused ring rotation takes the scrub path in `on_jog()` (seek, no audio render). Hypothesis: by design, not a defect.
- D5 jog scrub / D6: PARTIAL/open. Test to run: scratch on the platter staying within about 4 s of track. If the sound holds, record the 4.35 s limit as a design limit for the other developer.

## D6 scratch window test (operator, image 9)

- Platter scratch staying within about 4 s of track: OK, the sound holds until release (operator). Consistent with the reading of the 192000-frame (about 4.35 s) capture window; the limit itself is not measured, only that scratch inside a short window works.
- D6: PARTIAL. Scratch works within the capture window; it stops outside it (design limit, to discuss with the other developer). Not tested: both decks, scratch while playing, UI/underrun counters during scratch (API and UART showed no fault lines).
- D5 jog scrub in pause: closed as "by design" (platter = bounded scratch, ring = silent seek in pause), not a defect, pending the other developer's view on the window size.

### Correction to the D6 note above (2026-10-12)

The statement "API and UART showed no fault lines" is wrong for the counters. After the jog and scratch tests on image 9 (boot with the coalescing build, scrub in pause, ring and platter, scratch inside the window), `/api/status` `diagnostics` reads `pcm_underrun1` = 108, `pcm_underrun2` = 0, `output_late_count` = 0. Every earlier read of `pcm_underrun1/2` on this unit (D4, D7 runs, O5) was 0. UART: no `E (` line in the test window. The 108 underruns are on deck 1, the deck used for all jog/scratch tests, and accumulated since the last boot; when and with which gesture they occurred is not known. Open: reproduce with a reset counter, one gesture at a time (platter scratch, ring scrub, seek while paused), reading `pcm_underrun1` after each. D6 therefore stays PARTIAL with an unexplained underrun count.

### Underrun counter bisection (image 9, 2026-10-12)

After a reset and loading one track on deck 1 (paused), `pcm_underrun1` already read 384 at 41 s of uptime and stayed 384 over the next 26 s at rest. One gesture at a time, read after each: platter scratch inside the window 384 -> 384; ring scrub in pause 384 -> 384; touch seeks while playing 384 -> 384. `pcm_underrun2` 0, `output_late_count` 0, no UART line in any step. So none of the jog/scratch/seek gestures raised the counter; the underruns occur before the first gesture (at boot or during the track load) and are not caused by scratch. The earlier reading of 108 is explained by the same mechanism (count taken after one boot and one load), not by a jog gesture. The cause of the underruns at load is not identified; earlier boots on images 6 and 7 read 0 at the equivalent point. Image 9 contains the reverted pacing and coalescing changes. Next: rebuild and flash the reverted tree, load one track, read the counter.

## Image 10 (clean tree, jog changes reverted) - underrun check

Image 10: HEAD `4e3aa2b41ebcebdedb7ab9e25715553a6bff9416`, source_dirty false, app SHA-256 `198541f03e1331b7a68ce8e67192b36f2b3ad436cef5c9ddece21b6f97616a45`, ELF `08e56f320fd473fc1046950ba54922ac8a0724e17ede15255372c884511c736e`, wired flash at 0x20000 (hash verified), identity confirmed by `/api/firmware`.

- Reset, no track loaded: `pcm_underrun1` 0, `pcm_underrun2` 0, `output_late_count` 0 (uptime 17 s). After loading one track on deck 1 (paused, "Turn Down The Lights"): `pcm_underrun1` 0, `pcm_underrun2` 0 at 53 s and 61 s.
- On image 9 (MIDI IN pacing + scrub seek coalescing), the same sequence gave `pcm_underrun1` 384 right after the load. So the 384 underruns were introduced by the pacing and/or coalescing changes (not separated: image 8 had pacing only, its counter was not read). This is a reason beyond "no benefit" for having reverted them.
- Not repeated on image 10: the jog/scratch gestures (done on image 9 without effect on the counter).

## D8 underrun counters over 10 minutes, two decks playing (image 10)

Image 10 (HEAD 4e3aa2b4, ELF 08e56f32...). Both decks in loop, both playing ("Turn Down The Lights", "Sado Miso"), crossfader centre. `monitor_p4_reliability.ps1 -ExpectedBoard jc1060 -Mode Observe -DurationSeconds 600 -ExpectedLibraryTracks 22` with expected source and ELF SHA: result pass, 600.0 s, 2397 status samples, 0 HTTP errors, max latency 115 ms (status), 22 tracks, 0 hard failures, 0 investigation triggers. Counter deltas over the run: output_late 0, pcm1 0, pcm2 0, uac_dropped 0, uac_overflow 0, uac_underflow 0 (constant 1838264), service_log_dropped 0; `output_late_max_us` 0; allocation_failures 0, critical_allocation_failures 0. After the run `pcm_underrun1/2` and `output_late_count` still 0, both decks still PLAYING (uptime 829 s). Evidence: `D-audio/d8_observe_600s_result.json`, `..._metadata.json` (raw status.jsonl kept out of git, SHA-256 6c4fe03eb87f8b52eedf785eb9757fa888c98181ba60645351c8aab573fcbac8).

- D8: PASS (API counters over 10 min with two decks).
- D1: the 10-minute two-deck window is now covered by counters; the listening part (no crackle) is still the operator's report for this window: pending.

## Update 2026-10-12 (D1 closed)
- D1: PASS. Operator reports no crackle, stutter or dropout during the 10-minute two-deck D8 window (counters also flat). Not distinguished: MAIN ch1/2 vs cue ch3/4 wiring by ear beyond D2 PFL checks.
- D10: operator did not find LOAD LOCK in the UI. Code places it on the SYSTEM page, box "DECK LOAD & SYNC" (`dj_ui.c:1440`, field `LOAD LOCK: OFF`, tappable). To re-check on the unit.
- D10: BLOCKED. Operator confirms the LOAD LOCK button is absent from the UI of this image; it exists only on fork-ref. The shared-core source has the widget (`dj_ui.c:1440`) but it is not shown on the JC1060 UI, so the toggle cannot be reached by touch. Work item: LOAD LOCK missing from the JC1060 UI (or a different UI is built for this board; to check). Web/controller/peer refusal paths not exercised.

## Group E (started 2026-10-12, image 10, uptime ~4.7 h)
- E1: PARTIAL. SD mounted, 22-track library served from it and played in D1/D8. `sd_metrics` after ~4.7 h: reads 1689, writes 54, transfer_errors 0, read_max 46.5 ms, write_max 211.7 ms, gate_timeouts 0. Read throughput not measured separately.
- E2: PARTIAL. `idle_waits/polls/timeouts/errors` all 0, so the idle workaround never triggered on this image; no hang seen.
- E3: PARTIAL. SD reads and USB audio ran together for 10 min in D8 with `allocation_failures` 0 and no underrun; the internal DMA heap floor is not exposed by `/api/resources`, so the "stays above recorded minimum" part is NOT RUN.
- E4, E5, E6, E7, E9: not run yet (need operator: reboot, file replacement, other sticks).
- E5: PARTIAL. Hot cues set on both decks, RST, same track reloaded: hot cues came back at the same positions (operator). Memory cues: cannot be written on this image; DDJ-400 SHIFT+CALL (`memory_store`/`memory_delete`, notes 0x3D/0x3E) is in `omitted_inputs` with "No current P4 semantic action". Reading memory cues already present in track metadata not checked. "Keyed by media identity" not exercised (see E6).
- E9 reboots: FAIL (gate not met). UART journal boots 24-34 (operator RSTs; boot 34 is one more than the 10 requested, origin unknown): SD mount failed 3/3 (`ESP_ERR_TIMEOUT`, `sdmmc_init_ocr send_op_cond 0x107`, plus `SD_HOST: sd_host_del_sdmmc_controller(353): host controller with slot registered` on each retry) on boots 24, 26, 28, 30, 32, 34 and succeeded on 25, 27, 29, 31, 33: strictly alternating over that run. Earlier boots 1-23 of the journal: failures on 1, 11, 19, 22 and 24-style intermittents. Every failed boot continued on the USB media path (22 tracks), no panic, no reset loop. The 30 min playback part of E9 was not run. "No `sd_host`/Hosted errors" criterion is violated: `SD_HOST` errors present on every failed mount. Hypothesis (not tested): a quick warm RST leaves the card or the SDMMC slot in a state the next init cannot recover from, or the Hosted link init races the SD init. Needs the other developer.

### E9 note for the shared-core owner (SD mount fails after a warm RST)
- Symptom: after an RST (warm reset, board stays powered) the SD mount fails 3/3 with `ESP_ERR_TIMEOUT` on roughly every other boot; `SD_HOST: sd_host_del_sdmmc_controller(353): host controller with slot registered` follows each failed attempt, so the retry cleanup in the BSP also fails. After a cold power cycle the mount works (boot 35, operator unplugged and replugged: 386 SD reads, 0 transfer errors).
- Failed init signature: `sdmmc_init_sd_ssr: sdmmc_send_cmd returned 0x107` on attempt 1, then `sdmmc_init_ocr: send_op_cond (1) returned 0x107` on attempts 2 and 3 (the card no longer answers CMD1/ACMD41 at all).
- Observed pattern: strict alternation on 11 consecutive quick RSTs (boots 24-34 of the UART journal); isolated failures earlier (boots 1, 11, 19, 22). Failed boots keep running on the USB media path.
- Open questions: (1) does the card keep power across a warm RST (SD VDD/IO supply rail, UHS-I 1.8 V signalling state not reset)? (2) does ESP-Hosted/C6 init run concurrently on the SDMMC controller (slot 1) or shared GPIOs? (3) why does `sd_host_del_sdmmc_controller` report the slot still registered, so the retry never starts clean?
- Not tested: waiting longer between RST and mount, slower SD clock, a power-cycled SD VDD in the BSP before mount. Wi-Fi was never started.

### E9 30 min playback (cold boot 35, SD mounted)
- PASS for this part. Monitor Observe 1800 s (12:04-12:34), 7183 status samples, 0 HTTP errors, `output_late`/`pcm1`/`pcm2`/`uac_dropped`/`uac_overflow`/`uac_underflow`/`service_log_dropped` deltas all 0, allocation_failures 0, no hard failure. Two decks playing at the end. SD after 2347 s uptime: reads 1702, writes 95, transfer_errors 0, gate_timeouts 0. No `sd_host`/`sdmmc` or Hosted line in the UART after boot 35. Evidence: `E-storage/e9_observe_1800s_result.json`. Listening not reported for this window.
- E9 overall: FAIL on the "SD mounts across 10 reboots" part (warm RST, see above); PASS on 30 min playback after a cold boot. This also counts as a 30 min soak toward H1 for the counters (no operator listening yet).
- E9 30 min listening: operator reports no crackle and no dropout during the window.

### E7 (2026-10-12)
- FAT32: PASS (the operator's base stick, used for every earlier scenario, 22 tracks).
- exFAT: PASS. UART: `exFAT candidate detected at LBA 0`, `USB media library loaded: 22 tracks`; operator reports everything works (load and play not itemised). First attempts showed no media: the stick had no rekordbox export, then `export.pdb` read as 0 bytes (`pdb: File too short (0 bytes)`) because the host-side copy was incomplete; after a re-copy (225.3 kB) and a clean eject it loaded. The firmware only opens `/usb/.../export.pdb` read-only; no write to the stick found in the code. Not a firmware defect.
- GPT: NOT RUN. The operator could not create a GPT table on Linux Mint (no GParted); skipped by the operator.
- Observation: each stick removal logs `USB enumeration recovery exhausted 8 fast cycles; continuing every 30000 ms` followed by `recovering USB1 reason=1` every 30 s while no stick is present. Not a failure, but noisy at WARN level.

### E4 (2026-10-12, 22-track library instead of ~70)
- Scan: 22 tracks in about 0.4 s after USB detect (UART: exFAT detected 3759.9 s, library loaded 3760.3 s); no UI freeze on fast list scrolling (operator).
- Artwork: shown for tracks that have one (operator). PWV4 colour waveform shown on decks (operator).
- Playlists: **FAIL on device.** The PLAYLISTS list shows, but touching a playlist does not open it: the screen briefly shows then returns to the playlist list (operator). Data side is fine: the firmware parser (`rekordbox_pdb.c`, host build, `export.pdb` of 225,280 bytes) reads 1 playlist, id 1 "souper rz", not a folder, 22 entries for 22 track ids, so the export is valid.
- Not diagnosed: `ui_library_browse_open_selected()` (`ui_library.c:622`) returns false silently if the library generation differs, the playlist key read fails or allocation fails, and the periodic UI refresh (`ui_library.c:2535-2543`) resets the browse to ALL when `library_generation()` changes; none of these paths logs. The generation is bumped by `library_init` and by `library_sort`. Next step needs a log line in these paths (requires a build, so an explicit GO) or the operator's exact gesture (tap vs LOAD button vs controller).
- Row counts: the ~70-track target was not reachable with this library; E4 is PARTIAL.

### Work item: memory cues cannot be written (for the shared-core owner)
- DDJ-400 SHIFT+CALL (`memory_store` note 0x3D, `memory_delete` note 0x3E, decks 1/2) is in `omitted_inputs` of `controllers/pioneer_ddj_400/profile.json` because the firmware has no such action. This is not a profile mistake: a profile only maps MIDI to existing semantic actions.
- Code facts: memory cues are only read from rekordbox metadata (`rekordbox_anlz.c`, `track_meta_cache.c`, UI `MEMORY: n`); `hot_cue_store` stores hot cues only; no code path writes a memory cue (controller, web, touch). The UI MEMORY button only toggles a panel.
- Needed (firmware, not profile): persistent per-media storage like `hot_cue_store`; `deck_core` store/delete actions; declaration in `tools/controller_profile/compile_profile.py`; then map 0x3D/0x3E; show user memory cues beside rekordbox ones. Decision to take with the owner (shared P4 core, rekordbox export compatibility, ms position vs beat grid).
- Status: left open by the operator's decision (option A), no code written.

## Group F (started 2026-10-12, image 10)
- F2: PASS. `/api/status`, `/api/resources`, `/api/library`, `/api/firmware`, `/api/ui-timing`, `/api/diagnostic-log` all 200 over Ethernet (0.003-0.015 s, diagnostic-log 0.83 s); `pajoniiir.local` resolves (200).
- F1: PARTIAL (link up, DHCP lease 192.168.100.83, mDNS resolve; "startup with cable absent" NOT RUN).
- F3: NOT RUN. Needs a packet capture at boot; this host cannot capture without sudo (`dumpcap` restricted to group `wireshark`), and the DJ LINK switch is a persisted setting (`app_settings_get().dj_link`, `dj_link_service.c:285`), currently ON on this unit, so the factory default is not what is running.
- F4: PARTIAL. Operator ran Vynull (Rekordbox mode, device 17, 192.168.100.147) on the same LAN throughout the tests. Vynull log: `peer: Pajoniiir D1 (CDJ) device 3 at 192.168.100.83` and device 4 (D2), its claim sequence completed, and it follows both decks' play-state (NO TRACK, PAUSED, PLAYING) in step with the unit. Whether Link was ON during D7/D8/E9 windows is not recorded; to confirm with the operator.
- DJ LINK was ON during D7, D8 and the E9 30 min window (operator), so those results already include Link with Vynull present.
- F5: PARTIAL (operator, source = Vynull). The list fills with 1123 entries (Vynull reports 28 playlists/folders). Findings: (1) no artwork for remote tracks; (2) tracks do load from the peer but there is no visual indication of the load/download in progress; (3) the track list reloads at the end of the load (operator's impression, scroll/selection position not checked); (4) opening a playlist shows the same defect as on USB (returns to the list). `/api/resources` afterwards: allocation_failures 0, critical 0. UART shows no error line for the browse or the loads.
- F5 links the playlist defect to the shared browse code (`ui_library.c`, nodes/tracks mode) rather than to the stick: it fails with both a USB export and a Link source.

### F6 (2026-10-13 morning, Link source Vynull, image 10)
- Download completes: PARTIAL. Operator: works, but not every time and with no progress indication. "6 AM Mimosa" finally loaded; "Le Matos" did not. First download took 3-4 min (Vynull log 08:11:14 to 08:15:08: dbserver sessions 08:11:14-08:11:54, 08:12:43-08:13:16, 08:13:56-08:14:29, i.e. three sessions of about 30-40 s each).
- Cancel: FAIL. Operator: cancel does not work, and the LOAD buttons freeze. At 08:15:08 Vynull shows both decks (3 and 4) as NO TRACK; the unit's UART shows a new boot with `rst:0x1 (POWERON)`, so the unit was power-cycled around that time (presumably by the operator after the freeze; to confirm). After it, loads completed in about 35-50 s per Vynull session (08:15:15, 08:18:04, 08:21:47); deck 1 is now playing "6 AM Mimosa".
- Playback unaffected: PASS (operator: other deck, no gap, during the downloads). `pcm_underrun1/2` and `output_late_count` read 0 on the current boot (499 s), but the counters of the boot that was reset are lost, so they were not read for the download window.
- Not verified: atomic write / checksum of the cached file, corruption case, the cache path on SD. The UART is silent at WARN level for Link/download events, so no log supports the observations. The SD may have been unmounted at that boot (warm-RST issue, E9), not checked.
- Work items: no download progress shown; cancel ineffective; LOAD buttons stay frozen after a failed/slow load (busy flag not released?); "Le Matos" never loaded; Link/download events invisible at WARN.
- Power cycle at 08:15 confirmed as the operator's (after the frozen LOAD buttons); no crash (`crash_dump.present` false).
- F7: NOT RUN. Vynull cannot act as tempo master and no CDJ is available, so follower phase, handoff and sink latency were not exercised. Local SYNC between the two decks is covered by C2/C3.
- F8: PARTIAL. Operator stopped Vynull while the decks played, then restarted it: "everything seems stable". Read afterwards (uptime 987 s, no reboot since F6): both decks PLAYING (deck 1 remote-origin "6 AM Mimosa" at 221 s, deck 2 local "Kiss Kiss Bang Bang"), `pcm_underrun1/2` and `output_late_count` 0, allocation_failures 0, `crash_dump.present` false, no UART line after boot. Limits: I was not polling during the loss, so no position trace proves "no seek"; the remote track was already downloaded, so it did not depend on the peer to keep playing (the WAIT-on-source-loss path was probably not exercised); `network_sync` stays 0 (no master followed). Recovery of browse/new load after Vynull returned was not itemised.
- F8 (second run, 2026-10-13 08:33-08:36, 1 Hz poll, 106 samples, `F-link/f8_peer_loss_1hz.jsonl`): deck 1 played the remote-origin track "9 PM - Till I Come (Radio Edit)" (already fully downloaded), deck 2 the local "Kiss Kiss Bang Bang"; Vynull stopped at about 08:34:50 and restarted at about 08:35:50. Result: PASS for the playback side. No position jump (max |delta position - delta time| 22 ms deck 1, 23 ms deck 2), both decks PLAYING throughout, `pcm_underrun1/2`, `output_late_count`, allocation_failures and SD transfer_errors all 0, 0 failed polls, no crash. UI (operator): nothing shown on the perform page, "Source unavailable" shown on the library page, decks kept playing. Source loss while a download is still in progress was NOT exercised; the "WAIT" state of the follower path is not reachable without a Link tempo master (F7). Library recovery after Vynull returned: not automatic (operator): the source has to be reopened by hand to reload the library list; playback was unaffected.
