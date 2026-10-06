# P4 risk register

Status: **current M2.5 scope and development risks, reconciled 2026-10-06**.

Current release is JC4880/FLX4 `M2.5` from `20f1c3f0`.
The [release record](validation/M2_5_RELEASE_20261006.md) separates exact-image
physical/listening acceptance, diagnostic rollback and publication from unrun
variants. Dated records retain every earlier failed candidate and monitor loss.

| Status / priority | Risk | Evidence and required disposition |
| --- | --- | --- |
| ACCEPTED EXCEPTION (M2.5) | New final-image long soak explicitly waived | Operator waived a new 180m run because audio/deck/DSP code is unchanged. Final tagged OTA/startup and 62.622211s dual playback/listening passed with zero strict faults. No M2.5 long-soak PASS is claimed; v91 evidence remains image-specific |
| ACCEPTED EXCEPTION (v91) | Monitor loss prevents a continuous 180-minute record | Saved 1,015.8899243s + completed 9,785.4764933s = 10,801.3664176s with interruption. Continuation resources/strict deltas and operator sound passed. Preserve INCOMPLETE evidence; uninterrupted 180m remains NOT RUN. No automatic exception for a later image |
| CLOSED in focused v91 scope | Periodic PSRAM heap walks delay realtime service | Earlier ISO SKIPPED reproduced under heap diagnostics. Largest-block PSRAM walks were removed; retain cheap free/minimum readings and mark unmeasured values null. Requalify after diagnostic/allocator changes; wall-time query probes do not measure exact IRQ latency |
| CLOSED in focused v91 scope | PCM5102A DMA quantum differs from mixer output | Earlier 240-vs-256-frame mismatch reproduced output lateness; shared 256-frame quantum and focused/final segmented v91 runs have zero new strict faults. Preserve bounds and measure sink latency separately |
| CLOSED in focused v91 scope | Manual FLAC OUT/loop half starves decode or replays stale PCM | Failed manual-loop candidates are retained in the hardware ledger; v91 source-prefix/timeline handling passes operator OUT/half/double/exit and strict counters. Other format/deck/edge combinations remain NOT RUN |
| CLOSED in focused v91 scope | UI exhausts internal memory or HTTP stack | Earlier H3 network/USB failure and H allocation/stack failures are retained. One presentation, PSRAM-owned LVGL/waveform buffers and request-owned HTTP text pass v91 active resource floors with zero failed critical allocations. Requalify changed scenarios and ownership |
| CLOSED with separate diagnostics | Pending image confirms despite failed startup | Forced unconfirmed restart and readiness rejection at 60.253s restore original v91. Timeout image keeps actual AP/API alive and injects false readiness; an actual induced radio failure is not claimed. Ordinary tools exclude both diagnostic flags |
| CLOSED in focused v91 scope | Signed OTA/reboot leaves both USB roots absent | Corrected supply and recorded v91 OTA/reboot/rollback restore roots automatically. Bare M2.4 cold-cycle observation remains history. Active removal/held-control variants not repeated on v91 remain NOT RUN |
| OPEN / P2 | Software verification is advertised as new hardware support | JC1060/DDJ-400/non-FLX4/real peers are NOT RUN. Qualify exact board/controller/peer/sink/SHA before publishing another configuration |
| OPEN / P2 | Network LOCKED is mistaken for audible phase acceptance | K model/session tests pass; real phase/drift/handoff and per-sink/rate latency are unmeasured. Keep Link OFF by default; calibration is RAM-only and must not cross configurations |
| OPEN / P2 | Replaced peer media reuses wrong cached audio/artwork/cues | J full-identity and A/B host tests pass, but real peer/card replacement is NOT RUN. Unknown volume IDs force session-local identity/fresh downloads; do not promise persistent cue edits across those sessions |
| OPEN / P2 | Network download or SD writer starves playback | Bounded SD gate/admission, pins and manifest-last transactions have software tests. Real download/deadline, SD-removal/full-card/power-loss performance is NOT RUN; qualify exact card/peer |
| OPEN / P3 | Recorder or SD workaround ships unqualified | Both remain OFF in regular firmware and excluded by production packaging. G card/DMA/latency/finalize/overflow/audio gates are NOT RUN; separate experimental qualification required |
| ACCEPTED LIMITATION | Remaining local metadata and transport combinations | Source-cue restore/reimport/cross-media, exhaustive playlist/artwork/PWV4, shifted/touch LOAD and active-removal variants remain NOT RUN. Existing host tests are not physical proof; run focused gates if required by the deployed workflow |
| ACCEPTED LIMITATION | Enclosure/power assumptions change | Earlier operator electrical acceptance applies to unchanged wiring and corrected supply; numeric thermal/RF/strain margins were not captured. Repeat after supply/VBUS/enclosure/RF changes |
| ACCEPTED LIMITATION | Shared service credential permits nearby disruption | Shared credential, WPA2/WPA3 transition with PMF capability and signed OTA remain deployed policy. Restrict service exposure; consider per-device credentials/WPA3-only if distribution expands |
| ACCEPTED LIMITATION | Physical access bypasses application-only trust | Secure Boot, Flash Encryption and security eFuses remain OFF. Maintain controlled enclosure/recovery; qualify irreversible provisioning on a sacrificial board |
| DEFERRED MAINTENANCE | Signing key loss or rotation | Offline encrypted primary/backup custody was operator-confirmed. Backup recovery signature and multi-key overlap are unrun; verify before planned rotation, never expose secrets |
| HISTORICAL WAIVERS | Earlier held-control lifecycle variants were not exercised | M2.1 matrix records 43 PASS / 7 operator-waived; these are not 50 PASS or exact-v91 repetition. Preserve per-image limits |
| CLOSED (historical M2.4) | Valid cross-signed TLS chain rejected | M2.4 fixed the demonstrated M2.3 handshake failure with cross-signed bundle verification while retaining CA/hostname checks. Repeat a live device pull probe after TLS/client/hosting-chain changes |

## Release rule

Accept only a named image/configuration and explicit physical scope. Software,
simulator, package, installed device, sound and publication are separate evidence.
A newer documentation or diagnostic SHA is not the accepted firmware.
Published tags and versioned assets must remain immutable.

JC1060 uses separate project identity/lock/channel. PCM5102A is disabled because
its pins overlap Ethernet; USB-only pacing keeps ES8311 disabled.
Supported formats are bounded four-channel full-speed UAC1 adaptive/synchronous
16-bit or packed 24-bit. UAC2/high-speed, feedback-dependent and 24-in-32 formats
remain unsupported. DDJ-400 isolation/reconnect, physical FIFO/EP0 timing,
sink latency and real peer interoperability remain NOT RUN.

Security decisions are in [SECURITY_PROVISIONING_POLICY.md](SECURITY_PROVISIONING_POLICY.md).
Complete earlier failure/correction details are in
[H](validation/JC4880_H_CANDIDATE_20261004.md) and
[L](validation/JC4880_L_CANDIDATE_20261005.md); they do not reopen completed v91
focused gates or imply those earlier images were accepted.
