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

A read-only device query still reports v91 / `ota_1`; no update was installed.
The frozen v91, its public OTA channel, prior listening/soak evidence and tags
remain unchanged. This change belongs in the next exact M2.5 candidate and
requires its focused panel/artwork and active audio/resource check. It does not
close the remaining metadata matrix or JC1060/DDJ-400/real-Link hardware gates.
See [the release plan](../DEVELOPMENT_PLAN.md) and
[candidate checklist](../STARTUP_CHECKLIST.md).
