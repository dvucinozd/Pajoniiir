# JC1060 shared-core development target

Status: **software verified; hardware NOT RUN**, reviewed 2026-10-06.
The JC1060P470C_I_W_Y entrypoint shares the authoritative P4 audio, deck,
library, profile, UI and OTA core with JC4880. It uses an explicit component
allow-list and excludes the JC4880 BSP/startup. No second engine tree or S3
firmware is introduced.

Display/touch/Ethernet configuration and DDJ-400/Link modules were adapted from
collaborator [kayrozen](https://github.com/kayrozen), donor
`428b97dd4a175f03d3a172c8db9c4d5ed94195fb`. See
[provenance](../../docs/FORK_IMPROVEMENTS.md),
[E build evidence](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_E_SOFTWARE_20261004.md)
and [L integration](../../docs/validation/FORK_IMPROVEMENTS_PACKAGE_L_SOFTWARE_20261005.md).

## Configuration

- Native 1024x600 display; the previous product presentation is the default.
- Storage root 1 / controller root 0 are explicit board roles, not enumeration order.
- USB MAIN channels 1/2 and cue 3/4 are paced by actual UAC ring consumption.
- PCM5102A is disabled: GPIO50/51/52 overlap Ethernet. ES8311 is disabled for
  USB-only pacing. Do not apply JC4880 external-DAC wiring to this board.
- RMII Ethernet starts DHCP; Link defaults OFF and binds only the Ethernet netif.
- No Wi-Fi Link or local browsable NFS/DBServer library server.
- Recorder, SD experiments and UI preview remain OFF in regular builds.
- Separate committed `dependencies.lock`; IDF 6.0.2 and LVGL 9.5.0 are retained.
- At least 16 MiB flash is required by the partition layout. Application limit
  is `0x380000`, inside the unchanged `0x400000` factory/OTA slot size.

## Reproducible build

From a clean checkout, use a distinct configuration/build directory:

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
idf.py --version # must report ESP-IDF v6.0.2
$repoRoot = git rev-parse --show-toplevel
Set-Location "$repoRoot\firmware\main-deck-jc1060"
idf.py -B build_jc1060 -D SDKCONFIG=build_jc1060/sdkconfig build
git diff --exit-code -- dependencies.lock
```

Never reuse the JC4880 sdkconfig or alter managed dependencies without reviewing
the resulting lock. Shared changes require both board builds and relevant host
and simulator suites. Linux codec/socket/sanitizer checks are separate from
Windows portable suites. See the [checklist](../../docs/STARTUP_CHECKLIST.md).

## OTA and acceptance

Signed project and app descriptor must both be `main-deck-jc1060`; a JC4880
image is rejected. Packaging/channel tools require `-Project main-deck-jc1060`.
The configured pull root `https://ota.pajoniiir.eu/jc1060` is a separate
development channel location, not proof of published/accepted JC1060 artifacts.
Initial partition provisioning requires an identified board's wired install;
app-only OTA cannot change it.

Panel/revision, flash/PSRAM, USB topology, touch/render, Ethernet/SD, DDJ-400
MIDI/LED/SysEx/audio/reconnect, actual CDJ/rekordbox peers, media replacement,
sync phase/latency and OTA/recovery all remain **NOT RUN**. No clean build,
green LOCKED state or donor hardware report closes those gates. Only the named
JC4880/FLX4 configuration in the
[v91 release](../../docs/validation/JC4880_V91_RELEASE_20261005.md) is published.
