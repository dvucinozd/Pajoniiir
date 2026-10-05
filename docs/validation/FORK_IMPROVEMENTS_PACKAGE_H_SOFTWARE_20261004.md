# Package H software closure: previous design and resource gates

> Historical / scenario-specific record, indexed 2026-10-06. Results, hashes
> and pending items below apply to the named images and sessions. They are not
> a current installed-device or public-channel claim. See the
> [v91 release](JC4880_V91_RELEASE_20261005.md) and [current status](../DOCUMENTATION_STATUS.md) for later acceptance;
> no NOT RUN or waived scenario is converted into PASS by this reconciliation.

Date: 2026-10-04. Branch: `codex/fork-improvements`.
Implementation parent: `3a1c53fbac8528dc5bf289196bc7dfe103c4475e`.
Donor: `428b97dd4a175f03d3a172c8db9c4d5ed94195fb`; retained MIT attribution
in `firmware/main-deck-p4/components/ui/DJ_UI_NOTICE.md`.

Status: **SOFTWARE VERIFIED; physical acceptance NOT RUN**.
Later runtime reserve failure also promotes the existing PSRAM-only LVGL
allocator to ordinary builds; historical allocator statements below describe
this original closure, not the corrected candidate policy.
Subsequent [ordinary candidate installation](JC4880_H_CANDIDATE_20261004.md)
restored network/USB and operator-confirmed previous design/controls, but failed
the resource gate. That later record supersedes the installed-state statement
below; this original software closure does not constitute physical acceptance.
Production M2.4, public OTA channels and the separate APTA branch are unchanged.
No OTA or COM operation is performed by this closure. The last confirmed device
image remains recovery `M2.4-7-gb8d9cb7`, not this candidate.
The [H migration history](FORK_IMPROVEMENTS_PACKAGE_H_PROGRESS_20261004.md)
retains the installed H3 Wi-Fi/USB failure and recovery evidence.

## Delivered presentation and ownership

The previous design is the default at native 800x480 and 1024x600. Waveform lanes,
deck information, Library columns/action rail, Hot Cues and Settings use actual
screen geometry; the framebuffer is not scaled. Existing colors, typography,
primary transport and navigation remain the product basis.

- Overview adds bounded owned artwork, accepted musical key, real load/error
  state and load lock. Memory markers reuse the existing mini-waveform buffer.
  Effective local/source hot cues are merged by full persistent identity before
  rendering, so local deletion is not repainted from raw source analysis.
- Library retains the shared owner, controller sorting/loading, playlist path
  and export order, artwork/source/truncation status and real decoder progress.
  Saved navigation survives returning from another screen.
- Hot Cues shows the effective eight-slot bank and a scrollable list of up to
  sixteen memory cues/loops with truncation. DELETE is explicit and does not
  trigger playback. Source restoration requires a held action on the same deck.
- Touch PLAY/CUE and pad press/release use the controller semantic path. Holds
  keep their originating deck and are cancelled on target/tab/screensaver
  changes. Empty tracks display empty slots rather than invented example cues.
- Shared Settings retains the previous groups, brightness/screensaver state and
  a real stopped-only MAIN sink selector. Recorder remains build-gated. The
  Ethernet board shows Link unavailable until package I supplies its runtime;
  unsupported Wi-Fi controls are hidden. No Link service is claimed by H.

The optional dj_ui preview creates only its Overview/Library/Hot Cues objects.
The product creates its own selected objects. Settings and chrome are common;
no hidden legacy Overview/Library/Hot Cues tree is constructed in preview.
The donor demo Settings is omitted in actual runtime, retained only in the
standalone demo. The H4 Settings stash was reviewed and preserved without whole
application: its duplicate donor Settings mirroring is unnecessary with this
shared Settings architecture.

Library state/actions work without table widgets. Leased analysis metadata is
kept alive while used. Product render metadata and artwork buffers are allocated
once in PSRAM; borrowed artwork is copied before another cache operation can
reuse it. JPEG/filesystem work stays in workers, LVGL objects in the LVGL task.
Unchanged metadata/edit versions avoid repeated NVS reads and label allocation.
Audio and USB DMA placement/pacing are unchanged by H.

## Memory and startup evidence

Existing `/api/status` diagnostics preserve old fields and expose internal/DMA
and PSRAM free/minimum/largest-block measurements. New read-only `/api/resources`
uses the same host allow-list and exposes bounded allocation-failure counters,
last failure size/caps/phase, current startup phase and nine task stack minima
with sampling timestamps. No wildcard CORS or mutation is introduced.

The failed-allocation callback does not allocate, print, block or access storage.
Internal failures are classified separately from explicit PSRAM failures.
Pending OTA startup confirmation additionally requires zero critical allocation
failures and the existing saved-enabled AP/HTTP readiness deadline. External
USB/controller presence remains optional for confirmation.

Tasks sample their own stacks; no task handle is retained across teardown.
The helper throttles to one second, and output/decode calls are further divided
by block/iteration counts. Output scan time is included in deadline timing.
Minima persist across task lifetimes within the boot. Physical overhead remains
unmeasured and must be included in exact-image deadline acceptance.
Allocation tracking starts at common P4 startup; the JC1060 wrapper's earlier
Ethernet initialization precedes this hook. Ethernet bring-up diagnostics and
overall heap measurements remain part of its separate hardware gate.

Preview LVGL allocations use the existing PSRAM-only allocator with no internal
fallback, now with owned allocated-byte accounting. Regular libc LVGL usage is
reported as `null` (unknown), not zero; total heap measurements still apply.
Waveform surfaces/artwork are separate from LVGL-owned byte accounting.

