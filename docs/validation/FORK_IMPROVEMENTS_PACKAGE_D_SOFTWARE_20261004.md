# Package D software closure — 2026-10-04

> Historical / scenario-specific record, indexed 2026-10-06. Results, hashes
> and pending items below apply to the named images and sessions. They are not
> a current installed-device or public-channel claim. See the
> [v91 release](JC4880_V91_RELEASE_20261005.md) and [current status](../DOCUMENTATION_STATUS.md) for later acceptance;
> no NOT RUN or waived scenario is converted into PASS by this reconciliation.

Development `codex/fork-improvements`, base `409ec455`.
Donor: kayrozen/Pajoniiir `428b97dd4a175f03d3a172c8db9c4d5ed94195fb`,
DDJ-400 JSON only; extended-v2 binary not imported. Production M2.4 unchanged.

## Implemented

- S3CP v4 raw 9 NOTE_SELECT, raw 8 unchanged CC7_TO14, header/input/output
  lengths unchanged. Initial SysEx at most 128 bytes, output scale saturated
  using integer arithmetic, explicit channel-filter capability.
- Runtime parser, SD/atomic upload validator, compiler, converter, generated
  DDJ-400 profile and all related tests updated together.
- Profile worker sends initial SysEx once before LED mapping; per-packet
  connection epoch and USB producer generation checks reject stale work.
  Failed initialization disables the profile and reports FAILED. Reinstall
  queues reactivation through the worker. No filesystem work in USB callbacks.
- DDJ-400 selector CH1/CH2/MASTER emits 0/1/2 on press; releases ignored.
  MASTER maps to existing both-deck FX, not a post-fader master bus.
- Eight donor memory-call/store/delete inputs have no current P4 semantic
  action. Explicit `omitted_inputs` preserve provenance; no guessed mapping.
  This does not remove imported memory cues or their UI presentation.
- Separate Web Profile Builder `codex/p4-profile-export-v4`, commit
  `bc47c3c6d37c345cea047f07b89b95e2c9044605`, pushed/remote SHA verified.
  Exporter/parser/schema and Python/browser shared golden bytes coordinated.

## Software evidence

- Full P4 host runner PASS; profile runtime 9,191 checks, SysEx lengths 2..128,
  all terminal USB-MIDI CINs, once-only activation, failed enqueue and disabled
  mapping before initialization. Manager storage/upload negatives PASS.
- FLX4 unchanged golden parity: 12,288 inputs / 1,625 matches, 17 replay
  events, 690 LED combinations / 670 mappings. V2/v3 remain compatible.
- Python converter/compiler: 11 tests PASS, DDJ-400 reproducible 6,048-byte v4.
- Web: 63 Node tests and 20 browser tests PASS, v2/v3 golden bytes unchanged.
- JC4880 ESP-IDF 6.0.2 build PASS, application 2,540,944 bytes below 0x380000.
  Dependency lock unchanged. Final committed-image identity recorded below.
  Exact code commit `39313aa918b68062333cb8ddb8cfeeb5626a8391`, reconfigured
  build descriptor `M2.4-37-g39313aa`, project `main-deck-p4`, descriptor magic
  `0xABCD5432`, 2,540,944 bytes. Local and remote source SHA matched.
- Simulator PASS: 11 unchanged screenshot baselines at 800x480.
- Documentation integrity and git diff whitespace checks PASS.

## Unrun gates and rollback

DDJ-400 MIDI/LED/SysEx/filter/reconnect, FLX4 physical regression, exact-image
180-minute audio soak, heap/stack/deadline measurements, install/OTA and
public Web deployment: **NOT RUN**. DDJ-400 UAC formats/routing/pacing belongs
to package F, not this MIDI-profile acceptance. JC1060 target belongs to E.
No signing, publication or production-channel change performed.

V4 must not be installed onto M2.4. Existing v2/v3 profiles remain unchanged.
Rollback firmware requires a representable v2/v3 JSON recompilation; v4
selectors/SysEx/scaling cannot be preserved by changing the version byte.
Atomic upload rejects invalid input without replacing the valid target.
