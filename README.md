# Pajoniiir BL-A1800

Standalone dual-deck DJ system built around a Pioneer DDJ-FLX4, a Seeed
Studio XIAO ESP32S3 control board and a JC4880P443C_I_W ESP32-P4 multimedia
board. It reads Rekordbox media directly and does not require a PC during
performance.

Canonical repository: `https://github.com/dvucinozd/Pajoniiir.git`. The former
`dvucinozd/ESP32-DDJ-FLX4` URL is deprecated and retained only as a GitHub
redirect. The branch inventory audit on 2026-07-26 reduced the repository to
`master`; the later experimental `feat/p4-dual-usb-host` branch is intentionally
retained while its electrical and hardware acceptance gates remain open.

![Pajoniiir](docs/images/122.jpg)

> [!IMPORTANT]
> The ESP-IDF 6.0.2 migration is **merged into `master`**; both targets now
> build only under **ESP-IDF v6.0.2** (the component manifests pin
> `idf: "==6.0.2"`). The release prefix therefore moved from `RC1` to **`RC2`**,
> and the latest clean dual-target release build is **`RC2`** (`56905c89`) from
> 2026-07-30 — see
> [CLEAN_RELEASE_RC2_BUILD.md](docs/validation/CLEAN_RELEASE_RC2_BUILD.md). It
> was signed, packaged and installed successfully through OTA on both boards
> on 2026-08-02. It carries the full
> `fix/release-blockers-and-concurrency` stabilisation set (bounded compressed
> audio cache, paginated Library UI, immutable track sort, recorder safety
> hardening, lossless control queue, ANLZ ownership fixes and more).
>
> RC2 hardware acceptance is now **in progress**. Both targets have complete
> ESP-IDF v6.0.2 boot chains; the P4 microSD regression is fixed, and a focused
> 2026-08-02 smoke passed display/touch/Library, FLX4 MIDI/LED, MAIN/headphone
> audio and real-MP3 playback. Real WAV/FLAC cache testing was not performed:
> the Rekordbox database referenced files that were absent from the USB drive.
> Long-duration, USB recovery and fault-injection rows remain open in
> [ESP_IDF_6_0_2_MIGRATION.md](docs/migration/ESP_IDF_6_0_2_MIGRATION.md), so
> the latest **complete** functional hardware baseline remains
> **`RC1-123-g587cd7a1`** of 2026-07-14. See
> [Documentation Status](docs/DOCUMENTATION_STATUS.md) for the precise boundary.
>
> A later P4-only signed development update installed
> `RC2-51-g050ab43` into `ota_0` on 2026-08-22 and reached `valid`. COM15
> confirmed the 29,520 MB SDHC mount and, after one physical USB reinsert, an
> exFAT library load of 324 tracks. The already-inserted USB medium had first
> exhausted eight automatic enumeration-recovery cycles, so the reboot recovery
> row remains open. The S3 was not updated in that session and continued to
> report `RC2-44-g1923a3b`. See
> [RC2-51 P4 OTA deployment](docs/validation/RC2_51_P4_OTA_DEPLOYMENT_20260822.md).

> [!NOTE]
> On `feat/p4-dual-usb-host`, the P4 now hosts the direct-root FLX4 MIDI and
> four-channel UAC interfaces together with USB0 storage. Software coverage
> includes the fail-closed USB FIFO split, 48→44.1 kHz resampling with bounded
> ring clock correction, reconnect generations, FLX4-only shifted LED mirrors,
> headphone gain ramps, Beat Jump size pages, jog loop-boundary adjustment,
> gapless slip-reverse Censor and rate-limited UAC health alarms. The complete
> P4 host suite and ESP-IDF 6.0.2 feature build pass; this does not clear the
> open 5 V/VBUS brownout blocker or replace the required hardware soak.

## System at a Glance

| Device | Responsibility |
| --- | --- |
| **Pioneer DDJ-FLX4** | Operator surface: transport, jogs, tempo, mixer, pads, cue and LEDs |
| **XIAO ESP32S3** | USB MIDI host, semantic event translator, LED bridge, FLX4 USB-headphone streamer and service OTA AP |
| **ESP32-P4 board** | Authoritative playback/deck state, Rekordbox library, LVGL UI, audio DSP/mixer and MAIN/cue routing |

The S3 normalizes FLX4 input but does not own playback state. The P4 makes all
authoritative deck, mixer, audio-position and LED decisions. Both boards use
the existing `0xA5` UART control link, extended with the `0xA6` bulk/status
layer. The detailed ownership and data flow are documented in
[Architecture](docs/ARCHITECTURE.md).

## Current Capabilities

- Two independent decks with Rekordbox library browsing and MP3, WAV and FLAC
  playback. Compressed audio uses a bounded LRU page cache (8 × 32 KiB per
  deck) instead of whole-file PSRAM allocation. The current WAV subset is
  classic RIFF/WAVE PCM16 mono/stereo.
- FLX4 transport, jog/vinyl scratch, tempo and Master Tempo, mixer/EQ,
  headphone cue, hot cues, loops, beat jump/sync, Pad FX and Beat FX control.
  Beat FX Filter and Echo have recorded hardware acceptance; Flanger and the
  new one-shot Delay are software-tested and deployed, with focused physical
  audio/routing smoke still pending.
