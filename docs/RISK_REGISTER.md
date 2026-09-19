# P4 Risk Register

Status: **active P4-only register, reviewed 2026-09-14**.

| Priority | Risk | Current evidence | Required disposition |
| --- | --- | --- | --- |
| CLOSED (bench) | Common 5 V or downstream VBUS is unsafe, undersized or backfed | On 2026-09-11 the operator reported that all requested continuity, backfeed, voltage, drop and current measurements were ideal or inside their allowed limits, with no brownout/reset | Preserve the accepted wiring; repeat and record numeric measurements after any wiring/supply change and in the final enclosure |
| CLOSED (bench lifecycle) | USB0 media does not recover after boot, remove, active decode, software reboot or OTA reboot | The complete matrix is accounted: 43 PASS, 7 operator-waived I/J cycles and 0 pending. K1/K2 software reboots and L1/L2 signed OTA reboots automatically restored both roots and 100 tracks with clean playback/fault evidence | Preserve the qualified topology and rerun relevant cycles after USB, power, OTA or enclosure changes; do not reopen waived I/J cycles |
| ACCEPTED LIMITATION | USB1 FLX4 control/audio recovery loses USB0, repeats recovery or leaves controls latched | H1--H5, I1, I2 and jog-held J1 passed. I3--I5 and J2--J5 were explicitly waived by the operator on 2026-09-14 | Preserve the 8 accepted reconnect results; Groups I/J are closed and the untested held-control variants are accepted release-scope limitations |
| P1 | Sustained bounded-cache I/O blocks audio | Thirty-minute dual-MP3 exact-image soak passed | Verify physical WAV/FLAC files, mixed-format stress, BNA and locked-backend-read counters |
| P1 | Audio teardown, Master Tempo or combined DSP misses real P4 deadlines | `RC2-149-ga9c2898` reproduced dual-Master-Tempo output lateness, underrun, UAC loss and `IDLE0` Task-WDT. Commit `909e068` bounds the WSOLA search; exact image `RC2-150-g909e068` passed a 187-second MP3 + 96 kHz FLAC dual-MT smoke with operator-confirmed normal sound, one boot epoch, zero underruns/UAC loss and 12 isolated late warnings (max 15,569 us) | Keep the P1 open until the longer mixed-format Master Tempo/scratch/FX MAIN/cue run measures phase and sink margin; treat the bounded smoke as closure only for the reproduced immediate WDT |
| P1 | Lifetime idle UAC underflow is mistaken for active data loss | Fixed and exact-image smoked in `RC2-113-gaf597d8` | Preserve session-scoped flags and repeat in multi-hour soak |
| P1 | First remote transport action is consumed only to dismiss the screensaver | Fixed and exact-image smoked after more than 120 seconds idle | Preserve remote queue semantics and authoritative state confirmation |
| P1 | Pull/push OTA or web mutation leaves the product unavailable | Signed push OTA repeatedly passes; L1/L2 prove opposite-slot boot and post-reboot dual-USB recovery; hardened pull and fault paths are host-tested | Run AP-to-STA-to-AP, slow/interrupted request and intentional rollback acceptance |
| P1 | Final enclosure changes power, temperature, RF or service access | No closed-enclosure qualification yet | Measure power/thermal/RF, verify strain relief and retain wired recovery before sealing |
| P2 | Public service AP credential permits nearby disruption | AP is operator-controlled and firmware remains signature-protected, but association credential is public | Use per-device credential for distributed units or explicitly accept the risk for a private prototype |
| P2 | Signing key is lost or cannot rotate | Development key `rel-001` is backed up | Define encrypted or hardware-backed custody, recovery and multi-key rotation before production |
| P2 | Irreversible platform security is enabled without recovery planning | Secure Boot and Flash Encryption are not production-provisioned | Decide and document eFuse, signing, encryption, recovery and manufacturing procedure before enabling |
| P2 | A non-FLX4 profile is advertised without real hardware evidence | Host fixtures pass only | Keep non-FLX4 support out of first-release claims until physical descriptor/MIDI/LED/UAC acceptance |
| P3 | Recorder is re-enabled on an unqualified card | Recorder remains compiled out | Keep disabled unless scope reopens with SD latency and power-loss qualification |

## Release rule

The feature branch remains non-mergeable while any P0 or mandatory P1 row is
open. A focused smoke closes only the exact scenario it exercised; it must not
be promoted to repeated, electrical, multi-format, multi-hour or enclosure
acceptance.
