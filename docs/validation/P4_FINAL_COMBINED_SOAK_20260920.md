# P4 final combined soak — 2026-09-20

Status: **PASS**.

## Candidate and declared gate

- Firmware: `RC2-153-g66b5fee-dirty`
- Source-equivalent repair commit: `b9139e1bdab79e7d0aec279383e0b8f859d3eea3`
- Installed slot: `ota_1`
- Service-log boot epoch: `472`
- Toolchain: ESP-IDF v6.0.2
- Declared duration: 180 minutes
- Actual monitored duration: `180.156` minutes
- Harness: `tools/run_p4_release_qualification.ps1 -Mode Soak`
- Tracks: D1 key 115 `House Of Confusion.mp3`; D2 key 18
  `Symphony No.6 (1st movement).flac`
- Initial physical state: Master Tempo enabled on both decks, opposing pitch
  (`+3.01%` / `-3.01%`), Beat FX Flanger enabled, MAIN and cue audible

The harness sampled `/api/firmware` and `/api/status` every five seconds and
executed a seek, loop or CUE/restart operation every 180 seconds. It completed
60 scheduled operations.

## Strict result

| Counter/state | Baseline | Final | Delta |
| --- | ---: | ---: | ---: |
| UAC submitted blocks | 567,501 | 2,429,071 | +1,861,570 |
| UAC underflow frames | 1,111,604 | 1,111,604 | 0 |
| UAC overflow frames | 0 | 0 | 0 |
| UAC dropped blocks | 0 | 0 | 0 |
| UAC packet failures | 0 | 0 | 0 |
| UAC packet-lost frames | 0 | 0 | 0 |
| PCM underrun D1 / D2 | 0 / 0 | 0 / 0 | 0 / 0 |
| Locked backend reads D1 / D2 | 0 / 0 | 0 / 0 | 0 / 0 |
| Active UAC loss flags | 0 | 0 | 0 |
| Output-late count | 2 | 70 | +68 |

Firmware version, slot and boot identity remained unchanged. USB0 remained
mounted; the FLX4 profile, MIDI IN/OUT and UAC remained active; no current TWDT
evidence appeared. The maximum recorded output-late duration was `26,057 us`.
The `+68` count remained below the predeclared allowance of 120 and did not
correlate with UAC/PCM/packet loss, reboot or audible degradation.

The harness wrote its raw JSON evidence as
`tmp/p4-release-qualification/soak-20260920T140650Z-boot472.json`.
That runtime artifact is intentionally ignored by Git; this record preserves
the release-relevant result.

## Operator acceptance

After the automated run, the operator explicitly confirmed that the sound was
normal for the entire three hours. This closes the required listening gate as
well as the automated counter gate.

## Post-evidence teardown observation

The harness captures its final acceptance snapshot before its cleanup
`Stop-Decks` calls. That cleanup produced one idle-only UAC producer drop and
nine overflow frames while switching from active audio to continuous silence.
The values then stayed fixed for a ten-second observation while 445,686 silent
frames were submitted; underflow stayed unchanged, active loss flags stayed
zero, no `UAC_DATA_LOSS` event appeared and the operator heard no defect.

This event is outside the declared timed soak and does not invalidate its PASS.
It remains recorded as an idle teardown diagnostic rather than being hidden or
misrepresented as an active-playback fault.

## Release implication

Together with the earlier 78.176-minute combined functional PASS and the
focused UAC idle-continuity remediation, this closes the accelerated combined
functional/soak gate for the current candidate. Remaining M2 beta work is the
reduced OTA/fault matrix, version-prefix migration, final fresh-checkout
build/test/package pass, exact-image smoke, documentation freeze and tag.