- Simultaneous PCM5102A RCA MAIN output and FLX4 USB headphone cue.
- P4-owned FLX4 LED feedback with reconnect and board-reboot resynchronization.
- LVGL Overview, Library (paginated 8-row table with PREV/NEXT), Hot Cues and
  Settings tabs, plus the optional P4 Wi-Fi remote.
- Data-driven controller profiles loaded from SD or installed through the web
  UI; the built-in DDJ-FLX4 map remains the fallback. The web overwrite path is
  software-complete and still has pending hardware-acceptance rows. A
  host-qualified Hercules DJControl Inpulse 500 profile is included; physical
  MIDI/LED/reconnect and USB-audio qualification remains pending.
- Signed dual-slot OTA, validation and rollback on both processors.

Detailed implementation and acceptance status belongs in
[Project Overview](docs/PROJECT_OVERVIEW.md),
[Development Plan](docs/DEVELOPMENT_PLAN.md) and
[Documentation Status](docs/DOCUMENTATION_STATUS.md), rather than in this
repository entry page.

## Interface

The captures are representative; small UI details may be newer in firmware.

| Overview | Library | Settings |
| --- | --- | --- |
| ![Overview screen](docs/images/overview.jpg) | ![Library screen](docs/images/library.jpg) | ![Settings screen](docs/images/settings.jpg) |

The Hot Cues tab is implemented but does not yet have an archived screenshot.

## Repository Layout

```text
controllers/                 Compiled and source controller profiles
firmware/
  control-board-s3/          ESP32-S3 host/translator/audio-bridge firmware
  main-deck-p4/              ESP32-P4 playback/audio/UI firmware
  common/                    Shared firmware components
docs/                        Product, protocol, validation and design records
tests/                       PC-side regression tests
tools/                       Profile compiler, OTA packager and support tools
```

## Build and Test

Required baseline: **ESP-IDF v6.0.2** and its matching Espressif Python and
toolchain environment. Host tests additionally require native GCC/Make and
PowerShell 5.1 (ili noviji) na Windowsima, odnosno standardni shell na Linuxu.

A standard ESP-IDF installation can be initialized on Windows with:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v6.0.2"
. "$env:IDF_PATH\export.ps1"
```

Verify the selected environment before configuring either target:

```powershell
idf.py --version
```

It must report `ESP-IDF v6.0.2`. For the first build after switching from IDF
5.5.4, remove the previous generated configuration and managed components:

```powershell
Remove-Item -Recurse -Force build, managed_components -ErrorAction SilentlyContinue
Remove-Item sdkconfig, sdkconfig.old -ErrorAction SilentlyContinue
```

Build each target from the repository root:

```powershell
cd firmware\control-board-s3
idf.py set-target esp32s3
idf.py build

cd ..\main-deck-p4
idf.py set-target esp32p4
idf.py build
```

Run the host regression suites from the repository root. These are the same two
entry points CI uses, and both run under Windows PowerShell 5.1 and PowerShell 7:

```powershell
.\tests\run_s3_host_tests.ps1
.\tests\run_p4_host_tests.ps1
```

If `gcc` is not already on `PATH`, append msys2 rather than prepending it —
prepending shadows the system `python.exe` with msys2's, which cannot run the
OTA signing suite:

```powershell
$env:Path = "$env:Path;C:\msys64\ucrt64\bin"
```

Run the headless LVGL navigation and exact-framebuffer screenshot gate:

```powershell
.\tests\ui_simulator\run_ui_simulator_e2e.ps1
```

The first run fetches the pinned LVGL source into the ignored `.cache`
directory. The gate covers Overview Deck 1/2 selection, Library, Hot Cues,
Settings, the screensaver and exact Settings restoration. See
[`tests/ui_simulator/README.md`](tests/ui_simulator/README.md) for baseline
review and update instructions. This PC gate does not replace P4 display,
touch or waveform-motion hardware acceptance.

Both default firmware configurations include the FLX4 USB-headphone path.
Build, flashing, signed release packaging and rollback procedures are covered
by [OTA Update](docs/OTA-UPDATE.md). Hardware bring-up and recurring acceptance
checks are in the [Startup Checklist](docs/STARTUP_CHECKLIST.md).

## Documentation

Start with the [complete documentation index](docs/README.md). The primary
operational documents are:

| Topic | Document |
| --- | --- |
| Product status and source-of-truth policy | [Documentation Status](docs/DOCUMENTATION_STATUS.md) |
| Product shape and implemented scope | [Project Overview](docs/PROJECT_OVERVIEW.md) |
| P4/S3 responsibilities and data flow | [Architecture](docs/ARCHITECTURE.md) |
| FLX4 inputs, outputs and acceptance ledger | [DDJ-FLX4 MIDI Map](docs/DDJ_FLX4_MIDI_MAP.md) |
| UART events and bulk/status transport | [Control Link Protocol](docs/CONTROL_LINK_PROTOCOL.md) |
| Wiring, USB and audio connections | [Hardware Wiring](docs/HARDWARE_WIRING.md) |
| Current phases and remaining engineering work | [Development Plan](docs/DEVELOPMENT_PLAN.md) |
| Deferred native folder/APTA library integration | [libapta P4 Integration Plan](docs/LIBAPTA_P4_INTEGRATION_PLAN.md) |
| Open and accepted risks | [Risk Register](docs/RISK_REGISTER.md) |

Controller-profile schema/update guides, OTA records, validation evidence,
historical design decisions and upstream/vendor references are linked from the
documentation index. Dated design records explain intent; they do not override
current firmware or active operational documents.
