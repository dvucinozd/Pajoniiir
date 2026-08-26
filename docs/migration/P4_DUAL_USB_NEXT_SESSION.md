# P4 dual-USB next-session handoff

Saved: **2026-08-12**

Software checkpoint updated: **2026-08-27**

This is the operational checkpoint for the next physical bench session. It is
deliberately narrower than the architecture plan and records only facts needed
to resume without reconstructing today's terminal history.

## Repository checkpoint

- Repository: `https://github.com/dvucinozd/Pajoniiir.git`
- Branch: `feat/p4-dual-usb-host`
- Final firmware-changing checkpoint before the hardware pause: `850ab3f`
- Upstream `esp-usb` recovery fix: branch `codex/p4-hub-recycle-race`, commit
  `cc65dc268f9fb6e89b8b3c6c9e94f5aa1dbb2ccb`; both local USB dependencies are
  pinned to that exact commit
- Required SDK: ESP-IDF v6.0.2
- P4 serial/flash port: `COM15`
- Last diagnostic build directory: `firmware/main-deck-p4/build_diag_audio_wdt_local`
- Generated build directories and signed packages remain ignored.

The 2026-08-27 source checkpoint adds the complete direct FLX4 UAC path and
the selected reusable M3 behavior slices. `build_p4_local_uac` passes under
ESP-IDF 6.0.2; `main-deck-p4.bin` is 2,490,368 bytes, leaves 1,179,648 bytes
in the application partition budget, and has SHA-256
`4111cc92cb6ec1bb659b17e22f1ae88e9e2c9767ba206fdfd4fa18615bd93d9c`.
The complete P4 host suite also passes. This artifact has not been installed or
hardware-accepted yet.

The P4 currently reports `RC2-58-g008cfa4-dirty` from `ota_1`. It is the normal
monitor-enabled image with retained audio/library tracing, built from the
current dirty working tree. Its payload is 2,465,424 bytes, SHA-256
`b1993a41b6a6ac78f4a26e57f9d63691a8fb2d3b205adfe96ee5e89ba8672e6d`.
It is a diagnostic image, not a merge candidate.

## Current blocker and branch status

JP1 alone did not provide downstream VBUS on 2026-08-10. A later unqualified
bench arrangement did enumerate the 191-track stick on USB0 and the FLX4 on
USB1 as `2B73:0045`, proving the dual-root data topology. It did not prove the
power path. On 2026-08-12 controlled playback/load runs produced both a generic
P4 core-MWDT reset and a direct raw `BROWNOUT`, including a brownout about
100 ms after the library transaction had reached retained `done`. Audio
breadcrumbs varied between normal I2S output and snapshot preparation; the
Task-WDT ISR hook never fired. This rules out one deterministic audio-task
deadlock and leaves the unqualified power/VBUS path as the blocker.

Testing may resume only after the common 5 V path and each downstream VBUS are
electrically qualified under startup and sustained dual-deck load, without
backfeeding the P4-side VBUS. The circuit and pre-connection checks are in
[`../validation/P4_DUAL_USB_VBUS_BLOCKER_20260810.md`](../validation/P4_DUAL_USB_VBUS_BLOCKER_20260810.md)
and [`../HARDWARE_WIRING.md`](../HARDWARE_WIRING.md); the runtime record is
[`../validation/P4_DUAL_USB_RUNTIME_SMOKE_20260812.md`](../validation/P4_DUAL_USB_RUNTIME_SMOKE_20260812.md).
Do not merge this branch before the power blocker and remaining matrix pass.

The preferred permanent remediation is one internal 5 V distribution
daughterboard: a fused/eFuse branch powers P4 `VCC5V` at JP1 pin 2, and a
TPS2561-class dual current-limited high-side switch independently powers the
device-side VBUS of USB0 and USB1. Native P4 VBUS must be isolated on both
ports; data, ground and shield stay direct to their original roots. Two cable
interposers or carefully cut native VBUS traces are valid implementations of
the same electrical topology. See `HARDWARE_WIRING.md` for the wiring diagram,
ratings, prohibited arrangements and staged qualification order.

