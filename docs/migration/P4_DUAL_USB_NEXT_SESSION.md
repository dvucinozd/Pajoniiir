# P4 dual-USB next-session handoff

Saved: **2026-08-12**

Software/hardware checkpoint updated: **2026-09-02**

This is the operational checkpoint for the next physical bench session. It is
deliberately narrower than the architecture plan and records only facts needed
to resume without reconstructing today's terminal history.

## Repository checkpoint

- Repository: `https://github.com/dvucinozd/Pajoniiir.git`
- Branch: `feat/p4-dual-usb-host`
- Current firmware checkpoint: `4ee76a6`
- Upstream `esp-usb` recovery fix: branch `codex/p4-hub-recycle-race`, commit
  `cc65dc268f9fb6e89b8b3c6c9e94f5aa1dbb2ccb`; both local USB dependencies are
  pinned to that exact commit
- Required SDK: ESP-IDF v6.0.2
- P4 serial/flash port: `COM15`
- Last diagnostic build directory: `firmware/main-deck-p4/build_diag_audio_wdt_local`
- Generated build directories and signed packages remain ignored.

The 2026-08-29 checkpoint retains the complete direct FLX4 UAC path and final
active-S3 retirement, then hardens both-root recovery and USB0 MSC ownership.
Root power-off is conditional on the indexed HCD root still being idle;
attach/enumeration suppresses recovery. MSC teardown retires callback ownership
before destruction and one fixed 8 KiB DMA transfer serves the full device
lifetime, with larger FatFs operations split into bounded transactions.

The complete P4 host suite and ESP-IDF v6.0.2 `build_signed` pass. The tested
application is 2,449,552 bytes, leaves 1,744,752 bytes in its 4 MiB OTA slot and
has SHA-256
`a8f377bd4b310338cae545c1f7b07b0da60bdc415569993e4c7c416d912faa1a`.
It was signed as `RC2-106-gfa55e43-dirty`, installed through OTA and is now
running from `ota_0`. The tested source was committed immediately afterward as
`77aa23a` without intervening firmware changes; therefore the runtime version
is not an exact-commit artifact label. Exact evidence is in
[`../validation/P4_DUAL_USB_HOTPLUG_OTA_SMOKE_20260829.md`](../validation/P4_DUAL_USB_HOTPLUG_OTA_SMOKE_20260829.md).

The newer exact-commit checkpoint is `RC2-109-g269036b`, built clean with
ESP-IDF v6.0.2. Its 2,450,656-byte application has SHA-256
`7776f287f9f795abeee36ac648dc518517ec270823928293bf0b04cb334cd9ee`.
The signed bundle booted `ota_0`; USB0 mounted a 100-track Library and USB1
activated FLX4 MIDI/UAC. Idle and dual-deck counters stayed clean, one FLX4
disconnect/reconnect preserved USB0, and post-reconnect dual-deck UAC playback
passed. Exact evidence is in
[`../validation/P4_USB1_FAULT_RECOVERY_OTA_SMOKE_20260901.md`](../validation/P4_USB1_FAULT_RECOVERY_OTA_SMOKE_20260901.md).

The current exact candidate is `RC2-111-g4ee76a6`, running from `ota_1` after a
clean ESP-IDF v6.0.2 build and signed OTA. Its 2,451,680-byte application has
SHA-256
`238808c1c0918fbd132387437c70a711176f2ab43fc0b675902834328aa3c006`.
On 2026-09-02 it completed a strict 1,800-second dual-active real-MP3 soak with
seven controlled seek-to-zero restarts, zero API retries and zero new audio
late, PCM underrun, UAC drop/overflow, USB recovery or disconnect counters.
Exact evidence is in
[`../validation/P4_EXACT_IMAGE_DUAL_DECK_SEEK_SOAK_20260902.md`](../validation/P4_EXACT_IMAGE_DUAL_DECK_SEEK_SOAK_20260902.md).

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

