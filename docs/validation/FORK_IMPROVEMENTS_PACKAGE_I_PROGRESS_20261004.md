# Package I progress: Ethernet Pro DJ Link

Integration branch: `codex/fork-improvements`. Donor frozen at
`428b97dd4a175f03d3a172c8db9c4d5ed94195fb` (kayrozen/Pajoniiir v323).
JC4880 remains on the installed `M2.4-61-gc4912d5b` candidate. No image is
installed and no production channel changes as part of this software work.
Physical audio testing and final soak are deferred by the operator until the
end of integration, not waived. JC1060/DDJ-400/real Link peers remain NOT RUN.

## I1: MIT codec and executable tests

Imported the nine portable C codec modules into `firmware/common/djlink` with
the original MIT license, README and frozen-source provenance. Only JC1060
references the component; this step does not start transport or enable Link.
No donor application, UI, audio engine or IP/player-keyed cache is imported.

Corrected DBServer binary/string decoding: short/empty payloads are borrowed
byte arrays, never read as 32-bit numeric fields. Field tags must match their
declared argument types. Builders reject nonzero NULL payloads and oversized
lengths without integer overflow. A prefix decoder reports exact consumed bytes
for future TCP framing; truncated input returns no completed message.

The original NFS UDP fixture declared a 110-byte name at offset 600 in a
700-byte stack buffer, causing a 10-byte test-side overread under ASan. An
independent valid 110-byte source fixes the fixture while preserving the intended
packet-capacity checks. The failing test was retained and rerun.

Verification on 2026-10-04:

- Three Windows portable suites PASS, including heap-sized short-field and
  malformed/tag/overflow/TCP-prefix regressions.
- All four Linux/WSL suites PASS with ASan, UBSan and leak checking, including
  real localhost UDP NFS loss/reorder/retry/cancel mocks (1.5 MiB transfer).
- Linux sanitizer invocation is part of the existing CI workflow; Windows
  portable suites run in the full P4 host runner.
- JC1060 ordinary ESP-IDF 6.0.2 build PASS: 2,554,384 bytes, isolated BSP,
  LVGL 9.5.0 and correct app identity; dependency locks unchanged.
- Full P4 host runner PASS (exit 0); JC4880 ordinary ESP-IDF 6.0.2 build
  PASS at 2,556,800 bytes. Both board validators and `git diff --check` PASS.

The bounded NFS client tests are codec/transfer foundations for J, not a claim
that persistent cache, admission or remote load is implemented. Next: a bounded
discovery/dual-player claim model, Ethernet-only transport and serialized browse.

## I2: bounded discovery and a shared two-player claim

The heap-free `dj_link_core` model receives validated discovery/status/beat/
position packets, tracks at most eight peers and expires silent peers after
5 seconds. Source epochs change on expiry, IP/MAC replacement and transport
restart; callers can reject results referring to an old source epoch. Epochs
are runtime session identifiers, never persistent cache identity.

One claim coordinator admits both decks together. It prefers adjacent free
numbers within 1-4, then 5-6, permitting two distinct non-adjacent free numbers.
With fewer than two free numbers both remain observers with a reason. Conflicts
withdraw both claims, then retry; no duplicate identity or single-player fallback
is published. The 3x announcement/MAC/IP/final stages and 300 ms interval derive
from the frozen donor session; keep-alive period is 2 seconds. No media/library
advertisement, donor playback extrapolation or direct deck mutation is imported.

Windows and Linux sanitizer tests PASS: all 64 occupancy combinations, claim/
active conflicts, observer recovery, 5/6 compatibility, Rekordbox collection
discovery, peer-table bounds, replacement/timeout/reconnect epochs, malformed
and short packets, builder capacity failure and monotonic-clock wrap. Full P4
host runner PASS (exit 0). JC1060 ESP-IDF 6.0.2 builds the model; ordinary image
remains 2,554,384 bytes because this step does not start transport. Lock unchanged.
Transport, Settings activation and serialized browse still remain to implement.
