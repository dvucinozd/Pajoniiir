# JC1060 hardware test plan

Status: **plan only, all scenarios NOT RUN.** Target: `main-deck-jc1060` on the
shared P4 core (`codex/shared-p4-core-m3` lineage), Guition JC1060P470C_I_W_Y,
Pioneer DDJ-400, ESP-IDF 6.0.2, application budget `0x380000`.

Sources: [firmware/main-deck-jc1060/README.md](../../../firmware/main-deck-jc1060/README.md),
[SHARED_P4_BOARDS.md](../../SHARED_P4_BOARDS.md), package E/F/G/I/J/K/L records
in `docs/validation/`, and the hardware lessons of the reference fork
(`Pajoniiir-fork-ref`, v160-v340). Result layout: see [README.md](README.md).

## 0. Constraints and ground rules

- **One physical unit, no spare.** Phases are strictly sequential; stop at the
  first FAIL of a gate and do not start the next group.
- **Known-good recovery image first** (group 0): a fork-ref image that is
  proven on this unit, plus the wired flash procedure, must work before any
  new image is flashed. Boot-mode flashing must be confirmed available.
- Wi-Fi is **disabled in software** on this board (`board_capabilities`:
  `wifi=false`), not absent: the ESP32-C6 is fitted and ESP-Hosted (SDIO) stays
  linked, with the Hosted lifecycle hooks as no-ops. The reason is not stated
  in the upstream docs; the reference fork reproduced a Core 0 panic when
  disabling Wi-Fi (`sd_host_del_sdmmc_controller: host controller with slot
  registered`), consistent with the known ESP-Hosted SDIO-deinit vs shared SD
  host family. Treat Wi-Fi as out of scope for acceptance, but test the
  SD/Hosted coexistence (E9). Every default in the tooling that assumes the AP
  `http://192.168.4.1` is wrong here: use the Ethernet DHCP address or
  `pajoniiir.local`.
- Without Wi-Fi, `boot_network_required` is false, so the pending-OTA readiness
  gate does not require AP/HTTP readiness on this board (see G4).
- GPIO50/51/52 overlap Ethernet: PCM5102A is disabled. MAIN and cue audio go
  over USB UAC (MAIN ch 1/2, cue ch 3/4). Do not apply JC4880 DAC wiring.
- Initial partition provisioning is a wired install; app-only OTA cannot
  change it.
- Never flash an image whose `/api/status` board/project identity you have not
  checked in the built `.json`/ELF (`tools/check_board_build.py`).
- Do not run the firmware build or flash on the user's behalf without an
  explicit GO (user preference).

## 1. Test equipment

| Item | Notes |
| --- | --- |
| JC1060P470C_I_W_Y + 5 V supply | record panel/flash/PSRAM revision (A1) |
| USB-UART adapter on UART0 | console; USB_SERIAL_JTAG/PHY0 stay free for controller |
| DDJ-400 (record firmware) | on USB0 per board role |
| USB stick (FAT32 and exFAT) + powered hub | storage role; stick behind hub is a known topology |
| SD card (record make) | tracks WAV/MP3/FLAC/AIFF, 44.1 and 48 kHz, ~70 tracks |
| Ethernet cable, DHCP LAN | direct to PC or switch |
| Headphones + MAIN monitor (USB UAC out) | cue/MAIN listening |
| Pioneer peer(s): CDJ and/or rekordbox | for Link; `vynull` / `beat-link` as stand-in |
| Multimeter, scope or logic analyzer (optional) | rail/clock checks |
| Linux PC with Docker, Python 3 + `pyserial`, user in `dialout` and `docker` | tooling below; no host ESP-IDF or PowerShell needed |

### Tooling (Linux, Docker-only)

Two execution contexts, never mixed up:

| Context | Used for | How |
| --- | --- | --- |
| Tools container `pajoniiir-tools:idf6.0.2` (ESP-IDF 6.0.2 image + PowerShell 7 + Node) | PowerShell scripts, host tests, Python checks that need `IDF_PATH` | `tools/docker/run-in-tools.sh <cmd>`; build once with `tools/docker/run-in-tools.sh --build`; repo mounted at `/work`, runs as the host user |
| Host | Serial capture and anything touching the board | `tools/hil/hil.sh`, `tools/hil/hil_capture.py` (host `pyserial`); the container exposes no device |

Serial capture (`tools/hil/hil.sh`): `daemon` starts a reconnecting UART0
capture into `~/.cache/pajoniiir-hil/serial_all.log` (override with `HIL_PORT`,
`HIL_BAUD`, `HIL_LOG`); `stop`, `tail N PATTERN`, `status` (capture state, log
size, last version/reset reason, USB/UAC/OTA events) and `serve` (HTTP server
on the build directory, port 8080) complete it. There is no `build`, `ota` or
TCP-console subcommand: builds need an explicit GO and the shared core has no
TCP console (see O8). Copy the relevant log slice into `evidence/<group>/`.