One 2026-08-29 USB0 remove/reinsert cycle and one 2026-09-01 USB1 FLX4
disconnect/reconnect cycle passed, so focused software hotplug work may
continue. Commit `269036b` also bounds each controller transfer/UAC fault to one
teardown epoch and at most one deferred USB1 recovery request; the observed
approximately 100-request/s storm is closed. Release and
enclosure qualification still require the common 5 V path and each downstream
VBUS to be measured under startup and sustained dual-deck load without
backfeeding the P4-side VBUS. The circuit and pre-connection checks are in
[`../validation/P4_DUAL_USB_VBUS_BLOCKER_20260810.md`](../validation/P4_DUAL_USB_VBUS_BLOCKER_20260810.md)
and [`../HARDWARE_WIRING.md`](../HARDWARE_WIRING.md); the runtime record is
[`../validation/P4_DUAL_USB_RUNTIME_SMOKE_20260812.md`](../validation/P4_DUAL_USB_RUNTIME_SMOKE_20260812.md).
The 2026-09-02 exact-image 30-minute dual-active run stayed stable on the
improved supply, but no voltage/current/backfeed/protection measurement was
available, so the electrical blocker remains unchanged.
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

Confirmed in the 2026-08-29 continuation:

- signed OTA booted `RC2-106-gfa55e43-dirty` from `ota_0`;
- USB0 storage mounted and loaded a 100-track Library while USB1 FLX4 activated
  its local MIDI/UAC profile;
- one physical USB0 remove/reinsert cycle completed without reboot or FLX4
  loss, with 2/2 mount success and clean unmount/uninstall diagnostics;
- the remounted Library loaded again and two later MP3 track loads completed;
- host daemon errors, root recovery failures and recovery queue drops remained
  zero;
- the retained coredump shown by status is historical; the current OTA boot was
  `SW` and the hotplug cycle did not create a new panic.

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

GPIO32/GPIO34/GPIO35 belonged to the retired P4-to-S3 monitor/cue link and must
remain disconnected. They are **not** the PCM5102A MAIN-output wiring.

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

Build the current P4-only product defaults and flash the exact
verified build directory with:

```powershell
Set-Location "$repoRoot\firmware\main-deck-p4"
idf.py -B build_signed build
idf.py -B build_signed -p COM15 flash
idf.py -B build_signed -p COM15 monitor
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

The installed image already contains the final S3 retirement, PCM5102A MAIN,
direct FLX4 UAC cue and the accepted USB recovery/MSC fix. Rebuild and install
only if the source changes before the next session, then:

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
   changes the authoritative P4 UI/audio state once.
7. Confirm transport, PFL, loop, pad-mode, Hot Cue, Beat FX and Smart-control
   LEDs through the direct P4 path, including shifted pad mirrors.
8. Confirm direct UAC MAIN channels 1/2 and headphone channels 3/4, headphone
   gain ramp, 44.1/48 kHz source transitions, Beat Jump pages, jog loop-adjust
   and gapless Censor. Record ring pressure and data-loss counters.
9. While the storage/library remains usable, disconnect the FLX4. Expect
   `USB-MIDI controller disconnected` followed by
   `P4-local controller disconnected` and no P4 reset or storage loss.
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
- repeat boot with both USB devices attached and both insertion orders (one
  post-OTA boot with both active passed 2026-08-29);
- complete P4-local MIDI input and LED output acceptance;
- simultaneous storage reads, playback and controller traffic (one exact-image
  30-minute dual-active real-MP3 run passed 2026-09-02);
- repeated independent USB0/USB1 disconnect and recovery (one USB0 replug
  passed 2026-08-29 and one USB1 replug passed 2026-09-01;
  remove-during-decode and the repeated matrix remain);
- playback/cache-miss stress while operating the controller;
- heap, DMA heap, stack, latency, VBUS and current measurements;
- later multi-hour product soak (the bounded 30-minute exact-image diagnostic
  soak passed 2026-09-02);
- physical direct P4-to-FLX4 USB Audio acceptance with 44.1 and 48 kHz engine
  sources, clean reconnect and sustained zero unexpected data-loss alarms.

Direct USB1 enumeration itself is now confirmed, but it remains part of every
repeat matrix because the power delivery is not accepted.

The S3 fallback is already retired in source by product decision. Do not merge
this branch into `master` until the applicable P4-only hardware matrix is
complete and its logs are archived.