Configured critical stack allocations include LVGL 24 KiB, HTTP/output 8 KiB,
storage 16 KiB, USB library/MSC 4 KiB, loader 4 KiB per loaded deck and decode
48 KiB per deck in PSRAM. These are source configuration requirements, not
measured free-stack reserves. Controller size remains the validated runtime
configuration (minimum 4 KiB). Transport/heap overhead requires physical data.

`tools/check_ui_runtime_budget.py baseline.json candidate.json` checks real
captures with keys `board`, full `source_sha`, `scenario`, `status` and
`resources` (the latter two are complete API responses). Scenarios are
`network-usb-idle` and `dual-playback`; the latter requires both decks playing.
Verify image SHA, board, network and both USB devices separately before capture.

The gate requires matching scenarios/boards, zero critical failures, startup
`ready`, and candidate reserves: internal free >=24 KiB, largest >=12 KiB,
DMA free >=8 KiB and largest >=4 KiB. These conservative initial floors cover
an 8 KiB task plus allocator/USB margin; physical data may require higher floors.
Internal free or largest regression greater than 10% fails.
Stack order is output, decode1, decode2, loader1, loader2, LVGL, controller,
storage, HTTP. Required minima: output/HTTP >=1 KiB; others >=512 bytes.
Idle requires output/LVGL/controller/storage/HTTP; dual playback requires all.
Unknown/missing samples fail. This resource gate does not verify audible sound,
USB completion cadence, decode runway or overall hardware acceptance.

Recovery firmware does not expose every new measurement. Its missing largest
block must be measured with an appropriate diagnostic baseline before comparison;
synthetic fixtures and missing values cannot qualify hardware. No runtime baseline
or candidate resource PASS is claimed here.

## Executed software checks

- Full Windows P4 host runner: PASS, 122 compiled suite invocations plus separate
  static/API/controller/signing gates. Includes real resource callback/sampling
  code, allocator byte accounting and runtime-budget threshold/rejection tests.
  Optional operator-file MP3/PDB runs have no input file and remain skipped;
  fixture-based suites execute. No physical export/playback proof is implied.
- Seven simulator presentations: PASS, 95 exact captures. Legacy/product compact
  and wide have 17 each, actual preview compact/wide 13 each, standalone native
  compact/wide 9 each. Product/preview baselines were visually reviewed before
  update; unchanged standalone baselines still pass.
- Interaction assertions: selected tree counts, paired D1/D2 PLAY/CUE/pads,
  hold cancellation, local deletion in waveform pixels, stale restore rejection,
  held source restore, load lock, playlist order/navigation, screensaver return.
  Actual product loading/error percentages and empty decks have dedicated
  captures. Unload clears old main waveform pixels, time and BPM. An unavailable
  source clears Library rows and stale success status; empty decks report EMPTY.
- Five-minute deterministic dual-deck Master Tempo PC soak: PASS; drift 0,
  clicks/clipped 0; mixed peak 18748. It does not measure P4 CPU/I2S deadlines.
- ESP-IDF 6.0.2 local builds: PASS for ordinary and preview on both boards;
  experimental recorder builds are checked separately. Ordinary configurations
  keep preview/recorder/storage experiments off.

| Board | Ordinary bytes | Preview bytes | App budget |
| --- | ---: | ---: | ---: |
| JC4880 | 2,555,280 | 2,571,520 | 3,670,016 (`0x380000`) |
| JC1060 | 2,552,896 | 2,568,832 | 3,670,016 (`0x380000`) |

Board/BSP isolation, embedded project identity and LVGL 9.5.0 checks pass.
Both committed dependency locks are unchanged. Local logs/captures are ignored
under `.cache/h_final_*` and the corresponding `build*/h_final_build.log`.
CI runs clean ordinary/preview/recorder/PSRAM variants and all seven screenshot
gates after push; local incremental builds do not themselves prove clean CI.
Initial closure `d6cd50d573ab06755e993d23e0255bcdf2107569` passed all eight
jobs in [CI run 37219657645](https://github.com/dvucinozd/Pajoniiir/actions/runs/37219657645).
Final implementation `5d2d7fddf230b736f806ccb0e1167ff0d8915427`, including
empty/status regression coverage, passed all eight jobs in
[CI run 37221122271](https://github.com/dvucinozd/Pajoniiir/actions/runs/37221122271):
Linux host/95 screenshot gates, JC4880 ordinary/preview/recorder, and JC1060
ordinary/preview/recorder/PSRAM clean builds, linked-image checks, binary budgets
and unchanged dependency locks. A later evidence-only documentation commit does
not change that tested implementation. Build a fresh immutable candidate from
its chosen final SHA before signing/installing; no installed-image acceptance
is inferred from CI artifacts.

## Remaining physical and release gates

All candidate runtime resource comparisons, touch/render fluidity during dual
playback, MAIN/cue channel isolation/listening, strict audio/USB counters,
media/controller reconnect, signed OTA guarded rollback and exact final-image
180-minute soak are **NOT RUN**. H3's historical failure is not reclassified as
fixed on hardware by these software tests. JC1060/DDJ-400/Link hardware gates
also remain NOT RUN. Packages I-L are outside this H closure.

Next: immutable pushed-source JC4880 ordinary candidate, signed bundle and
configuration/startup/resource review, then the staged physical checks in
the accepted plan. Preserve the recovery bundle at
`D:\Documents\.codex-reviews\Pajoniiir-recovery-M2.4-7-gb8d9cb7`.
Ordinary/preview rollback requires no cue/profile/manifest schema migration;
production OTA channel remains unchanged until configuration-specific acceptance.
