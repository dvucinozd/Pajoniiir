# P4 Risk Register

Status: **active P4-only register, reviewed 2026-09-13**.

| Priority | Risk | Current evidence | Required disposition |
| --- | --- | --- | --- |
| CLOSED (bench) | Common 5 V or downstream VBUS is unsafe, undersized or backfed | On 2026-09-11 the operator reported that all requested continuity, backfeed, voltage, drop and current measurements were ideal or inside their allowed limits, with no brownout/reset | Preserve the accepted wiring; repeat and record numeric measurements after any wiring/supply change and in the final enclosure |
| P1 | USB0 media does not recover after boot, remove or active decode fault | Groups A--G pass: cold/warm boots, both insertion orders, five idle removals, five deterministic Library-load removals and five deterministic audio-load removals; progress is 31/50 cycles with FLX4 retained | Complete USB1 and held-control lifecycle groups plus reboot/OTA recovery |
| P1 | USB1 FLX4 control/audio recovery loses USB0, repeats recovery or leaves controls latched | One bounded FLX4 reconnect passed on an earlier installed image; `RC2-116-g77d723c` retains durable connection delivery and packet-loss accounting and remained connected through the targeted three-hour soak | Run repeated physical reconnect, held-control saturation, LED resync and UAC recovery while USB0 remains mounted |
| P1 | Sustained bounded-cache I/O blocks audio | Thirty-minute dual-MP3 exact-image soak passed | Verify physical WAV/FLAC files, mixed-format stress, BNA and locked-backend-read counters |
| P1 | Audio teardown, Master Tempo or combined DSP misses real P4 deadlines | Deterministic host soak passes; `RC2-116-g77d723c` passed a targeted three-hour dual-MP3 limiter/WDT soak. Fourteen rare late warnings had no underrun, active UAC loss, observable output failure or reboot; phase maxima point to bounded I2S pacing/scheduler jitter, not a confirmed DSP defect | Measure worst-case two-deck Master Tempo/scratch/FX P4 CPU/I2S margins, expose or capture sink counters, and perform STOP/reload plus listening acceptance; change code only for a reproducible violation |
| P1 | Lifetime idle UAC underflow is mistaken for active data loss | Fixed and exact-image smoked in `RC2-113-gaf597d8` | Preserve session-scoped flags and repeat in multi-hour soak |
| P1 | First remote transport action is consumed only to dismiss the screensaver | Fixed and exact-image smoked after more than 120 seconds idle | Preserve remote queue semantics and authoritative state confirmation |
| P1 | Pull/push OTA or web mutation leaves the product unavailable | Signed push OTA repeatedly passes; hardened pull and fault paths are host-tested | Run AP-to-STA-to-AP, slow/interrupted request, rollback and post-reboot dual-USB acceptance |
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