## Current physical state

The P4 was fully wired-flashed through COM15 with the experimental
`main-deck-p4` feature image. Esptool verified every written region. The board
boots the factory application at `0x20000`.

Confirmed during the short smoke:

- ESP32-P4 revision v1.3 boots with the ESP-IDF v6.0.2 bootloader;
- application size is 2,446,160 bytes;
- application SHA-256 is
  `581d6d6033e145245b244dc8664aa9484865af847710ab36b8f9d60280249d73`;
- the P4-local-controller feature overlay is enabled;
- the microSD card is detected as a 29,520 MB SDHC device;
- USB0 loads a Rekordbox library containing 191 tracks;
- the legacy S3 peer remains visible as firmware `RC2`, slot 1, state 3;
- the P4-local controller runtime starts and waits for a USB1 controller;
- observation reached about 98.9 seconds without panic, assertion, watchdog,
  brownout or unexpected reboot.

Confirmed in the 2026-08-12 continuation:

- USB0 storage and direct-root USB1 FLX4 enumerated together;
- FLX4 reached USB-MIDI ready as VID:PID `2B73:0045`;
- an upstream disconnect/recycle race was captured and fixed in the pinned
  `esp-usb` fork, with no repeat of that panic after the fixed OTA;
- one deck ran 33 seconds with clean deadline/underrun counters;
- two decks still produced a confirmed raw brownout after about 6.5 seconds,
  even in the monitor-disabled isolation image and without the FLX4 attached.
- retained audio and library phase journals reproduce across reset and are
  exposed in `/api/status`; an additional playing-deck/load sequence produced
  a generic WDT after a completed load, while the fine repeat produced a direct
  brownout after a completed load;
- experimental ANLZ cache writes are disabled to remove a proven SD-commit
  load spike; the attempted one-second load pacing was removed because it did
  not prevent an isolated reset and only added operator latency.

Full evidence and the non-fatal startup warnings are recorded in
[`../validation/P4_DUAL_USB_INITIAL_WIRED_SMOKE_20260809.md`](../validation/P4_DUAL_USB_INITIAL_WIRED_SMOKE_20260809.md).

## Audio wiring that must remain unchanged

The external PCM5102A MAIN-output DAC is wired as follows:

| PCM5102A | ESP32-P4 JC4880 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| second GND | GND, optional |
| LRCK / WS | GPIO52, JP1 pin 5 |
| DATA / DIN | GPIO51, JP1 pin 7 |
| BCK / BCLK | GPIO50, JP1 pin 9 |
| SCK / MCLK | not connected |

The boot message `monitor PCM I2S transport started: BCLK=32 WS=34 DOUT=35`
describes the separate P4-to-S3 monitor/cue link. Those pins are **not** the
PCM5102A MAIN-output wiring.

## Resume commands

Initialize the required environment and confirm the checkout:

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
idf.py --version
$repoRoot = git rev-parse --show-toplevel
Set-Location $repoRoot
git status -sb
git log -3 --oneline --decorate
```

Build the current feature source with the P4-local overlay and flash the exact
verified build directory with:

```powershell
Set-Location "$repoRoot\firmware\main-deck-p4"
idf.py -B build_p4_local_uac -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.p4_local_controller" build
idf.py -B build_p4_local_uac -p COM15 flash
idf.py -B build_p4_local_uac -p COM15 monitor
```

A new flash is not required merely to continue testing the image that is
already running. Rebuild first only when testing a newer commit or when an exact
new artifact/hash is required.

Only one process may own COM15. If opening the port returns `Access is denied`,
list stale monitor processes before terminating only the process that names
COM15:

```powershell
Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -match 'idf_monitor.*COM15|COM15.*monitor' } |
  Select-Object ProcessId, ParentProcessId, Name, CommandLine
