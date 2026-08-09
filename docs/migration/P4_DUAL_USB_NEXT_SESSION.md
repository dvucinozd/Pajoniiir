# P4 dual-USB next-session handoff

Saved: **2026-08-09**

This is the operational checkpoint for the next physical bench session. It is
deliberately narrower than the architecture plan and records only facts needed
to resume without reconstructing today's terminal history.

## Repository checkpoint

- Repository: `https://github.com/dvucinozd/Pajoniiir.git`
- Branch: `feat/p4-dual-usb-host`
- Last firmware-changing commit: `fc03034`
  (`fix(p4-usb): raise production control transfer limit`)
- Hardware evidence commit before this handoff: `0ec1bd3`
  (`docs(p4-usb): record initial wired smoke`)
- Required SDK: ESP-IDF v6.0.2
- P4 serial/flash port: `COM15`
- Current feature build directory: `firmware/main-deck-p4/build_flash`
- The tree was clean before this handoff was written.

The commits after `fc03034` are documentation-only. The firmware currently on
the P4 therefore reports `RC2-54-gfc03034`; a later rebuild from the branch head
will have a newer `git describe` string even if the executable logic is
otherwise unchanged.

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

The existing `build_flash` directory is configured with
`sdkconfig.defaults;sdkconfig.p4_local_controller` and
`CONFIG_PAJONIIIR_P4_LOCAL_CONTROLLER=y`. Rebuild or flash it with:

```powershell
Set-Location "$repoRoot\firmware\main-deck-p4"
idf.py -B build_flash build
idf.py -B build_flash -p COM15 flash
idf.py -B build_flash -p COM15 monitor
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

## First test next session

Start with the product feature image that is already flashed:

1. Power the system normally and begin a COM15 capture before connecting the
   controller.
2. Keep the Rekordbox drive directly on USB0.
3. Connect the DDJ-FLX4 directly to USB1, without a hub.
4. Confirm these production-image log facts:
   - USB Host manager reports `peripheral_map=0x03`;
   - storage remains mounted and the 191-track library remains available;
   - `controller_usb` reports `USB-MIDI ready` for VID `0x2B73`, PID `0x0045`;
   - the controller identity reports `parent_port=1 direct_root=1`;
   - `p4_local_ctrl` reports `P4-local controller active ... port=1`.
5. Exercise Browse/Load, PLAY/CUE on both decks, both jogs, pitch, channel
   faders, crossfader, EQ/filter, PFL, pads and Beat FX. Confirm that each action
   changes the authoritative P4 UI/audio state once, with no duplicate S3 event.
6. Confirm transport, PFL, loop, pad-mode, Hot Cue, Beat FX and Smart-control
   LEDs through the direct P4 path.
7. While the storage/library remains usable, disconnect the FLX4. Expect
   `USB-MIDI controller disconnected` followed by
   `P4-local controller disconnected; S3 fallback resumed` and no P4 reset.
8. Reconnect the FLX4 and confirm direct-root identity, control recovery and the
   authoritative LED snapshot.
9. Save the complete log and record every operator-visible or audible result;
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

- USB1-only controller boot and direct FLX4 enumeration;
- boot with both USB devices attached and both insertion orders;
- real P4-local MIDI input and LED output;
- simultaneous storage reads, playback and controller traffic;
- independent USB0/USB1 disconnect and recovery;
- playback/cache-miss stress while operating the controller;
- heap, DMA heap, stack, latency, VBUS and current measurements;
- 30-minute dual-active diagnostic soak and later multi-hour product soak;
- direct P4-to-FLX4 USB Audio at 44.1 and 48 kHz.

Do not merge this branch into `master` or retire the S3 fallback until the
applicable hardware matrix is complete and its logs are archived.