Examples:

```
tools/docker/run-in-tools.sh pwsh tests/run_p4_host_tests.ps1
tools/docker/run-in-tools.sh pwsh tools/check_documentation.ps1
tools/docker/run-in-tools.sh pwsh tools/monitor_p4_reliability.ps1 \
    -ExpectedBoard jc1060 -BaseUrl http://<eth-ip> -Mode Observe
tools/docker/run-in-tools.sh pwsh tools/run_p4_uac_transition_stress.ps1 \
    -BaseUri http://<eth-ip>
tools/hil/hil.sh daemon && tools/hil/hil.sh status
```

The container reaches the board over the host network stack through Docker's
default bridge; if the board's LAN is not routable from the bridge, add
`--network host` to `run-in-tools.sh` for that session and record it.

Other scripts in this tree used by the plan: `tools/validate_p4_dual_usb_log.py`
(B2, feed it a `hil.sh` log slice), `tools/check_board_build.py`,
`tools/check_p4_binary_budget.py`. Software-side checks are green on the
tools image (full `tests/run_p4_host_tests.ps1`, `tools/check_documentation.ps1`);
`monitor_p4_reliability.ps1` and `run_p4_uac_transition_stress.ps1` have never
run against a JC1060 and are first exercised in O5 and D7.

## 2. Groups and scenarios

Each scenario: **PASS** only if all criteria hold and evidence is stored under
`evidence/<group>/`. The "Gate" column maps to the upstream packages.

### 0 Baseline (`00-baseline`)

| ID | Scenario | Pass criteria |
| --- | --- | --- |
| 0.1 | Known-good image boots on the unit | Boot to UI, no panic; image identity recorded |
| 0.2 | Wired flash + readback of the new image | Flash ok, partition table 16 MiB layout intact, NVS preserved; UART boot log saved |
| 0.3 | Image identity | `/api/status` reports board `jc1060`, project `main-deck-jc1060`, source SHA and ELF SHA-256 equal to the build |
| 0.4 | Wrong-board refusal | A JC4880/M3 signed image is rejected before flash (OTA path) |

### O Observability (`O-observability`) - prerequisite for all later groups

The reference fork debugged over UART0 (ttyUSB) at verbose level plus a TCP
console. The shared core defaults to UART0 console at WARN, has no TCP console,
and its structured diagnostics are HTTP (`/api/status`, `/api/resources`,
`/api/ui-timing`, `/api/diagnostic-log` = SD journal). Prove the debug channels
before relying on them.

| ID | Scenario | Pass criteria |
| --- | --- | --- |
| O1 | UART0 console | Boot log (ROM, bootloader, app, panic backtrace) readable on the ttyUSB adapter at 115200 through `tools/hil/hil.sh daemon`, surviving an adapter unplug/replug (reattach marker in the log); record the TX/RX GPIOs actually used on this board; USB_SERIAL_JTAG/PHY0 stay free for the controller |
| O2 | Log level | Build/run with a documented way to raise the level (INFO/DEBUG) for acceptance sessions; WARN default recorded for the regular image |
| O3 | Early-boot crash visibility | Deliberate early fault (or known-bad image) still prints on UART0, including panic and reset reason |
| O4 | Web over Ethernet only | httpd starts with no Wi-Fi netif (code looks up `WIFI_AP_DEF` for the AP address; confirm no failure); `/api/status`, `/api/resources`, `/api/ui-timing` reachable at the DHCP address and `pajoniiir.local` |
| O5 | Tool defaults | From the tools container, `monitor_p4_reliability.ps1 -ExpectedBoard jc1060 -BaseUrl http://<eth-ip>` and `run_p4_uac_transition_stress.ps1 -BaseUri http://<eth-ip>` run against the board (container-to-board routing confirmed, `--network host` if needed); no hidden dependence on 192.168.4.1 |
| O6 | Remote log | `/api/diagnostic-log` streams `/sd/logs/system.log` when an SD card is mounted; behaviour without SD recorded |
| O7 | Persistent journals | `service_log` records survive a reboot and a panic; retained audio-WDT/library-load slots readable |
| O8 | Gap list | If a fork-ref aid is missing (TCP console, `/api/log`, heap probe, one-command build/OTA helper), record it as a work item before continuing; do not add it silently during acceptance. Known gaps at planning time: no TCP console, no `hil.sh ota`/`build`, WARN default log level |

### A Boot, display, touch (`A-boot-display-touch`) - gate E