```

## First test after stable power is available

The installed image already has the normal MAIN plus monitor/cue path and the
latest retained tracing. Rebuild and install only if the source changes before
the next session, then:

1. With both protected outputs disabled, verify the interposer continuity,
   VBUS isolation, polarity and absence of shorts. Power the P4 from the common
   regulated 5 V source, then enable and measure each downstream output.
2. Use COM15 when it is physically available; when both root ports are occupied,
   capture `/api/status` and `/api/diagnostic-log` over the `Pajoniiir` Wi-Fi
   service interface instead.
3. Keep the Rekordbox drive on the protected USB0 data interposer.
4. Connect the DDJ-FLX4 to the protected USB1 data interposer, without a hub.
5. Confirm these production-image log facts:
   - USB Host manager reports `peripheral_map=0x03`;
   - storage remains mounted and the 191-track library remains available;
   - `controller_usb` reports `USB-MIDI ready` for VID `0x2B73`, PID `0x0045`;
   - the controller identity reports `parent_port=1 direct_root=1`;
   - `p4_local_ctrl` reports `P4-local controller active ... port=1`.
6. Exercise Browse/Load, PLAY/CUE on both decks, both jogs, pitch, channel
   faders, crossfader, EQ/filter, PFL, pads and Beat FX. Confirm that each action
   changes the authoritative P4 UI/audio state once, with no duplicate S3 event.
7. Confirm transport, PFL, loop, pad-mode, Hot Cue, Beat FX and Smart-control
   LEDs through the direct P4 path, including shifted pad mirrors.
8. Confirm direct UAC MAIN channels 1/2 and headphone channels 3/4, headphone
   gain ramp, 44.1/48 kHz source transitions, Beat Jump pages, jog loop-adjust
   and gapless Censor. Record ring pressure and data-loss counters.
9. While the storage/library remains usable, disconnect the FLX4. Expect
   `USB-MIDI controller disconnected` followed by
   `P4-local controller disconnected; S3 fallback resumed` and no P4 reset.
10. Reconnect the FLX4 and confirm direct-root identity, control recovery and the
   authoritative LED snapshot.
11. Save the complete log and record every operator-visible or audible result;
   do not convert an unobserved row into a pass.

If direct enumeration fails, preserve the full descriptor/USB log before
changing code or cabling. Record whether the FLX4 was powered, which physical
connector was used, connection order and whether USB0 storage stayed online.

## Diagnostic-spike distinction

[`P4_DUAL_USB_HARDWARE_RUNBOOK.md`](P4_DUAL_USB_HARDWARE_RUNBOOK.md) describes
the dedicated `firmware/p4-dual-usb-spike` Phase 1 diagnostic image. Its
`PHASE1 STATUS`, `MSC READY` and `MIDI READY` lines are what
`tools/validate_p4_dual_usb_log.py` parses.

The validator must not be used to reject a `main-deck-p4` production-image log
merely because that image does not emit the spike's `PHASE1 STATUS` grammar.
Use the spike later for the exact topology, reconnect matrix and 30-minute
machine-validated soak; use the currently flashed feature image first for
end-to-end product behavior.

## Gates that remain open

- measured stable 5 V rail and protected, backfeed-free VBUS on both ports;
- boot with both USB devices attached and both insertion orders;
- complete P4-local MIDI input and LED output acceptance;
- simultaneous storage reads, playback and controller traffic;
- independent USB0/USB1 disconnect and recovery;
- playback/cache-miss stress while operating the controller;
- heap, DMA heap, stack, latency, VBUS and current measurements;
- 30-minute dual-active diagnostic soak and later multi-hour product soak;
- physical direct P4-to-FLX4 USB Audio acceptance with 44.1 and 48 kHz engine
  sources, clean reconnect and sustained zero unexpected data-loss alarms.

Direct USB1 enumeration itself is now confirmed, but it remains part of every
repeat matrix because the power delivery is not accepted.

Do not merge this branch into `master` or retire the S3 fallback until the
applicable hardware matrix is complete and its logs are archived.
