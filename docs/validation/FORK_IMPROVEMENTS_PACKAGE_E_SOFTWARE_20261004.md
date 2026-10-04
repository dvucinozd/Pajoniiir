# Package E: shared P4 core and JC1060 build

Development branch: `codex/fork-improvements`; production M2.4 unchanged.
Firmware implementation: `ad0be38254222bfcd967e9a4ef43db52e44b0b53`.
CI checker fixes: `0e6f36c` and `ecca351` (firmware sources unchanged).
Donor wiring/JD9165 table: kayrozen/Pajoniiir
`428b97dd4a175f03d3a172c8db9c4d5ed94195fb` (MIT notices preserved).

## Implementation

E1 adds immutable board capabilities and explicit USB roles. JC4880 uses storage
root0/controller root1; JC1060 uses storage root1/controller root0, FS PHY0.
E2 adds `main-deck-jc1060`, an explicit shared-component list, JD9165 display
BSP, RMII startup, thin common-main wrapper and separate dependency lock.
Playback, library, profiles and UI sources remain in the existing P4 tree.

GT911, I2C, codec, SD and PCM5102A functions now live in `board_adapter`.
JC4880 retains ST7701/270-degree PPA; JC1060 uses native 1024x600/zero-degree
mapping. RMII uses P4 defaults MDC31/MDIO52/external clock50, PHY address1,
reset51. Network initialization failure allows local startup; no Link protocol
is enabled by Ethernet startup.

JC1060 disables PCM5102A because its pins overlap Ethernet. ES8311 is only a
bring-up monitor; DDJ-400 MAIN routing belongs to F. Both retain internal USB DMA
policy; SD bounce implementation belongs to G. IDF6.0.2, LVGL9.5.0, pinned USB
fixes and `0x380000` budget apply to both targets. Factory/OTA partitions match
the existing 16 MiB layout; configure rejects smaller flash.

App descriptor and signed-manifest validation use the running project identity;
sharing silicon/signing keys never authorizes a different board project.
JC1060 channel/publication/recovery qualification remains L. CI separately builds
each target and checks lock stability, BSP isolation, embedded project, LVGL,
binary budget, resampler and linked USB wrapper. `tools/check_board_build.py`
performs the board checks. The P4 lock changes only its manifest hash; resolved
versions remain unchanged. JC1060 has its own lock/JD9165 dependency.

## Verification and limits

Local checkpoint results (2026-10-04):

| Gate | Result |
| --- | --- |
| Full P4 host runner | PASS |
| Board capabilities, both board configurations | PASS |
| Native/rotated overlay mapping and both wrong-board manifest directions | PASS |
| Existing 800x480 navigation/screenshots | PASS, 11 unchanged images |
| IDF6.0.2 JC4880 build and fresh build directory | PASS, 2,541,824 bytes |
| IDF6.0.2 JC1060 build and fresh build directory | PASS, 2,594,880 bytes |
| BSP/board identity, LVGL pin and binary budget | PASS on both images |
| Lock stability during repeated configure/build | PASS, both locks unchanged |
| Documentation integrity and diff whitespace | PASS |
| Hosted CI for E2 | PASS on `ecca351`, all three jobs |
| Hardware, installed image, signing/publication | NOT RUN |

Both exact local images report `M2.4-39-gad0be38`; embedded project checks pass.
Local SHA-256 (these are not signed/distribution artifacts):

- JC4880: `67ae4fb49c5ef6ef8655ad79d5c49c0c544bb64e05aa6221546812a212c0d5e3`.
- JC1060: `5e8abdfa4f4dcafdfaaeaebc9233f17f38958f5026142fd879b367f991753324`.

The initial CI run compiled the new firmware but failed because the verifier
opened the container-only `/project` lock path on the runner host. The checker
now uses the host build directory, and each board check is in its own CI job.
The intermediate CI run was superseded/cancelled by that job-isolation fix.

[Hosted run 37192059236](https://github.com/dvucinozd/Pajoniiir/actions/runs/37192059236)
is green for `ecca351a170ce73e942ff8a4748dbe6df73217b9`: JC4880, JC1060 and
host/simulator jobs. Both fresh container builds preserve their committed locks
and pass board/project, LVGL, budget, resampler and USB wrap checks. Package E
is **software verified**; hardware acceptance and release remain NOT RUN.

Local builds, fresh build-directory repetitions, full host regression and the
800x480 simulator are recorded at this checkpoint. Fresh build directories
recompile sources; CI tests dependency materialization from a clean checkout.
Native full-screen/edge mapping and invalid/overflow bounds have host checks.
Both products reject the other signed-manifest project in parser tests.
Full descriptor/signature/download/rollback integration remains L.

Legacy UI compiles on JC1060 but widget layout adaptation remains H. Existing
screenshots cover JC4880, not JC1060 presentation parity. Wi-Fi user startup/
toggle is disabled by board capability; shared remote-Wi-Fi dependencies and
the ESP-Hosted SDMMC constructor remain linked. Physical shared-SD/Hosted
behavior must be checked at bring-up.

All hardware gates are **NOT RUN**: board/panel revision, flash/PSRAM, USB
topology, touch/color/rotation/fluidity, SD, PHY/DHCP, MAIN/cue, DDJ-400 MIDI/UAC,
reconnect/reboot and OTA recovery. Runtime heap, stack reserves, DMA latency
and audio deadlines require hardware. No images are signed, installed or
published here. First JC1060 partition installation is wired; app-only OTA
cannot migrate it. Preserve the accepted JC4880 image for rollback.
