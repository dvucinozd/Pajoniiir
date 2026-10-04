# Package H migration progress (H remains open)

Date: 2026-10-04. Branch: `codex/fork-improvements`.
Donor: `428b97dd4a175f03d3a172c8db9c4d5ed94195fb`,
`firmware/main-deck-jc1060/components/ui/dj_ui.c` and its header.
MIT provenance is retained in `components/ui/DJ_UI_NOTICE.md`.
Production M2.4 and the separate APTA branch are unchanged.

## Current checkpoint: H3 hardware regression and recovery

H3 was pushed as `cf282906ff5ffe2bc045659c59e5d6618e46a094`.
All eight clean CI jobs passed in
[run 37207348161](https://github.com/dvucinozd/Pajoniiir/actions/runs/37207348161).
The operator prefers the previous design; keep it as the product basis and
keep dj_ui as an optional preview. Further H4 Settings work is preserved in a
local stash and paused while recovering the device.

A fresh ESP-IDF 6.0.2 build from the pushed SHA produced
`M2.4-49-gcf28290`, 2,596,416 bytes, project `main-deck-p4`.
Image SHA-256: `59f7ba4ddb4dcd3ee44741388ac5e7de56209e34dce050b33b3215e062080da0`.
Bundle and manifest signatures passed against the committed rel-001 public key.
Both decks were empty/idle before the authorized local OTA. Upload returned
`ok:true,rebooting:true`. The operator confirmed the new UI was visible.

**Hardware acceptance FAIL:** Wi-Fi appeared briefly and disappeared; USB0
storage and USB1 FLX4 were not recognized, including after full power cycles.
The operator supplied `I:\\logs\\system.log`; a read-only evidence copy has SHA-256
`472c475e8185774625c29b5a35e8590b088dc8272d34ab93fe12fbcb931c60d1`.
Boot 577 on the previous image had USB mounted, 324 library tracks, FLX4/profile
active and Wi-Fi started (88,995 internal bytes free). Boots 578-585 on H3 show
Wi-Fi startup at about 61,955 bytes free / 31,744 largest block, then
`WIFI_FAILED a0=45064` (`0xB008`, ESP_ERR_HTTPD_TASK). Subsequent attempts have
about 15 KiB internal free / 5,120-byte largest block, LOW_INTERNAL_HEAP and
bounded three-attempt give-up. HTTP needs an 8,192-byte task stack.
No USB mounted/controller-connected events appear in these H3 boots. The log
confirms the HTTP task allocation failure; USB recovery remains unverified.

Small LVGL allocations previously used C malloc and consumed internal heap;
the preview also retains legacy widgets. A custom LVGL PSRAM-only allocator
now avoids that shared pool, including labels/styles and reallocations. It
never falls back to internal memory on external OOM. Kconfig/build checks
require the custom allocator for the preview. Ordinary builds retain their
existing allocator and presentation. This correction is software-checked;
its physical Wi-Fi/USB effect remains NOT RUN until recovery and installation.
Local correction checks PASS: full P4 host runner, new allocator suite (failure
ownership/no fallback/monitoring), unchanged eleven legacy simulator captures,
both ESP-IDF 6.0.2 preview builds and board checks, unchanged dependency locks
and documentation integrity. Images are 2,596,432 and 2,593,904 bytes. The
allocator's archive anchor is verified by successful firmware linking.
Correction `f430b4878c6ac9d71c8be35af53a389147d53eb4` is pushed; all eight jobs
passed in [run 37209618960](https://github.com/dvucinozd/Pajoniiir/actions/runs/37209618960).

The exact prior image `M2.4-7-gb8d9cb7` and rel-001 signatures were independently
verified and copied to `D:\\Documents\\.codex-reviews\\Pajoniiir-recovery-M2.4-7-gb8d9cb7`.
Image SHA-256: `0e9caf717808adbe2be526a10f59ddf768c1119eb2c914ccfe6b4fae15005d6c`.
OTA recovery is unavailable while AP is down. One-time wired recovery was
explicitly authorized for Pajoniiir only. The operator identified COM15;
esptool confirmed ESP32-P4 revision v1.3, native USB Serial/JTAG. Read-back
partition table matches the existing 16 MiB layout. OTA selection records
77/78 were VALID; the selected slot was ota_1 at `0x820000`.
Only that application slot was written with the verified previous image.
Bootloader, partition table, OTA selection and NVS were untouched. esptool
verified the write; a full 2,505,392-byte flash read-back matched the expected
SHA-256 above. Hardware reset was issued; no unrelated COM device was touched.

The operator then reported normal recovery. Read-only HTTP checks confirm
`M2.4-7-gb8d9cb7`, ota_1, OTA service idle/empty error, working AP/API,
USB host ready and USB0 storage mounted (mount result ESP_OK). A later idle
snapshot has 106,175 internal bytes free; this is a recovery snapshot, not a
qualified minimum under playback load. FLX4 was absent in the first snapshot;
after reconnection the operator confirmed working controls. A subsequent API
snapshot confirms FLX4 VID/PID 2B73/0045, MIDI IN/OUT and USB audio present,
`pioneer_ddj_flx4` profile active and USB0 still mounted with ESP_OK.
Recovery of the previous UI, network, storage and FLX4 controls is confirmed.
No MAIN/cue listening or long-soak claim follows from USB audio presence.
Public production, tags and channels are unchanged.

## H3 Hot Cues integration (software checkpoint)

H2 is pushed as `ae2a9b0257629bfcc1fc9b347263cde1f02b5b7a`.
All eight clean CI jobs passed in
[run 37206006959](https://github.com/dvucinozd/Pajoniiir/actions/runs/37206006959).

The actual Hot Cues screen now uses dj_ui. Pad hold/release, target selection,
VINYL/CDJ jog mode and explicit source restore enter the existing deck-core
semantic path. Delete mode is explicit: pressing a pad deletes via the shifted
HOT_CUE action, with no initial cue/seek preview. Target/tab/accepted-track
changes reset delete mode. Restore requires a long hold; short clicks, lost
presses, tab changes and an accepted track-generation change cannot restore the
wrong track. Hold cancellation is per deck so replacing D2 does not release
an unrelated D1 touch hold. No persistent format changes are introduced.

The display shows effective cue/loop counts and memory-cue count/truncation.
Compact bottom controls now move their actual status-container parent, fixing
the prototype's offscreen group. Both layouts retain large native cue pads.
Settings remains legacy in actual firmware. Duplicate legacy allocations,
key metadata, complete Settings binding, final touch/layout polish and default
enablement remain pending. H is not software-closed.

New actual-runtime tests exercise persistent deletion, no seek before delete,
mode reset on target change, explicit restore and stale hold cancellation.
Native tests cover accepted-generation cancellation and verify that an unchanged
frame adds neither rendered waveform columns nor LVGL display flushes. Window
and tone setters avoid redundant invalidation; empty deck clearing is performed
once per transition. These are host/rendering results, not P4 fluidity evidence.

H3 local verification: full P4 host runner and five screenshot gates PASS;
ESP-IDF 6.0.2 preview builds, board isolation and `0x380000` budget checks PASS.
Images: JC4880 2,596,432 bytes; JC1060 2,593,904 bytes. Locks remain unchanged.
Documentation and staged whitespace checks pass before push. Clean H3 CI and
all physical gates remain pending at this checkpoint.

## H2 Library integration (previous checkpoint)

H1 is pushed as `3e4e816016819affec4ce12c8414ea847c1c41a5`.
All eight clean CI jobs passed in
[run 37205163757](https://github.com/dvucinozd/Pajoniiir/actions/runs/37205163757),
including both preview builds, ordinary builds, storage experiments and host gates.

H2 connects actual Library rows, artwork and hierarchical playlists to dj_ui.
The existing owner retains catalog selection, generations, load admission and
worker publication. Shared presentation actions handle touch row selection,
page movement, sort and playlist navigation; no second catalog or loader exists.
Sorting preserves the selected track identity and remains forbidden within
playlist order. Artwork is copied from the bounded worker/cache during update.
Decoder progress is read from the requested deck only while it is loading;
metadata stages do not invent a percentage. Loaded-row marks, playing/ready
status and per-deck load locks come from current state. Opening a folder does
not falsely apply a playing deck's track-load lock.

On compact displays PLAYLISTS/BACK is visible next to the primary LOAD controls.
Folder/page transitions reset page scroll so new first rows are visible. Sorting
and source information remain secondary scroll content. Local USB is explicitly
the only current source; remote browse/download arrives in I/J.

Actual-runtime tests pass on both resolutions, including new touch row/LOAD,
sort-selection preservation, D2 isolation and folder navigation assertions.
Screenshot baselines are reviewed after these changes. The preview remains
default-off. Hot Cues/Settings and legacy allocation removal remain unfinished;
the H1 historical scope below describes the earlier checkpoint, not H2 scope.

H2 local verification: full P4 host runner and all five exact screenshot gates
PASS; both ESP-IDF 6.0.2 preview builds and board/budget checks PASS. Image sizes:
JC4880 2,594,816 bytes; JC1060 2,592,304 bytes. Both dependency locks are unchanged.
Documentation integrity and staged whitespace checks pass before push. H2 clean
container CI is pending; physical gates remain NOT RUN.

## H1 scope

The common component now contains the donor presentation adapted to LVGL 9.5.0,
with native 800x480 and 1024x600 object layouts. The optional
`PAJONIIIR_DJ_UI_PREVIEW=ON` CMake setting enables
`CONFIG_PAJONIIIR_DJ_OVERVIEW`. Ordinary builds keep it disabled.

The actual firmware uses the new Overview only. Its touch events enter the
existing deck-core transport path. CUE/pad hold and release are paired, including
lost presses and tab changes; the Hot Cues prototype latches the pressed deck
even when the target changes. Library, Hot Cues and Settings still use their
existing presentation in actual firmware. Native prototype captures for those
three screens do not prove full runtime integration.

The Overview bridge uses leased immutable metadata during update and owned
RGB565 pixels during draw, the existing waveform ring cache and authoritative
position interpolator. It copies artwork, memory cues and the effective local
cue bank, with persistent-ID edit precedence. Deleted source cues are not burned
into waveform pixels. Unload clears artwork, waveform images and all cue/loop
overlays even when an old leased analysis snapshot is still present. No JPEG
decoding or filesystem work occurs in LVGL draw callbacks.

PWV4 color preview is extracted into a shared helper; the legacy renderer uses
the same code. Feature visibility follows board Ethernet capability and recorder
configuration. Unbound prototype hardware/status fields display UNKNOWN rather
than donor device names or fabricated SD measurements. Firmware version remains
Git-derived. No profile, cue-store, OTA-manifest or partition format changes.

## Verification

- Full P4 host runner: PASS (102 registered functional suites plus the separate
  controller, static/API/signing and lifecycle gates).
- Legacy 800x480 screenshot regression: PASS, all 11 hashes unchanged.
- Native presentation: PASS at both resolutions, nine captures each, including
  empty, error, loading, artwork, cues, screensaver and exact Settings restoration.
- Actual UI runtime: PASS at both resolutions, eleven captures each, including
  controller/library navigation, hierarchical playlists, load lock, PWV4,
  deck-specific PLAY/CUE and screensaver restoration. The three legacy screens
  remain visible as such in these captures.
- New screenshot baselines were visually reviewed before recording; subsequent
  gates use exact hashes. Native unload also checks for stale colored pixels.
- Five-minute deterministic dual-deck Master Tempo regression: PASS, zero drift
  and clipping. This does not measure ESP32-P4 deadlines or audible quality.
- Local ESP-IDF 6.0.2 preview builds for both boards: PASS. JC4880 image:
  2,591,408 bytes; JC1060 image: 2,588,880 bytes. Board isolation, project identity,
  LVGL 9.5.0 and the unchanged `0x380000` app budget checks pass for both.
  Clean container CI results remain pending the push.
- Dependency locks remain unchanged. `git diff --check`: PASS.

The owned waveform surfaces allocate 221,080 bytes per deck at 800x480 and
398,334 bytes per deck at 1024x600 (PSRAM in firmware). Repeating an unchanged
frame adds no rendered columns. Legacy widgets remain allocated during this
migration, so these figures are additional surface storage, not total heap use
or measured hardware memory reserves.

CI builds ordinary, recorder and dj-ui variants on JC4880, and ordinary,
recorder, PSRAM and dj-ui variants on JC1060. The host job runs all five
presentation gates. Preview configuration must be explicitly asserted rather
than inferred from an unused component compiling.

## Remaining H work

1. Bind Library rows, playlists, selection, source/artwork epochs, actual
   load progress and admission errors to the new presentation.
2. Complete Hot Cues edit/delete/restore and jog-mode controls, key metadata,
   memory-cue/truncation indicators and controller/touch semantic parity.
3. Bind real Settings, sink/controller/storage diagnostics, brightness, Wi-Fi
   and experimental recorder behavior; defer unsupported Link controls to I.
4. Polish compact FX/secondary navigation and wide touch sizes; remove duplicate
   legacy presentation allocations only after all four screens pass parity.
5. Exercise complete actual runtime at both resolutions, rapid source changes,
   unload/reload, artwork cancellation, errors, screensaver/wake and load progress;
   visually review replacements before changing their baselines.
6. Enable the new default presentation only after functional parity and both
   clean-build/compatibility gates. H is not software-closed at H1.

## Installation and physical gates

The user authorized OTA when needed on 2026-10-04. Initial read-only probes could
not resolve `pajoniiir.local`; `192.168.4.1/api/status` timed out. No device image
was changed. COM-connected hardware is ignored. A future OTA must use a fresh
build from a pushed source SHA and a verified signed bundle, after checking
actual board/project and idle decks. No production channel is changed.

JC4880 touch/render fluidity under dual playback, audio listening/deadlines,
memory/stack reserves, reconnect, exact-image 180-minute soak and signed OTA
acceptance remain NOT RUN. JC1060/DDJ-400/CDJ hardware gates also remain NOT RUN.
Rollback at this checkpoint is the ordinary build with the preview disabled;
no stored-state migration is required.
