# M2.5 development artwork fallback — 2026-10-06

Status: **software verified; candidate installation / physical render NOT RUN**.

## Change and source

The operator supplied `favicon.svg` as the default Pajoniiir artwork. The
[retained SVG](../../firmware/main-deck-p4/components/ui/assets/default_artwork.svg)
is byte-identical to that file, SHA-256
`cc7213894515a6438de36828c99386e7d9eebf6d5bb31bf70ea79c37bfc2661d`.
[Generation tool](../../tools/generate_default_artwork.cjs) uses sharp 0.35.4 /
libvips 8.18.6 to pre-render 40x40 Library and 34x34 deck RGB565 images.
Repeating generation produces identical C output. Generated pixels are committed;
neither sharp nor an SVG renderer is needed to build or run firmware.

The previous product presentation shows the logo for a loaded track without
artwork, including pending/unreadable artwork, and preserves real covers when
available. Empty product decks and folder rows stay distinct. Local and remote
track rows use the same fallback; remote titles reserve the thumbnail space.
The optional preview also uses the logo instead of its former ART placeholder.

Real artwork keeps its existing worker/cache and owned UI copies. A logo does
not consume a cache slot, filesystem request or mutable pixel buffer. Returning
to the same cover after a fallback explicitly restores its image descriptor;
row cache keys cannot latch the logo or retain the preceding cover.

The P4 ELF places both arrays and descriptors in read-only flash: 5,512 pixel
bytes plus 56 descriptor bytes. Eight bounded row-type flags distinguish remote
tracks from navigation rows. No audio task, DMA allocation or playback timeline
is changed.

## Automated and visual evidence

Test source is parent `667dbdf999b0b350a518729146c2dfa64c7ed82f` plus this
change. Both builds identify as `M2.4-109-g667dbdf9-dirty`; these are compilation
evidence, **not signed, installed or published M2.5 release artifacts**.

| Gate | Result |
| --- | --- |
| Full `tests/run_p4_host_tests.ps1` | PASS, exit 0 |
| Actual UI interaction / screenshot gates | PASS: all eight modes, 117 screenshot hashes |
| Native product sizes | PASS: 800x480 and 1024x600 |
| Cover → logo → identical prior cover | PASS for deck and Library paths, with real covers retained on other tracks |
| Unloaded/unavailable and remote track → folder | PASS: no stale track artwork/logo |
| Visual review before baseline update | PASS: product, preview, donor demo and Link views; remote title overlap corrected before final review |
| JC4880 IDF v6.0.2 build | PASS; 2,576,432 bytes, under `0x380000` |
| JC1060 IDF v6.0.2 build | PASS; 2,634,528 bytes, under `0x380000` |
| Independent dependency locks | PASS, unchanged |
| Regular configuration | PASS: preview, recorder, SD idle experiment and both OTA fault flags OFF |
| Artwork regeneration | PASS, byte-identical C output |
| Documentation integrity / whitespace | PASS: 81 Markdown/HTML files, 134 retired paths; `git diff --check` |
| New image OTA / panel / active playback / final soak | NOT RUN |

Binary SHA-256 for the local development builds:

- JC4880: `97292b7bbea7c4cef77d3d53aadda3d220f6a96ec487dd187ab24b4a6d12def0`.
- JC1060: `30fe881f5a340091583d76b8590bf8e5a820bcf9b9e864ac263379bfc0b940e0`.

Initial screenshot comparison failed as expected because the logo changes
pixels. Interaction coverage also caught an empty-deck error: a successfully
read deck summary is not necessarily valid. The final implementation checks
`track.valid`, and both empty-deck assertions and full screenshot gates pass.
The first concurrent JC1060 configure encountered the shared Component Manager
Git cache lock; after the other configure released it, retry and final builds
passed without changing dependencies or deleting the shared cache.

Logs and reviewed PNG/PPM captures remain outside Git under
`D:/Documents/.codex-reviews/Pajoniiir-default-artwork-20261006/` and the ignored
`.cache/ui_simulator/` tree. Generated builds and local configs are not committed.

## Acceptance boundary

A subsequent clean candidate from pushed source
`e09e9ca430c0efa1adb55e0a27cdf84695d12e71` was built with ESP-IDF 6.0.2,
packaged and signature-verified, then installed by signed push OTA:

- Version/project: `M2.4-111-ge09e9ca4` / `main-deck-p4`.
- Application: 2,576,432 bytes; SHA-256
  `d2a98e8c7c454de85c611b62790d6e67393f9ecc4172e3771d4da4618d2f70e6`.
- Signed bundle: 2,576,620 bytes; SHA-256
  `1d81634778700d824ef38b85aa3197006ae268df261c60a1e9b20c993e3ab45c`.
- Both dependency/configuration and clean-source checks passed. Preview,
  recorder, storage/DMA experiments and OTA diagnostic flags remain disabled.
- Exact-source CI run `37384956937` completed successfully.
- OTA acknowledged HTTP 200, rebooting; firmware/API subsequently report the
  candidate in `ota_0`, boot 617, startup READY, DDJ-FLX4 and USB storage present.
- Both decks stayed stopped. Resources report zero allocation failures and
  zero critical allocation failures. Operator confirms the logo is displayed.
  The first observation showed D1 empty. A subsequent operator LOAD puts
  `Red For Love {320}` on D1, READY at 100% and stopped; the operator confirms
  the logo on Overview as well. This also exercises the physical LOAD path.

Matched empty-deck snapshots immediately before/after OTA show internal free
147,027 / 147,111 bytes and DMA free 107,483 / 107,567 bytes. The largest
internal block changes from 69,632 to 51,200 bytes (26.47% smaller). A later
sample retains a 51,200-byte largest block with 146,627 internal free bytes.
This exceeds the planned 10% comparison threshold for those empty-deck
snapshots; zero failed allocations alone does not explain fragmentation.
ELF comparison with the retained exact-v91 build finds unchanged `.iram0.text`,
`.dram0.data` and `.dram0.bss`, and only eight additional `.dram1.bss` bytes.
Logo descriptors are read-only `R` symbols. Those facts bound static cost but
do not establish the cause of the runtime largest-block difference.

A controlled follow-up loads the same key 4 `HAPPY STATION` track on D1,
stopped, matching the saved v91 loaded baseline. Internal free is
105,043 / 105,035 bytes, DMA free 65,499 / 65,491 bytes, and both largest
internal/DMA blocks are unchanged at 31,744 bytes. The eight-byte decrease
matches static BSS growth; PSRAM free increases by 15,224 bytes. This closes
the single-loaded-deck comparison without a material memory regression.
The empty-deck largest-block variation remains recorded, and dual-playback
resource comparison is still required. No speculative allocator change was
introduced.

The first clean build session ended before final linking. Resuming the same
clean build completed successfully with observed exit code zero. Original and
completion logs, signed package, CI evidence, pre/post diagnostic logs and
runtime snapshots are retained at
`D:/Documents/.codex-reviews/Pajoniiir-M25-logo-candidate-20261006/`.

The frozen v91 public OTA channel and tags remain unchanged. The new candidate
still requires its active audio/resource check, and the startup largest-block
variation remains recorded for that review. It does not close the remaining metadata matrix or
JC1060/DDJ-400/real-Link hardware gates.
See [the release plan](../DEVELOPMENT_PLAN.md) and
[candidate checklist](../STARTUP_CHECKLIST.md).
