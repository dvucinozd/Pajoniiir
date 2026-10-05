# Package I software closure — 2026-10-05

> Historical / scenario-specific record, indexed 2026-10-06. Results, hashes
> and pending items below apply to the named images and sessions. They are not
> a current installed-device or public-channel claim. See the
> [v91 release](JC4880_V91_RELEASE_20261005.md) and [current status](../DOCUMENTATION_STATUS.md) for later acceptance;
> no NOT RUN or waived scenario is converted into PASS by this reconciliation.

Scope: MIT codecs, JC1060 Ethernet-only discovery/dual-player claim, serialized
DBServer browse and the shared Library presentation bridge. Production M2.4 and
installed JC4880 `M2.4-61-gc4912d5b` are unchanged. This is software verification,
not hardware acceptance, a release candidate or a published OTA image.

## Provenance and checkpoints

Frozen donor: `428b97dd4a175f03d3a172c8db9c4d5ed94195fb` (v323).
Codec MIT attribution remains in `firmware/common/djlink/LICENSE`; application
MIT copyright remains in `firmware/common/dj_link_core/LICENSE.Pajoniiir`.
I1 `0036c089`, I2 `2fd5832a`, I3 `656f982f`, I4a `37d2626f` are pushed checkpoints.
I4a CI [37228742133](https://github.com/dvucinozd/Pajoniiir/actions/runs/37228742133)
passed at exact `37d2626f0162fe56803a60a5842676411964b198`.
The I4b implementation and this record are committed together on
`codex/fork-improvements`; its commit parent is that I4a SHA. Final clean CI
provenance is associated with the pushed I4b commit, not the installed device.

## Final implementation

- Link defaults OFF and persists its NVS switch. Only JC1060 compiles the Link
  codec/core/service. Every UDP/TCP socket requires the actual Ethernet netif;
  TCP also binds the current local IPv4 address. Failed binding never falls back
  to Wi-Fi/default routing. Loss/IP/netmask changes invalidate the source session.
- One coordinator claims two distinct numbers, withdraws both on conflict and
  remains observer if no pair is available. Peer table is bounded at eight.
  Existing I1-I3 tests cover discovery/status/beat/position, all 64 occupancy
  combinations, timeout, reconnect/MAC/IP replacement and malformed input.
- One low-priority worker owns one nonblocking TCP session and one DB request.
  Connect/send/receive/select do not wait. The 128-byte send queue owns its bytes
  and advances after partial sends. Each tick receives at most 1,024 TCP bytes.
  Port-query/DB descriptor replacement cannot reuse stale readiness/callbacks.
  The DB model retains its bounded three-second step timeout and idle reconnect.
- The cache owns at most 2,000 rows in PSRAM, with no internal-memory fallback.
  Partial lists publish received/total progress but no completed rows. Premature
  footer or malformed metadata fails the request. Only completed pages are copied
  to LVGL. No filesystem/network work occurs in a draw callback.
- The worker checks live source and local claim generation before socket delivery
  and publication. Requests also have a separate command ID so rapid menu changes
  cannot publish a previous page, even when the same menu/source is revisited.
  Command identity is checked under the cache mutex before copying any row;
  refused old pages cannot overwrite LVGL's accepted row snapshot.
  Visible pages drive metadata priority; list rows and details share one request.
- The previous product Library gains an explicit source cycle: local USB, each
  CDJ's USB/SD, or rekordbox collection. Unavailable media fails clearly. It shows
  source, menu/breadcrumb, browse progress and truncation; playlists keep source
  order. Navigation stores up to 16 levels, detects repeated nodes and restores
  parent selection. Returning local restores selection/navigation unless the
  local media generation changed. Local USB refresh does not replace a remote
  view. Remote artwork remains empty until J provides verified assets.
- Incoming LOAD routes only from a known sender IP/player to one claimed deck
  with a known source epoch and a three-second TTL. The status socket binds our
  unicast address, so broadcast LOAD is excluded. LVGL processes the bounded
  queue through the same stopped/busy checks used by touch/controller remote
  LOAD. No deck state is changed and no load ACK is sent before verified audio
  admission. In I all remote audio LOADs fail explicitly with
  `AUDIO DOWNLOAD UNAVAILABLE`; J supplies that capability. No own library is
  advertised as browsable and no IP/player/track-derived persistent key exists.

## Verification

All following commands completed with exit code 0 on Windows/WSL:

- Full `tests/run_p4_host_tests.ps1`, GCC appended to PATH. New portable browse
  suite tests owned copies, 2,000-row bounds/truncation, visible detail, malformed
  partial completion, stale source/connection generation, cancel and incoming
  destination/TTL validation. Existing profile/audio/parser/Web/OTA suites pass.
- `bash tests/djlink/run_linux_sanitizers.sh` on Ubuntu WSL as unprivileged user
  `daniel`: nine suites, ASan/UBSan/leak checks. Actual interface-bound UDP/TCP
  tests cover broadcast exclusion on status, absent interface, send queue limits,
  fragmented replies, disconnect, silent-peer timeout and teardown. Existing
  codec/NFS tests remain PASS; NFS test success does not enable downloads in I.
- All eight simulator presentations: legacy, product compact/wide, native
  compact/wide, runtime compact/wide, and new `link-wide`. The Link scenario
  compiles the actual shared Library and exercises source selection, controller
  page/load, playlist order/parent selection, playing-deck lock, unavailable
  audio, delayed old command refusal, local refresh, empty/error/lost source and
  restored local selection.
  Eight new 1024x600 screenshots were visually reviewed before establishing
  `baselines-link-wide.json`. Existing seven screenshot manifests are unchanged.
- Ordinary ESP-IDF **6.0.2** builds and `tools/check_board_build.py` for both
  targets. JC4880: **2,557,216 bytes**; JC1060: **2,581,280 bytes** at local I4b
  validation. Both below `0x380000`; separate locks unchanged. Project/BSP/Link
  isolation, LVGL 9.5.0 and regular-build experiment exclusions PASS. These local
  development builds are not signed/installed candidates.
- `git diff --check`; no generated config, build tree or managed component staged.
  CI now includes the new Link simulator alongside both clean target matrices
  and Linux network sanitizer tests.

Reproduction:

```powershell
$env:Path += ';C:\msys64\ucrt64\bin'
./tests/run_p4_host_tests.ps1
./tests/ui_simulator/run_ui_simulator_e2e.ps1 -Presentation link-wide -KeepArtifacts
wsl -d Ubuntu -- bash -lc 'cd /mnt/c/Users/Daniel/.codex/worktrees/fork-improvements/DDJ-FFL4 && bash tests/djlink/run_linux_sanitizers.sh'
```

Local evidence is in `.cache/i4b-full-host-final.log`, `.cache/i4b-sanitizers-final.log`,
`.cache/i4b-ui-*.log`, `.cache/i4b-link-ui-final.log`, `.cache/i4b-jc4880-final.log`
and `.cache/i4b-jc1060-final.log`; immutable hosted results attach to the pushed SHA.

## Open gates and rollback

| Gate | Result |
| --- | --- |
| Portable codecs/discovery/claim/DB/cache/load routing | PASS |
| Real mock UDP/TCP with sanitizers | PASS |
| Both ordinary builds, locks/budget and UI regression | PASS |
| Actual JC1060 Ethernet/task timing/heap/USB concurrent playback | NOT RUN |
| Actual CDJ model/version and rekordbox interoperability | NOT RUN |
| JC1060/DDJ-400 physical UI/audio/reconnect | NOT RUN |
| JC4880 listening/active resource comparison/final 180-minute soak | NOT RUN; deferred by operator |

Software closure does not waive any physical gate. The missing recovery
largest-block measurement from H also remains open. No COM port, OTA image,
release tag, installed firmware or public channel was changed by I4b.
Software rollback is the previous integration commit; no device rollback is
needed because the installed JC4880 candidate was retained. Link can remain OFF.

Next: J persistent media/file identity and cancellable verified local download,
then K authoritative-clock sync/master and L final compatibility/candidates.
Artwork/ANLZ/audio/cue sharing must use J's confirmed persistent identity;
discovery and browse epochs here are runtime freshness only.
