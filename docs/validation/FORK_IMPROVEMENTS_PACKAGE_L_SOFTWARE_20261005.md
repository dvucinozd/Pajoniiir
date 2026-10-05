# Package L: integration candidates and qualification handoff

> Historical / scenario-specific record, indexed 2026-10-06. Results, hashes
> and pending items below apply to the named images and sessions. They are not
> a current installed-device or public-channel claim. See the
> [v91 release](JC4880_V91_RELEASE_20261005.md) and [current status](../DOCUMENTATION_STATUS.md) for later acceptance;
> no NOT RUN or waived scenario is converted into PASS by this reconciliation.

Date: 2026-10-05. Integration branch `codex/fork-improvements`, based on
`0f1dfc6c76e02b1bc7a316cb6bec342024f49392`. Package K's exact
[CI run 37249883560](https://github.com/dvucinozd/Pajoniiir/actions/runs/37249883560)
passed all eight jobs. Frozen donor remains
`428b97dd4a175f03d3a172c8db9c4d5ed94195fb`; this package adds local delivery
guards, not new donor modules. APTA stays outside this integration.

## Final software scope

Packages A-K are software verified. L completes the ordinary candidate tooling
and acceptance documentation. A completed software program does not imply a
released product. Production M2.4 remains the public baseline. The last-known
installed JC4880 is `M2.4-61-gc4912d5b`; no installation was performed today.
The operator deferred flashing until the next session.

- The packager accepts explicit `main-deck-p4` or `main-deck-jc1060`, defaulting
  to the existing JC4880 behavior. Each has its own signed project/bundle and
  output directory. Signature/key/schema and ECDSA-P256 behavior are unchanged.
- Both binary descriptors and signed manifests must match project and version.
  Runtime app-descriptor comparison is bounded, rejecting unterminated fields.
  Tests cover both wrong-board directions, correct same-board acceptance,
  version mismatch, corrupted signatures/hash/size and interrupted bundles.
- Ordinary packaging rejects recorder/SD/USB-DMA experiments, dj_ui preview,
  dirty source versions and images over the 0x380000 application budget. Actual
  factory/OTA slots stay 0x400000 each on the existing 16 MiB layout. The build
  budget is a smaller independent limit; partitions were not enlarged.
- Channel schema remains version 1 with the `p4` silicon entry. JC4880 uses
  `https://ota.pajoniiir.eu`; JC1060 uses its own `/jc1060` channel root. The
  channel generator rejects using the JC4880 root for JC1060 or a renamed
  bundle whose signed project belongs to another board. It writes local files
  only; no public upload or channel switch is part of this delivery.
- `create_integration_candidate.py` freezes a local evidence record only after
  clean/pushed source, all eight exact-SHA CI jobs, build identity/version,
  ESP-IDF v6.0.2, board isolation, ordinary configuration, budget and signatures
  pass. It records image/bundle hashes, both locks, configuration, partition and
  bootloader hashes. The manifest signature is verified too. Initial wired
  install artifacts and offsets are retained for a future JC1060 bring-up.
- Final qualification defaults to zero new output-late events. The existing
  Soak mode already requires at least 180 minutes. Listening, controls and
  physical acceptance remain explicit operator gates.

## Evidence and candidates

The local full host gate includes S3CP v2/v3 compatibility and v4 parser,
compiler/storage/upload/runtime tests; signed OTA and startup/rollback helpers;
cache identity/interruption and both core transport lifecycles. The unchanged
CI matrix executes all eight UI presentations and Linux network ASan/UBSan,
then builds both ordinary boards and five separate experiment/preview variants.
Every firmware job verifies dependency locks and linked USB/SD wrappers.

L local preflight PASS: full P4 runner, nine real-crypto/signing/candidate tests,
ordinary packaging/channel tests on PowerShell 5.1 and 7, release harness strict
counter self-test, bounded descriptor and startup suites, documentation integrity
and `git diff --check`. Both IDF v6.0.2 builds pass board/budget verification:
JC4880 2,565,936 bytes; JC1060 2,624,768 bytes. Both locks are unchanged.
Fresh committed candidates and their exact hosted run follow this preflight.

Before packaging, L's exact pushed SHA must pass all eight CI jobs. Fresh
ordinary builds use separate `build_l_candidate` directories and isolated
generated sdkconfig files on ESP-IDF v6.0.2. Build trees and signed artifacts
remain outside Git. Final SHA, CI URL and hashes are delivered with the
candidate-evidence files; never replace these values with a newer branch HEAD.

Local output root:
`D:\Documents\.codex-reviews\Pajoniiir-L-candidates-20261005`.
JC4880 is in `pajoniiir-<git-version>` and JC1060 in
`pajoniiir-jc1060-<git-version>`. Each contains `.bin`, signed `.ddjota`,
`manifest.json`, `manifest.sig`, a local `latest.json`, `candidate-evidence.json`
and `wired/` initial-install artifacts. Signing material is never copied.

Reproduction after clean commit/push and exact CI success:

```powershell
./tools/package_ota_release.ps1 -Project main-deck-p4 -BuildName build_l_candidate -OutputRoot <local-output-root> -SigningKey <external-private-key>
./tools/package_ota_release.ps1 -Project main-deck-jc1060 -BuildName build_l_candidate -OutputRoot <local-output-root> -SigningKey <external-private-key>
./tools/publish_ota_release.ps1 -Project main-deck-p4 -ReleaseDir <jc4880-candidate> -WriteToReleaseDir
./tools/publish_ota_release.ps1 -Project main-deck-jc1060 -ReleaseDir <jc1060-candidate> -WriteToReleaseDir
python tools/create_integration_candidate.py --repo-root . --build firmware/main-deck-p4/build_l_candidate --release <jc4880-candidate> --project main-deck-p4 --public-key firmware/common/ota_manifest/keys/ddj_ota_release_public.der --ci-evidence <exact-gh-run-json>
```

The second candidate uses the same command with JC1060 project/build/release.
`publish_ota_release.ps1` generates a local discovery document; it does not
publish it to a server. Initial partition layout requires a separately authorized
wired install. App-only OTA cannot migrate it.

## Support and physical acceptance matrix

| Board / controller / peer | Firmware evidence | Current status |
| --- | --- | --- |
| JC4880 / FLX4 / local USB | Public M2.4 | Existing production baseline and its documented limitations |
| JC4880 / FLX4 / local USB | c4912d5b (M2.4-61) | Focused AP/USB/controls/paused resources PASS; full active audio/soak remains open |
| JC4880 / FLX4 / local USB | L candidate exact SHA in evidence | Software verified; new exact-image hardware gates NOT RUN |
| JC1060 / DDJ-400 / local media | L candidate exact SHA in evidence | Software verified; board/controller/audio bring-up NOT RUN |
| JC1060 / supported controller / CDJ | L candidate exact SHA plus peer model/version | Codec/mock/build verified; real peer gates NOT RUN |
| JC1060 / supported controller / rekordbox | L candidate exact SHA plus host/version/configuration | Codec/mock/build verified; real peer gates NOT RUN |
| Either board / experimental recorder | Separate experimental configuration | Software experiment only; exact-card timing/power-loss/audio NOT RUN |

No general DDJ-400/CDJ/rekordbox hardware-support statement follows from this
matrix. Future qualification records must name panel/board revision, controller
VID/PID, peer/software version, sink/sample-rate and exact firmware SHA.

## Next installation and release gates

1. Select the retained JC4880 ordinary candidate; verify local signed hashes
   and recovery bundle `Pajoniiir-recovery-M2.4-7-gb8d9cb7` before the session.
2. With both decks stopped and recorder inactive, install the signed `.ddjota`
   over the existing service OTA path. Ignore COM ports belonging to other work.
3. Confirm expected version/slot, image health, previous design, AP/API, USB0,
   FLX4 MIDI/LED/UAC and startup readiness. Measure resources with the same
   scenarios and absolute floors used by H, including active dual-deck playback.
4. Exercise real MP3/WAV/FLAC, seek/cue/loop resize, scratch/Master Tempo,
   persistent cue edits, artwork/listing and MAIN/cue isolation/listening.
5. Verify removal/reconnect, soft reboot, signed OTA and negative startup
   rollback. Finish at least 180 minutes on this exact final image: zero new
   strict underrun/output-late/USB faults, no watchdog or leak and clean sound.
6. Qualify JC1060 separately when available: panel/touch/topology, DDJ-400 four
   channels/reconnect, real Ethernet browse/download/media swap, CDJ and
   rekordbox peers, measured audible sync phase/master handoff and recovery.
7. Publish only a concretely accepted configuration. Tag/channel/public release
   needs its own verified decision; green CI or candidate signing does not
   authorize publication. Changed image SHA invalidates exact-image acceptance.

The [first JC4880 L session](JC4880_L_CANDIDATE_20261005.md) now supersedes the
initial NOT RUN installation status: signed OTA/startup/controls passed focused
checks, but a 44-frame USB packet loss failed strict audio smoke. Final hardware
acceptance and soak remain open. For JC1060 all physical gates remain NOT RUN.
The older H observations
are retained as historical evidence, not transferred to these binaries. Rollback
uses the retained signed previous JC4880 image; no persistent schema migration
is added by L. JC1060 requires initial wired provisioning before app-only OTA.
