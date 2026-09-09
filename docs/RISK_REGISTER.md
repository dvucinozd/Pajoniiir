# P4 Risk Register

Status: **active P4-only register, reviewed 2026-09-09**.

| Priority | Risk | Current evidence | Required disposition |
| --- | --- | --- | --- |
| P0 | Common 5 V or downstream VBUS is unsafe, undersized or backfed | Earlier dual-deck bench runs produced raw brownout; later focused and 30-minute runs stayed stable on an improved supply but no rail measurement was available | Protect and current-limit each downstream VBUS, isolate native VBUS, measure voltage/current at startup and sustained load, then repeat the full matrix |
| P1 | USB0 media does not recover after boot, remove or active decode fault | One post-OTA mount and one USB0 reinsert passed with FLX4 active | Cold/warm boot, both insertion orders, active-load/decode removal and repeated lifecycle gate |
| P1 | USB1 FLX4 control/audio recovery loses USB0, repeats recovery or leaves controls latched | One bounded FLX4 reconnect passed on the installed image; `c8b2711` adds durable connection delivery and packet-loss accounting with host evidence | Install `c8b2711`, then run repeated physical reconnect, held-control saturation, LED resync and UAC recovery while USB0 remains mounted |
| P1 | Sustained bounded-cache I/O blocks audio | Thirty-minute dual-MP3 exact-image soak passed | Verify physical WAV/FLAC files, mixed-format stress, BNA and locked-backend-read counters |
| P1 | Audio teardown, Master Tempo or combined DSP misses real P4 deadlines | Deterministic host soak passes; `c8b2711` hardens worker teardown and nonblocking output bookkeeping | Install current source candidate, measure worst-case two-deck P4 CPU/I2S margins and perform STOP/reload plus listening acceptance |
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
