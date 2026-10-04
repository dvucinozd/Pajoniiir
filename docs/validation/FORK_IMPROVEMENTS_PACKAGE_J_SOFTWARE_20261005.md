# Package J software closure — 2026-10-05

Scope: cancellable JC1060 Ethernet NFS download, verified SD cache, full-identity
assets and common LOAD integration. Parent integration SHA is
`f3d4e1aa21de5183b2b57739d1b021422542115d` (I closure). This record and J code
are committed together on `codex/fork-improvements`; clean hosted results attach
to that pushed revision. Production M2.4 and installed JC4880
`M2.4-61-gc4912d5b` are unchanged. No device, COM port, OTA or public channel is
accessed by this package. Software verification is separate from acceptance.

## Provenance and compatibility

Frozen donor: `428b97dd4a175f03d3a172c8db9c4d5ed94195fb` (v323).
NFS codec retains `firmware/common/djlink/LICENSE` (MIT); narrow PDB walker and
DB analysis writers retain `firmware/common/dj_link_core/LICENSE.Pajoniiir`.
The cache/identity/admission/lifecycle code is the common-core adaptation,
not donor IP/player/track FNV storage. No duplicate library/audio engine,
S3 transport, APTA code or recorder enablement is introduced.

The donor PDB fixture originally put the title at index 18. It now uses 17
and a deliberately different `X` at 18, while retaining the 29-track parser
cross-check. The writer tests use this project's memory-cue source ordering,
16-entry truncation and eight hot slots, not donor-only sort/dedup helpers.
A donor null-output preview crash was fixed. Long file paths fail rather than
silently selecting a truncated filename; DB paths preserve valid surrogate pairs
and reject malformed UTF-16. Over-capacity DB blobs are rejected whole.
Existing S3CP v2/v3/v4, FLX4, local USB load and Web/OTA tests remain applicable.

## Implementation and ownership

- The existing single-flight UI load worker owns downloads. Touch, controller
  and incoming LOAD use the same lock/busy admission and request generation.
  No deck reset occurs before the full local artifact is prepared and admission
  is rechecked. Cancel, failed transfer, unavailable media, space rejection and
  PLAY/load-lock races preserve the previous deck. The incoming reference is
  copied from the incoming request, independently of the visible selected row.
- Every NFS UDP socket binds both the actual JC1060 Ethernet interface and its
  local IPv4 address. There is no Wi-Fi/default-route fallback. The frozen codec
  performs fresh portmapper/MOUNT/LOOKUP/GETATTR/READ with 1 KiB reads and a
  four-block window. Unexpected peer/port or oversized datagrams are excluded.
  Each READ must retain LOOKUP type/size/fsid/fileid/mtime/ctime; a changed file
  is rejected before its bytes are delivered. Request/source epoch, peer and SD
  availability are checked throughout. A source change cancels old work.
- CDJ USB/SD uses a freshly downloaded PDB (maximum 64 MiB), bounded page walk
  and full track path. Rekordbox uses the sole serialized DBServer path session;
  no second browser/client is opened. Unsupported extensions fail clearly.
  Only MP3/WAV/FLAC enter the existing audio decoder. PDB size/mtime comparison
  remains an unused donor optimization hint, never identity authorization.
- Full 32-byte identity derives through the existing SHA-256 media mechanism
  from export/session proof, complete audio SHA, normalized path, size and full
  timestamp. IP, player number and numeric Rekordbox ID are locators only.
  The 32-bit runtime UI lookup key never replaces persistent cue/cache identity.
- NFS attributes and PDB bytes cannot prove a physical volume UUID. Therefore
  **each current mount/load adds a fresh random session nonce and re-downloads**,
  even after reconnect or reboot. No cross-session hit or cue/artwork inheritance
  is permitted. This is the plan's conservative session-local fallback. Its
  explicit limitation is that local cue edits cannot migrate between these
  unidentified sessions. Durable reuse needs a future trustworthy media/export
  identity; identical size/mtime/PDB or locator fields cannot supply it.
- `/sd/djlcache-v1` owns a maximum 1 GiB including manifests; audio limit is
  1 GiB, PDB 64 MiB, and admission requires file size plus 64 MiB free reserve.
  Directory scans allocate no directory-sized list. Both deck full IDs and the
  active audio/asset group protect pruning. Single-writer `download.part` is
  recovered before each transaction. No active or deck artifact is evicted.
- Each artifact must match expected length, flush/fsync/close successfully, and
  match a SHA computed by reading back the SD file. The stable 112-byte manifest
  stores version/completion, full ID, size, data SHA and record SHA. Publication
  renames data first and the completion manifest last. Orphan data, `.part`,
  bad manifests, short/extra/corrupt files or failed writes never hit. A hit
  hashes the entire file; existence and size alone are insufficient.
- DOWNLOAD reserves the shared SD activity admission for the entire operation,
  excluding REC. Reads/writes yield the SD gate at 4 KiB boundaries. Handle
  cleanup remains serialized after cancellation. Optional asset writers retire
  a failed predecessor before opening another asset. Network work/JPEG/parsing
  stays in the worker; no output-task filesystem operation or per-block allocation
  is added. Real fsync/card latency still requires measurement.
- Audio, DAT/EXT and artwork share the accepted full identity. Missing/corrupt
  metadata never blocks valid audio. Source DAT/EXT is bounded to 1 MiB each;
  DB waveform/grid/cues/PWV4 are separately bounded. PPTH must refer to the
  accepted source or verified local path. Borrowed in-memory ANLZ streams allow
  parser work after bounded SD reads, without holding the SD gate through parse.
  JPEG is limited to 64 KiB and decoded to an owned thumbnail. LVGL publication
  copies it into the bounded 24-slot cache; remote lookup compares the full ID
  and cannot fall back to a colliding local runtime key.
