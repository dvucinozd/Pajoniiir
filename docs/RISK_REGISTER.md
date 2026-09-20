# P4 Risk Register

Status: **active P4-only register, reconciled 2026-09-20**.

| Priority | Risk | Current evidence | Required disposition |
| --- | --- | --- | --- |
| CLOSED (bench) | Common 5 V or downstream VBUS is unsafe, undersized or backfed | On 2026-09-11 the operator reported that all requested continuity, backfeed, voltage, drop and current measurements were ideal or inside their allowed limits, with no brownout/reset; on 2026-09-20 the operator confirmed this is the existing enclosure wiring | Preserve the accepted wiring; repeat and record numeric measurements after any wiring, supply or enclosure change |
| CLOSED (bench lifecycle) | USB0 media does not recover after boot, remove, active decode, software reboot or OTA reboot | The complete matrix is accounted: 43 PASS, 7 operator-waived I/J cycles and 0 pending. K1/K2 software reboots and L1/L2 signed OTA reboots automatically restored both roots and 100 tracks with clean playback/fault evidence | Preserve the qualified topology and rerun relevant cycles after USB, power, OTA or enclosure changes; do not reopen waived I/J cycles |
| ACCEPTED LIMITATION | USB1 FLX4 control/audio recovery loses USB0, repeats recovery or leaves controls latched | H1--H5, I1, I2 and jog-held J1 passed. I3--I5 and J2--J5 were explicitly waived by the operator on 2026-09-14 | Preserve the 8 accepted reconnect results; Groups I/J are closed and the untested held-control variants are accepted release-scope limitations |
| CLOSED (M2 beta) | Sustained bounded-cache I/O blocks audio | Real WAV/FLAC natural EOF plus simultaneous MP3+FLAC and MP3+WAV windows passed with zero locked-read, PCM, late and BNA deltas and clean audio | Repeat after cache, decoder, filesystem or USB scheduling changes |
| CLOSED (M2 beta) | Audio teardown, Master Tempo or combined DSP misses real P4 deadlines | Bounded WSOLA fixed the reproduced WDT; focused mixed-rate dual-Master-Tempo and the final 180.156-minute combined scratch/FX/MAIN/cue soak passed without strict fault delta or audible defect | Direct cycle-margin profiling remains uncaptured; repeat after audio/DSP scheduling changes |
| CLOSED | Lifetime idle UAC underflow is mistaken for active data loss | Session-scoped health passed focused transitions and the final multi-hour soak | Preserve session-scoped flags and regression coverage |
| CLOSED | First remote transport action is consumed only to dismiss the screensaver | Fixed and exact-image smoked after more than 120 seconds idle | Preserve remote queue semantics and authoritative state confirmation |
| CLOSED (M2 beta) | Pull/push OTA or web mutation leaves the product unavailable | Public pull, AP-to-STA-to-AP recovery, interrupted upload, signed opposite-slot rollback, guarded mutations and post-reboot dual-USB recovery passed | Repeat the relevant matrix after OTA, network or partition-layout changes |
| ACCEPTED LIMITATION | Final enclosure changes power, temperature, RF or service access | Operator reports the unit has operated in its current intended enclosure for approximately two months and confirms wired recovery access; dedicated numeric thermal/RF/strain evidence was not captured | Preserve the current topology and repeat qualification after any enclosure, wiring, supply or RF-layout change |
| ACCEPTED LIMITATION | Shared service credential permits nearby disruption | Operator selected one shared service password; firmware updates remain signature-protected | Restrict distribution and physical/network exposure; revisit per-device credentials if deployment scope expands |
| P2 | Signing key is lost or cannot rotate | Operator selected encrypted offline custody with a separate encrypted offline backup for `rel-001`; provisioning/backup verification and rotation are not yet recorded | Provision both copies, restrict access, verify backup recovery without exposing key material and define multi-key rotation before production |
| P2 | Irreversible platform security is enabled without recovery planning | Secure Boot and Flash Encryption are not production-provisioned | Decide and document eFuse, signing, encryption, recovery and manufacturing procedure before enabling |
| P2 | A non-FLX4 profile is advertised without real hardware evidence | Host fixtures pass only | Keep non-FLX4 support out of first-release claims until physical descriptor/MIDI/LED/UAC acceptance |
| P3 | Recorder is re-enabled on an unqualified card | Recorder remains compiled out | Keep disabled unless scope reopens with SD latency and power-loss qualification |

## Release rule

The accelerated M2 beta was merged into `master` after its mandatory beta gates
closed. An unrestricted production release remains blocked by unresolved
production signing-key provisioning/rotation and security decisions. A focused
smoke closes only the exact scenario it exercised; it must not be promoted to
unrelated acceptance.
