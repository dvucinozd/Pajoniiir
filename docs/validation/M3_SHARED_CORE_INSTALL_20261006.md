# M3 shared-core installation, 2026-10-06

## Exact installed image

The ordinary shared-core candidate `M3-dev-g95e6573c3422` is installed on the
identified M3 ESP32-P4 revision 1.3, MAC `80:f1:b2:d3:4c:81`, 16 MiB flash.
Secure boot and flash encryption were disabled. Runtime `/api/firmware` confirms:

- Project `main-deck-m3`, board `m3`, source clean.
- Source `95e6573c3422f458454f1b5d6df6133e85302bb7`.
- ELF SHA-256 `c4b07f8e7543f0e0c08d7d544130fbf37adb1e6f7cfc10f42c0fd7769ff7c267`.
- Image 2,565,136 B, SHA-256 `025a5370d03642d18a747461220ec33d7b0b7bbadc6acaedaaf769a5f8bf9505`.
- Signed bundle 2,565,324 B, SHA-256 `2fd9c99d83b217655b4fe64f558c9b624ffe9470e4c3a504e8df3eda12c9e165`.
- Current slot `ota_0`, image state `valid`, OTA service `idle`, no last error.

The immutable artifact directory is `releases/shared-p4-95e6573c`, retained
locally. The migration-tool fixes made during installation do not rebuild or
replace this firmware candidate. The later tool/documentation commit is distinct
from the installed firmware source above.

## Wired transition and preservation

COM20 was native USB Serial/JTAG (`303A:1001`), rather than USB1 CH340 UART.
Full-flash reads stopped during the historical application's region, including
at 0x24f000. Both streaming and bounded stub reads and a ROM probe failed;
the hardware/driver cause was not established. No write preceded those probes.

The operator explicitly declined another full backup because a recovery image
already existed. The recovery-image mode captured and hashed protected flash,
bootloader, partition table, NVS, PHY and OTA selection. It validated the exact
existing `M3-51-gafb2099` recovery image and matched its device-side digest and
header to the factory slot. Its SHA-256 is
`7fb9c78e4918117e60848b1bf4d34277412b722341dacd41deaeaf2e94c815b7`.
No complete 16 MiB snapshot, unused-slot image or coredump capture is claimed.
Private captures remain ignored under `.cache/m3-migration/20261006-com20-95e6573c`.

Before migration, OTA sequence 4/state VALID selected `ota_1`, version
`M3-51-beta.1-6-g483063f`; ota_0 contained `M3-51-beta.1-5-gccebdb9`.
The selected recovery reference was the matching factory image, not the running
OTA version. The partition table matched the candidate's existing 16 MiB layout.

The new factory image was written and verified with esptool's device-side MD5
and header readback after local appended-SHA/signed-SHA checks. NVS and
bootloader/table compared byte-for-byte with their protected capture. Only after
those checks were the two OTA-selection sectors reset and read back. There was
no full-chip erase and no bootloader, table or NVS write.

Preserved pre-boot NVS SHA-256:
`a399279cfc2a9b703a6e5828b72b5b6e611e6e4a4e2d66de24f470b1a137aa6c`.
Preserved bootloader/table-region SHA-256:
`cc8ec3e442dd3484f4cbfc4a77f8187fa0dc31b97740416a8a27ae2fd892a5cc`.

The automatic serial reset did not produce visible startup. A subsequent ROM
watchdog reset detached native USB but also did not establish startup. The
operator pressed physical RESET and confirmed GUI appeared. This is a manual
first-boot step, not a successful automatic-reset qualification.

## Observed acceptance

| Gate | Result and scope |
| --- | --- |
| Wired factory installation | PASS: written image/device digest, header, preserved NVS and layout |
| First boot | PASS after physical RESET; automatic native-USB reset unresolved |
| GUI, touch/backlight, settings | Operator confirmed all were correct before OTA |
| Wi-Fi/AP/DHCP/HTTP | PASS: `Pajoniiir-M3`, host lease 192.168.4.2, exact factory API identity |
| Signed local OTA | PASS: HTTP 200, reboot, same source/ELF in ota_0, VALID and idle |
| Startup resources | Observed ready, allocation failures 0, critical failures 0 |
| Controller/library | Initial enumeration PASS: FLX4 MIDI In/Out/UAC ready; mounted USB media and operator-confirmed 100-track export |
| MAIN/PFL, MT, seek, CUE, loops | NOT RUN |
| Waveforms/all zooms/artwork/library load | NOT RUN |
| Wrong target | PASS: signed JC4880 bundle rejected HTTP 400 `wrong manifest project`, M3 identity/VALID slot unchanged, target/size counters empty |
| Interrupted upload, startup timeout and physical rollback | NOT RUN on this image |
| Campaign A and 60-minute timing soak | NOT RUN |
| Legacy cue mapping/apply | NOT RUN: requires explicitly selected old export |
| JC4880/JC1060 physical qualification | NOT RUN |

Migration software regressions now execute 19 tests, including the real API's
`state_text` transport field, existing-image capture, unmatched recovery refusal,
changed NVS/recovery refusal and missing/misplaced region rejection. The Python
adapter attaches SPI flash before commands, retains UART baud changes and avoids
baud changes on native USB. The signed firmware itself remains the exact-SHA
software-qualified 95e6573c candidate.

No production release, public OTA channel, release tag or existing asset was
published or modified. Installed/VALID does not constitute full hardware
acceptance. Isolated output-late findings retain the monitoring policy.

After the operator connected all test hardware, a cold POWERON was observed at
service-log boot 318: FLX4 connected at 2066 ms, its profile activated at 2106 ms,
USB media mounted at 2384 ms, the 100-track library loaded at 2536 ms, and Wi-Fi
started at 3514 ms. This single observation does not complete Campaign A.

Live integration exposed two monitor contract gaps in the 95e6573c candidate:
`/api/library` returns rows rather than the old M3 `loaded` counter, and compact
status lacks the MT/loop fields needed to verify a timing workload. The host
monitor now counts rows and preserves strict workload requirements for
TimingSoak; common status adds the authoritative deck-core MT/loop snapshots for
the next candidate. A 95e6573c Observe run captures counters only; no qualified
timing-soak PASS is claimed for it.

The wrong-target upload test's first immediate firmware GET timed out at the
host's five-second deadline. A repeated test saved the rejection before polling
and confirmed the unchanged idle/VALID image within its observation window.
This transient host timeout is retained as an observation, not a board reset or
an established hardware cause.
