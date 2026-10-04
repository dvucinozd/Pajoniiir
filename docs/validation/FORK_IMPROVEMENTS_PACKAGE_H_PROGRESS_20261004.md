# Package H migration progress (H remains open)

Date: 2026-10-04. Branch: `codex/fork-improvements`.
Donor: `428b97dd4a175f03d3a172c8db9c4d5ed94195fb`,
`firmware/main-deck-jc1060/components/ui/dj_ui.c` and its header.
MIT provenance is retained in `components/ui/DJ_UI_NOTICE.md`.
Production M2.4 and the separate APTA branch are unchanged.

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
