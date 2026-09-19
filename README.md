# Pajoniiir BL-A1800

Standalone dual-deck DJ system built around a Pioneer DDJ-FLX4 and a
JC4880P443C_I_W ESP32-P4 multimedia board.

The P4 directly hosts Rekordbox storage on USB0 and DDJ-FLX4 MIDI plus
four-channel USB audio on USB1. It is authoritative for playback, controller
state, LEDs, mixer/DSP, MAIN/cue audio, LVGL UI, Wi-Fi service and OTA. No PC is
required during performance.

Canonical repository: `https://github.com/dvucinozd/Pajoniiir.git`.

![Pajoniiir](docs/images/122.jpg)

> [!IMPORTANT]
> The active release candidate line builds only with ESP-IDF v6.0.2. The exact
> validated firmware and installed hardware candidate is
> `RC2-147-gc21ad86` on `ota_1`.
> Its signed OTA and exact-image checks passed. The earlier
> `RC2-116-g77d723c` completed a targeted three-hour continuous dual-MP3
> limiter/WDT soak with one boot epoch, no watchdog reset, PCM underrun or
> active UAC loss and no observable USB/controller/output failure. Fourteen
> rare output-late warnings were below fault severity and had no downstream
> failure; their analysis does not justify a code change. The current bench
> 5 V/dual-VBUS measurement gate is
> operator-confirmed PASS. The 50-cycle lifecycle matrix is complete with 43
> PASS, seven explicitly waived I/J cycles and 39 accepted physical
> attachment/reconnect actions. I1, I2 and J1 passed on the exact installed
> image; by explicit operator decision I3--I5 and J2--J5 are waived and
> permanently closed rather than reported as passes.
> All five deterministic Group F Library-load removal cycles passed on the
> exact installed image without reboot, partial Library publication, recovery
> mismatch, controller loss or audio fault. The
> deterministic Group G audio-load trigger and all five physical Group G
> cycles passed on the exact image without reboot, controller loss or audio
> fault.
> All five Group H idle FLX4 disconnect/reconnect cycles also passed while
> USB0 remained mounted with a coherent 100-track Library; profile, MIDI, LEDs,
> UAC, dual playback and audible MAIN/cue recovered in every accepted cycle.
> Group K software-reboot and Group L signed OTA-reboot recovery both passed
> 2/2 with both roots occupied and no manual reinsert. The branch remains **not
> release-qualified**. Real MP3/WAV/FLAC load/play/EOF and simultaneous
> MP3+FLAC/MP3+WAV focused cache checks now pass with audible confirmation;
> mixed-format seek/loop/CUE/scratch edges, on-device timing, remaining OTA
> fault paths, combined-load and closed-enclosure gates remain open.

## Current capabilities

- Two independent decks with Rekordbox browsing and bounded MP3/WAV/FLAC cache.
- FLX4 transport, jog/vinyl, tempo and Master Tempo, mixer/EQ, cue, Hot Cues,
  loops, Beat Jump/Sync, Pad FX and Beat FX.
- Simultaneous PCM5102A RCA MAIN and FLX4 USB headphone cue.
- P4-owned FLX4 LED feedback with reconnect resynchronization.
- LVGL Overview, Library, Hot Cues and Settings screens.
- P4 Wi-Fi remote, diagnostic status/log and signed push/pull OTA paths.
- SD/web-installable controller profiles with exact FLX4 built-in fallback.

## Build

Required SDK: **ESP-IDF v6.0.2**.

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
idf.py --version

$repoRoot = git rev-parse --show-toplevel
Set-Location "$repoRoot\firmware\main-deck-p4"
idf.py build
```

Host regressions:

```powershell
Set-Location $repoRoot
.\tests\run_p4_host_tests.ps1
```

Exact UI simulator gate:

```powershell
.\tests\ui_simulator\run_ui_simulator_e2e.ps1
```

Signed isolated build:

```powershell
Set-Location "$repoRoot\firmware\main-deck-p4"
idf.py -B build_signed fullclean
idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig build

Set-Location $repoRoot
.\tools\package_ota_release.ps1
```

Generated build directories, local sdkconfig files, signing keys and release
packages are not committed. `firmware/main-deck-p4/dependencies.lock` is
committed and must remain reproducible.

## Documentation

- [Current status](docs/DOCUMENTATION_STATUS.md)
- [Complete next-session handoff](docs/migration/P4_DUAL_USB_NEXT_SESSION.md)
- [Startup and release checklist](docs/STARTUP_CHECKLIST.md)
- [Development plan](docs/DEVELOPMENT_PLAN.md)
- [Risk register](docs/RISK_REGISTER.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Hardware wiring](docs/HARDWARE_WIRING.md)
- [OTA procedure](docs/OTA-UPDATE.md)
- [FLX4 MIDI map](docs/DDJ_FLX4_MIDI_MAP.md)
- [Latest exact-image three-hour soak](docs/validation/P4_RC2_116_LIMITER_WDT_OTA_SOAK_20260910.md)
- [30-minute exact-image soak](docs/validation/P4_EXACT_IMAGE_DUAL_DECK_SEEK_SOAK_20260902.md)

Superseded plans and checklists are retained only as files prefixed with
`ARCHIVE_` and in Git history. They are not active release instructions.