| ID | Scenario | Pass criteria |
| --- | --- | --- |
| A1 | Revision capture | Panel controller, flash size >= 16 MiB, PSRAM size recorded |
| A2 | Cold boot x 10 | UI up each time, no DSI underrun, watchdog or `alloc FAILED`; boot time recorded |
| A3 | Display geometry | Native 1024x600, no rotation, colour bars / gradients correct, no tearing visible |
| A4 | Touch GT911 | Full-screen grid taps map to the right coordinates; multi-touch not required |
| A5 | UI navigation | Overview, Library, Hot Cues, Settings, screensaver and return; each tab renders; no LVGL stall report |
| A6 | Backlight/PA | PA is low before network/NVS work (no pop at boot) |
| A7 | Reboot persistence | Settings (LOAD LOCK, tempo/jog/cue) survive reboot |

### B USB roles and hot-plug (`B-usb-roles`) - gate E/F

| ID | Scenario | Pass criteria |
| --- | --- | --- |
| B1 | Role mapping | Controller on root 0, storage on root 1 as declared; confirmed physically by swapping connectors (storage on the controller port is not detected as storage) |
| B2 | Dual attach | DDJ-400 (UAC + MIDI) and stick enumerate together; `validate_p4_dual_usb_log.py` clean |
| B3 | Stick behind powered hub | Mounts, reads, no brownout |
| B4 | Hot-plug controller x 20 | Unplug/replug; MIDI + audio recover each time without reboot |
| B5 | Hot-plug storage x 20 | Unplug/replug during idle and during playback from SD; playback survives; remount ok |
| B6 | UsbRecovery monitor | `tools/docker/run-in-tools.sh pwsh tools/monitor_p4_reliability.ps1 -ExpectedBoard jc1060 -BaseUrl http://<eth-ip> -Mode UsbRecovery` completes with accounted outcomes |
| B7 | Both removed and restored | Full chain recovers; no leak of heap (see `/api/resources`) |

### C DDJ-400 controller (`C-ddj400`) - gate D

| ID | Scenario | Pass criteria |
| --- | --- | --- |
| C1 | Profile load | DDJ-400 profile selected automatically; labels in UI match |
| C2 | Deck controls | PLAY/CUE, SYNC, jog (VINYL and CDJ mode), pitch fader, loop, hot cues, pads |
| C3 | Mixer | Channel faders, crossfader, EQ, filter, trim; behaviour matches expectations |
| C4 | PFL/cue | Cue buttons route to cue output; PFL LED follows state |
| C5 | LED feedback | Play/cue/pad LEDs track P4 state (P4 authoritative) |
| C6 | VU / SysEx | VU and any SysEx init as defined by profile |
| C7 | Reconnect | Controller replug during playback: state restored, no stuck notes |
| C8 | Profile persistence | Custom/SD profile survives reboot and corruption fallback |

### D Audio (`D-audio`) - gate F/H

| ID | Scenario | Pass criteria |
| --- | --- | --- |
| D1 | MAIN over USB UAC | Both decks playing, clean MAIN on ch 1/2, no crackle over 10 min |
| D2 | Cue on ch 3/4 | Headphone mix correct, independent of MAIN |
| D3 | Formats | WAV 16/24/32/float, MP3 CBR/VBR, FLAC, AIFF; 44.1 and 48 kHz and high rates; correct duration and pitch |
| D4 | Master Tempo | Both decks MT on at +/- tempo; keylock listening pass; no clicks |
| D5 | Seek / loop / cue | Seek, loop in/out/resize, cue preview, beat jump; no underrun |
| D6 | Scratch | Jog scratch both decks; no underrun or UI freeze |
| D7 | UAC transitions | `run_p4_uac_transition_stress.ps1` with `-BaseUri http://<eth-ip>`: all cycles accounted |
| D8 | Underrun counters | `/api/status` / log show zero unaccounted MAIN deadline misses over D1 |
| D9 | Mixed sample rate | 44.1 kHz deck + 48 kHz deck simultaneously |
| D10 | Load lock | LOAD LOCK ON with deck playing: touch, controller, web and peer LOAD refused, audio uninterrupted; OFF: allowed |

### E Storage (`E-storage`) - gate G

| ID | Scenario | Pass criteria |
| --- | --- | --- |
| E1 | SD mount and read | SD tracks play; read speed adequate |
| E2 | SD idle workaround | Idle then playback resumes; no hang |
| E3 | SD / USB DMA | Concurrent SD and USB audio, internal DMA heap floor stays above the recorded minimum |
| E4 | Library scan | ~70 tracks scanned, playlists/artwork/PWV4 shown; no UI freeze |
| E5 | Hot cues / memory cues | Persist across reboot; keyed by media identity |
| E6 | Media replacement | Replace file under same name: cues not silently rebound |
| E7 | exFAT / FAT32 stick | Both mount, plus GPT |
| E8 | Recorder (optional) | Experiment-only; skip unless built with the flag |
| E9 | SD / ESP-Hosted coexistence | With the C6 present and Hosted linked but Wi-Fi never started: SD mounts, reads and writes across boot, 10 reboots and 30 min playback; no `sd_host`/Hosted errors in the UART log. Records the "shared SD/Hosted behavior" gate left open by package E. Wi-Fi enable/disable itself is NOT tested here and must not be attempted on the only unit without a recovery image (reference-fork panic on disable) |

