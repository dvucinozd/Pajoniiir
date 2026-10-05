# JC4880 library smoke - 2026-10-06

Status: **focused checks passed; extended metadata acceptance remains open**.

## Installed identity and scope

- JC4880 / DDJ-FLX4, `M2.4-91-g75136aef`, `ota_1`.
- Frozen source: `75136aef749a1f03d9089c8b6ff6452b3dba0839`.
- Local USB library: generation 3, 260 tracks.
- Both decks remained stopped; this was not an audio test or soak.
- No OTA, reboot, cue edits or source restore was performed.
- The development default-artwork logo is absent from this installed image.

## Observed results

| Check | Result and evidence |
| --- | --- |
| Catalog consistency | PASS: three further API reads match the baseline, with 260 unique track keys and unchanged generation |
| Local load | PASS: D1 key 4 reaches READY and 100% progress; title is `FUN FUN - HAPPY STATION (CLUB MIX) (℗1983 / ©2010)`, artist Fun Fun, duration 436000 ms |
| Stale catalog request | PASS: generation 4 request against generation 3 returns HTTP 409, `Library generation changed`; title, READY state and stopped transport remain unchanged |
| Artwork display | PASS for visibility: operator reports artwork is visible |
| Memory-cue display | PASS for visibility: operator confirms cues were displayed; count, positions and source correspondence were not verified |
| Playlist navigation | PASS for one playlist: operator reports `untitled playlist` and working navigation |
| Allocation health | PASS: end resources report ready startup, zero allocation failures and zero critical allocation failures |

The accepted load has zero deltas for PCM underrun 1/2, output-late,
locked backend reads 1/2, USB packet failures/lost frames, dropped blocks
and overflow frames. Idle USB underflow accounting is not used as playback
acceptance; no playback was requested.

API title correspondence proves catalog-to-deck handoff, not comparison with
the original Rekordbox export. Playlist navigation does not prove exported
track order or nested-folder behavior. Artwork visibility does not prove
stale-image isolation during rapid scrolling.

## Open checks

- Source title/downbeat comparison, including an offset beatgrid.
- Nested playlist hierarchy and exported track order.
- PWV4 source correspondence and missing/corrupt metadata fallback.
- Rapid artwork browsing and source-change isolation.
- Memory-cue count, positions, loops and truncation against source data.
- Source-cue tombstones, explicit restore and physical cross-media isolation.
- Default Pajoniiir logo on a new exact-image candidate.

These observations do not close the full M2.5 release matrix or qualify
JC1060/DDJ-400/Link. The public v91 artifacts and channel are unchanged.

## Retained evidence

Root: `D:/Documents/.codex-reviews/Pajoniiir-library-smoke-20261006/`.
Raw before/after catalog, firmware, status and resources snapshots are retained
there with operator observations. SHA-256:

| File | SHA-256 |
| --- | --- |
| `catalog-consistency.json` | `2b8da3f931139b4b9b886b561f37c8c2b8ad59ac34b3ce4f1f056440f828ef35` |
| `load-result.json` | `1a7f85d407b9c4a430c54e4fd9db597141e6ac8cf975cb0d6c333907d49b1790` |
| `stale-load-result.json` | `7944db9fa02474fac6e284aa80c380d3ecf5fed1c5cb254a387ee52b14fa1723` |
| `operator-observations.json` | `f741797b5777c04647cd7db38a9bc90a4f54455e693b43eaff61fad87841a395` |
| `end-status.json` | `502cbd499ebd80b530ac26ff47b203e5ab6cec6bae68b7f97e83d077f6721810` |
| `end-resources.json` | `33a43f6d54864d4bc22bb5e90bb6d0920b20ed3a4f0d0b9aefc85487df5be34e` |
