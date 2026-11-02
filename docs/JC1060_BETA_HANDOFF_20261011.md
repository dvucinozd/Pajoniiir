# JC1060 shared P4 beta contributor handoff

Update 2026-11-03: the operator now has a JC1060 and selected DDJ-FLX4 for local
qualification. The [completion candidate](JC1060_COMPLETION_20261103.md) adds
memory-bank editing, ordinary LOAD LOCK and optional Wi-Fi, plus playlist/Link
UI repairs. Its new build/package identities and physical gates are separate
from the published beta2 and historical contributor records below. JC8012 is
deferred; no new-image installation or physical PASS is implied here.

Date: 2026-10-11. Future physical testing owner: contributor Kayrosen
([kayrozen](https://github.com/kayrozen)). The operator has no JC1060 available.
Physical acceptance of the published beta is **NOT RUN**; preparing this handoff
did not contact the contributor or imply that testing had begun.

Update2026-10-19: the published
[shared-p4-beta.2](https://github.com/dvucinozd/Pajoniiir/releases/tag/shared-p4-beta.2)
now provides `jc1060-beta-kit.zip`, signed `main-deck-jc1060.ddjota` and matching
debug ZIP for clean `2f2b05558ccb68238d7228f59e52cd6c7d06e12f`, embedded
`M2.5-246-g2f2b0555`. Use the beta2 signed manifest/BETA-IDENTITY and
[delivery record](validation/SHARED_P4_BETA_2_20261019.md) for its exact hashes.
This supersedes the historical package identities below for the new download,
not their scoped test evidence. Its physical checks remain NOT RUN and no
message has been sent to Kayrosen. The first bounded block and network/storage/
USB MAIN recovery gates below still require the exact new image on hardware.

Update 2026-10-15: the [new signed integration kit](JC1060_CONTRIBUTOR_KIT_20261015.md)
is prepared locally on clean `c0ee8152` with the shared repairs and updated
DDJ-400 loop mappings. Its README, short checklist, recovery material, exact
BIN/ELF, signed inventory and source bundle are verified offline. It has not
been sent, published or installed. Use its separate identity/acceptance record;
the published beta below remains immutable.

Update 2026-10-14: Kayrosen supplied scoped JC1060/DDJ-400 testing on a separate
branch, finally source `4e3aa2b4`. The
[completed contributor review](reviews/2026-10-14-jc1060-contributor-review.md)
reconciles identities, listening and Observe counters, reproduces integration
defects and records failed warm-reset SD/USB MAIN recovery gates. Those results
do not transfer to the published beta or a new canonical candidate. Selective
integration is the next step; the branch is not merged by this review.

## Package and identity

The [shared P4 beta prerelease](https://github.com/dvucinozd/Pajoniiir/releases/tag/shared-p4-beta.1)
provides `jc1060-beta-kit.zip`, containing a signed `main-deck-jc1060.ddjota`,
raw application BIN, signed manifest, board/source/build configuration, partition
and bootloader artifacts, compiled profiles and SHA-256 inventory. Read
`BETA-IDENTITY.json` and `SHA256SUMS.txt` in the kit for the exact source and
image hashes; verify them before flashing. The matching ELF/map debug archive
is published separately for crash analysis.

The kit is an ordinary JC1060 configuration, not a recorder/alternate-UI/PSRAM
DMA experiment. Its firmware project must be `main-deck-jc1060` and its board
must be `jc1060`. An image or signed bundle for JC4880/M3 is not interchangeable.
Historical and final local software results are in the
[three-target matrix](validation/SHARED_P4_FINAL_BUILD_MATRIX_20261011.md);
new beta packaging provenance is in the
[beta record](validation/SHARED_P4_BETA_1_20261011.md).

## Hardware and installation

- Display: 1024 x 600, using the JC1060 BSP and its touch configuration.
- Controller: board USB0 / root0; Rekordbox storage: board USB1 / root1.
- MAIN and headphones: controller USB audio. Do not enable PCM5102A GPIO50/51/52
  or the ES8311 pacing path; the pins conflict with Ethernet.
- Network: board Ethernet provider. JC1060 has no Wi-Fi capability. Pro DJ Link
  is compiled but runtime default OFF; network interoperability is a separate gate.
- Refer to [board policies](SHARED_P4_BOARDS.md),
  [hardware wiring](HARDWARE_WIRING.md) and [OTA procedure](OTA-UPDATE.md).

For an already provisioned compatible layout and signed OTA runtime, upload
only `main-deck-jc1060.ddjota` through the local web update UI, with stopped
decks and stable power. Record startup and the exact accepted slot/identity.
For a first installation, confirm the actual flash size, bootloader and partition
layout before using wired ESP-IDF flashing. The package includes the build's
`flasher_args.json`; do not guess offsets or write a different board's table.
Application-only OTA cannot migrate the partition table. Preserve settings and
recovery artifacts as appropriate; do not perform a full-chip erase.

## Bounded first physical block

Use FLX4 first as the reference controller; qualify any other controller
separately. Record the controller VID/PID/profile, USB topology and source/ELF.

1. Cold boot: GUI geometry/colors, touch/backlight, storage/library count and
   Ethernet/web readiness. Confirm the exact board/project/source/version and
   image state through `/api/firmware` and `/api/status` when reachable.
2. With stopped decks, verify Browse and LOAD routing to both decks, LED state
   and screensaver first-action wake. A first PLAY consumed by wake is expected.
3. Run one short dual-deck block with a 44.1-kHz and a 48-kHz track, opposite
   pitch, both MT, loops and zoom 8. Exercise jog/Browse, PFL D1/D2 and headphone
   Mix/Volume; observe MAIN/cue and both waveforms. Include seek/loop exit and
   CUE-to-PLAY on each deck, then stop both.
4. Compare before/after counters: no new ISO packet/frame loss, PCM underrun,
   UAC drop/overflow, reset/WDT or stale deck/controller/library state. A missing
   counter is unavailable, not zero. Keep output-late separate and record its
   count, maximum and clusters alongside operator observations.

Avoid `/api/resources` heap walks and large diagnostic logs during playback;
these have interfered with USB audio timing on the M3 bench. Capture crash/bus
trace and large logs only after stopping. Do not reset away a first-fault capture.

Return a dated validation record with hashes, firmware/status snapshots,
before/after deltas, performed controls, physical audio/visual confirmation and
explicit NOT RUN items. Stop on a hard failure and retain evidence; a clean
short block is not long qualification. Subsequent reconnect/insertion-order,
long timing, Ethernet/Link and wider controller coverage remain separate.

## Operator dispositions and release boundary

JC4880's long soak was explicitly skipped by the operator on 2026-10-11;
that waiver is NOT RUN, not PASS and not a waiver for JC1060. S2 testing remains
operator-closed. No existing physical result transfers to a new beta image.
The beta is a GitHub prerelease, not a replacement for stable M2.5 or a public
OTA-channel update. Publishing it does not establish full shared-core acceptance.
