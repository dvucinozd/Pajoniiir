# P4 Risk Register

Status: **active P4-only register, reconciled 2026-09-20**.

| Priority | Risk | Current evidence | Required disposition |
| --- | --- | --- | --- |
| CLOSED (bench) | Common 5 V or downstream VBUS is unsafe, undersized or backfed | On 2026-09-11 the operator reported that all requested continuity, backfeed, voltage, drop and current measurements were ideal or inside their allowed limits, with no brownout/reset; on 2026-09-20 the operator confirmed this is the existing enclosure wiring | Preserve the accepted wiring; repeat and record numeric measurements after any wiring, supply or enclosure change |
| CLOSED (bench lifecycle) | USB0 media does not recover after boot, remove, active decode, software reboot or OTA reboot | The complete matrix is accounted: 43 PASS, 7 operator-waived I/J cycles and 0 pending. K1/K2 software reboots and L1/L2 signed OTA reboots automatically restored both roots and 100 tracks with clean playback/fault evidence | Preserve the qualified topology and rerun relevant cycles after USB, power, OTA or enclosure changes; do not reopen waived I/J cycles |
| ACCEPTED LIMITATION | USB1 FLX4 control/audio recovery loses USB0, repeats recovery or leaves controls latched | H1--H5, I1, I2 and jog-held J1 passed. I3--I5 and J2--J5 were explicitly waived by the operator on 2026-09-14 | Preserve the 8 accepted reconnect results; Groups I/J are closed and the untested held-control variants are accepted release-scope limitations |
| CLOSED (M2.1 baseline) | Sustained bounded-cache I/O blocks audio | Real WAV/FLAC natural EOF plus simultaneous MP3+FLAC and MP3+WAV windows passed with zero locked-read, PCM, late and BNA deltas and clean audio | Repeat after cache, decoder, filesystem or USB scheduling changes |
| CLOSED (M2.1 baseline) | Audio teardown, Master Tempo or combined DSP misses real P4 deadlines | Bounded WSOLA fixed the reproduced WDT; focused mixed-rate dual-Master-Tempo and the final 180.156-minute combined scratch/FX/MAIN/cue soak passed without strict fault delta or audible defect | Direct cycle-margin profiling remains uncaptured; repeat after audio/DSP scheduling changes |
| CLOSED | Lifetime idle UAC underflow is mistaken for active data loss | Session-scoped health passed focused transitions and the final multi-hour soak | Preserve session-scoped flags and regression coverage |
| CLOSED | First remote transport action is consumed only to dismiss the screensaver | Fixed and exact-image smoked after more than 120 seconds idle | Preserve remote queue semantics and authoritative state confirmation |
| CLOSED (M2.1 baseline) | Pull/push OTA or web mutation leaves the product unavailable | Public pull, AP-to-STA-to-AP recovery, interrupted upload, signed opposite-slot rollback, guarded mutations and post-reboot dual-USB recovery passed | Repeat the relevant matrix after OTA, network or partition-layout changes |
| ACCEPTED LIMITATION | Final enclosure changes power, temperature, RF or service access | Operator reports the unit has operated in its current intended enclosure for approximately two months and confirms wired recovery access; dedicated numeric thermal/RF/strain evidence was not captured | Preserve the current topology and repeat qualification after any enclosure, wiring, supply or RF-layout change |
| ACCEPTED LIMITATION | Shared service credential permits nearby disruption | Operator selected one shared service password; WPA2/WPA3 transition mode advertises PMF capability; firmware updates remain signature-protected | Verify association from the actual service client on the final image; restrict distribution/exposure and revisit per-device credentials or WPA3-only PMF-required mode if deployment expands |
| CLOSED FOR M2.1 / DEFERRED | Signing key is lost or cannot rotate | Operator confirmed encrypted offline primary and separately stored encrypted backup copies for `rel-001` on 2026-09-20; M2.1 retains that key and the successor-key-before-retirement rotation boundary is documented. On 2026-09-20 the operator explicitly accepted untested backup recovery signing as deferred maintenance | Before a planned key rotation, verify backup recovery without exposing key material and implement multi-key overlap; emergency replacement retains wired recovery |
| ACCEPTED LIMITATION (M2.1) | Physical access can bypass software-only trust because hardware-rooted protection is absent | There is no spare P4 board; Secure Boot, Flash Encryption and security eFuses remain disabled so the only unit and its wired recovery path are not put at irreversible risk | Preserve controlled physical access, closed enclosure, signed OTA and wired recovery; qualify RSA-PSS Secure Boot v2 plus release-mode Flash Encryption on a dedicated pilot before any later provisioning |
| P2 | A non-FLX4 profile is advertised without real hardware evidence | Host fixtures pass only | Keep non-FLX4 support out of first-release claims until physical descriptor/MIDI/LED/UAC acceptance |
| P3 | Recorder is re-enabled on an unqualified card | Recorder remains compiled out | Keep disabled unless scope reopens with SD latency and power-loss qualification |

## Release rule

M2.1 is released with exact-image acceptance complete and encrypted
primary/backup signing-key custody operator-confirmed. Untested backup recovery
signing is explicitly accepted as deferred future maintenance. The security
decisions themselves are recorded in
[`SECURITY_PROVISIONING_POLICY.md`](SECURITY_PROVISIONING_POLICY.md). A focused
smoke closes only the exact scenario it exercised; it must not be promoted to
unrelated acceptance.