- Download progress crosses to LVGL through atomics. Result metadata/artwork
  is owned until common audio/session publication or stale/error retirement.
  The decoder uses the SD gate for `/sd` paths independently of USB availability.
  Local USB removal stops only USB sessions; SD loads/output and completed
  playback remain intact. Network loss after a completed load does not require
  the peer to stay online.

## Verification

Local commands completed with exit code 0:

- Full `tests/run_p4_host_tests.ps1`, including added cache/PDB/analysis suites,
  all existing profile/parser/audio/OTA/Web gates, and the production firmware
  lifecycle harness. That harness executes the actual selected-stop and source
  gate functions, checking that USB removal preserves SD loaded and loading
  sessions/output while USB sessions are cleared.
  The artwork harness executes production slot/publication/lookup functions,
  proving deep-copy ownership, different full IDs with the same runtime key,
  and independence from local USB catalog refresh.
- Ubuntu WSL `tests/djlink/run_linux_sanitizers.sh`: twelve suites with ASan,
  UBSan and leak detection. Actual UDP NFS mocks exercise changed READ attributes,
  cancellation, verified filesystem transactions and A/B replacement at the
  same endpoint/path/size/timestamp. Existing real TCP browse/disconnect/timeout
  and codec tests remain green. Cache tests additionally cover full-file SHA,
  wrong ID/checksum, write/flush failure, short/unsealed transfer, orphan/power-loss
  states, space reserve, budget and deck/active pins. Analysis writer/parser
  round trips and borrowed-stream ownership pass.
- All eight UI presentations: legacy, product compact/wide, native compact/wide,
  runtime compact/wide and link-wide. The Link scenario uses the real shared
  Library with a **prepared-artifact mock**, checking successful LOAD, playing
  lock, cancelled download preserving the old deck, and incoming LOAD independent
  of selection. This does not emulate audible decoder or firmware NFS timing.
  `link_loaded` and the affected Link status captures were visually reviewed
  before updating that manifest; the other seven manifests remain unchanged.
- Default five-minute virtual dual-deck Master Tempo soak: zero source drift,
  clicks or clipping. This does not qualify P4 deadlines or audible hardware.
- Ordinary ESP-IDF **v6.0.2** builds for both targets, board/project isolation,
  LVGL 9.5.0, binary budget `0x380000`, unchanged committed locks and regular
  recorder/storage/preview exclusions. Local uncommitted development binaries
  are evidence only; clean CI builds identify the pushed source exactly.
  Final local sizes: JC4880 2,559,152 bytes; JC1060 2,610,448 bytes, both
  below the unchanged 3,670,016-byte application budget.
- `git diff --check` and documentation integrity. Generated configurations,
  managed components and build trees are excluded from the commit.

Reproduction:

```powershell
$env:Path += ';C:\msys64\ucrt64\bin'
./tests/run_p4_host_tests.ps1
./tests/audio_keylock_soak/run_audio_keylock_soak.ps1
./tests/ui_simulator/run_ui_simulator_e2e.ps1 -Presentation link-wide -KeepArtifacts
wsl -d Ubuntu -- bash -lc 'cd /mnt/c/Users/Daniel/.codex/worktrees/fork-improvements/DDJ-FFL4 && bash tests/djlink/run_linux_sanitizers.sh'
```

Logs: `.cache/j_host_final.log`, `.cache/j_sanitizers_final.log`, `.cache/j_soak.log`,
`.cache/j_ui_*.log` and each target's ignored `build_j.log`. Hosted clean target
matrices also cover experimental recorder/PSRAM and preview variants separately.
The final pushed SHA and its CI run are reported together at package delivery.

Initial CI [37243167572](https://github.com/dvucinozd/Pajoniiir/actions/runs/37243167572)
on `b6f3a220` found a host-test portability error: strict Linux C11 needed
`_POSIX_C_SOURCE` for the forced `fileno` write-failure fixture. Windows and
the pthread sanitizer build exposed it already. The follow-up adds the feature
declaration in that test; no firmware behavior changes and no test is removed.

## Physical gates, rollback and next work

| Gate | Result |
| --- | --- |
| Identity A/B, corruption/interruption/space/pins and metadata | PASS, automated |
| Actual localhost UDP/TCP with sanitizers | PASS |
| Shared LOAD/lock/cancel/incoming selection and all layouts | PASS, simulator |
| SD/USB source gate and selected teardown lifecycle | PASS, host harness |
| Both ordinary builds, locks/budget and existing compatibility suites | PASS |
| Real JC1060/CDJ and rekordbox NFS/export/analysis/artwork | NOT RUN |
| Exact-card power loss, removal/full-card and fsync latency | NOT RUN |
| Concurrent download with physical dual-deck USB/audio/resource timing | NOT RUN |
| DDJ-400 channels, listening/reconnect and sync phase | NOT RUN |
| JC4880 active resources/listening/final 180-minute exact-image soak | NOT RUN; deferred by operator |

Keep Link OFF until the applicable hardware session is available. Software
rollback is the parent integration commit; the cache uses a separate versioned
directory and cannot modify the local USB library. No installed-device rollback
is needed because the retained JC4880 image was not changed. Public release,
signing/OTA/candidates and hardware acceptance do not follow from this closure.
Next: K network clock/sync/master, then L compatibility and final candidates.