### F Ethernet and DJ Link (`F-ethernet-link`) - gate I/J/K

| ID | Scenario | Pass criteria |
| --- | --- | --- |
| F1 | RMII bring-up | Link up, DHCP, `pajoniiir.local` resolves; startup succeeds with cable absent |
| F2 | Web UI | Remote controller over Ethernet; status, resources, library endpoints respond |
| F3 | Link OFF default | No Link traffic at boot (packet capture); only Ethernet netif bound |
| F4 | Link ON | Peer discovery, claim, no effect on audio |
| F5 | Rekordbox/CDJ browse | Browse via DBServer, Library bridge populated |
| F6 | Download | Cancellable download to SD cache, atomic and verified; cancel and corruption cases; playback unaffected |
| F7 | Sync / tempo master | Follower phase and handoff audible; record sink latency (uncalibrated upstream) |
| F8 | Peer loss | Source loss: WAIT, no seek, recovers |
| F9 | Heap/stack | `/api/resources` stable with Link on over 30 min |

### G OTA and recovery (`G-ota`) - gate L

| ID | Scenario | Pass criteria |
| --- | --- | --- |
| G1 | Signed local push OTA | Update accepted, boots, identity updated |
| G2 | Pull OTA channel | Only if the JC1060 channel is published; newer-only; TTL; size/SHA check |
| G3 | Interrupted upload | Power/cable loss mid-OTA: previous image still boots |
| G4 | Unconfirmed image rollback | Pending image with no readiness: rolls back within the bounded gate. On this board `boot_network_required` is false (no Wi-Fi), so the gate only checks critical allocation failures: an image whose HTTP/Ethernet service never starts can still be confirmed. Test both a failing image (allocation failure injection or timeout test build) and an image with Ethernet down; record the result and, if confirmed, open a work item to gate on Ethernet/HTTP readiness |
| G5 | Wrong-board image | JC4880/M3 image rejected |
| G6 | Size | Built binary under `0x380000` (`check_p4_binary_budget.py`) |

### H Soak and acceptance (`H-soak`) - gate H/L

| ID | Scenario | Pass criteria |
| --- | --- | --- |
| H1 | 30-min dual-loop timing | Both decks looping; TimingSoak monitor passes |
| H2 | 60-min mixed session | Browse, load, loop, scratch, hot-plug; no panic/WDT |
| H3 | Long soak (>= 4 h, segmented ok) | Interruptions logged honestly, not hidden |
| H4 | Final listening | Operator confirms MAIN and cue on the final image |

## 3. Order and stop conditions

0 -> O -> A -> B -> C -> D -> E -> F -> G -> H. Do not start the next group while a
scenario of the current one is FAIL. Stop immediately and roll back to the
known-good image on: panic or reboot loop, `usb_host_install` NO_MEM, repeated
`alloc FAILED`, DSI underrun, storage corruption, or a brownout.

Early-risk areas learned on the reference fork (check first, fix in shared code):

- `usb_host_install` must still get its internal DMA budget; starting the web
  server or mDNS too early boot-looped the fork once.
- Storage root index and the hub topology were the cause of long USB bring-up
  sessions; the shared core declares roles instead of probing, so B1 is critical.
- Heap-walk polling and diagnostic logging steal USB/audio time: keep the
  regular build for acceptance, use diagnostics only for investigations.
- Slow storage/network/JPEG work must stay out of audio and LVGL draw paths.

## 4. Per-session procedure

1. Build (with explicit GO) or take a signed artifact; run
   `tools/check_board_build.py` and `check_p4_binary_budget.py` through
   `tools/docker/run-in-tools.sh`.
2. Record `git rev-parse HEAD`, dirty flag, ELF SHA-256.
3. Start `tools/hil/hil.sh daemon` before flashing so the whole boot is captured;
   flash (with explicit GO); save the boot log slice.
4. `GET /api/status`, `/api/resources`; save JSON.
5. Run the group's scenarios; save logs under `evidence/<group>/`.
6. `tools/hil/hil.sh status`, then fill `RESULT_TEMPLATE.md`; decide continue /
   stop / rollback.

## 5. Out of scope here

- Wi-Fi on JC1060: disabled by board capability; enabling it needs a separate
  fix of the ESP-Hosted/shared-SD teardown and is not part of this acceptance.
- Reference-fork results are not acceptance evidence for the shared-core image.
- Public channel publication needs separate authorization after H.
- ES8311 / PCM5102A audio paths are not part of acceptance (disabled on this
  board by design).
