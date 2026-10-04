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

## I3: Ethernet-only UDP worker and persistent activation

JC1060 starts an optional low-priority worker after common P4/NVS startup.
It uses the existing `board_ethernet_netif`, current interface MAC/IP/netmask
and strict `SO_BINDTODEVICE` on each discovery/beat/status socket. Socket creation
or binding failure closes the whole transport; it never falls back to another
interface. The lwIP implementation was checked against the pinned IDF 6.0.2
source: `SO_BINDTODEVICE` calls `udp_bind_netif`. Directed broadcasts use that
interface's subnet. Receive/send waits are bounded at 40 ms, receive batches at
12 packets, and round-robin receive prevents discovery floods starving beats.
Oversized datagrams are rejected. A socket error closes all sockets, invalidates
the source session and retries after 2 seconds. Ethernet loss/IP/netmask change
likewise withdraws identities and clears peer epochs.

The persistent `dj_link` NVS key defaults OFF and has host coverage for enable,
disable, reboot, corrupt value and failed writes. The existing 1024x600 Settings
group now has the Link switch and live OFF/WAIT IP/CLAIMING/OBSERVER/ERROR/player
status. UI reads bounded copies only; all socket work stays in the worker.
Model, snapshot and 6 KiB worker stack are PSRAM-only, with no internal fallback;
audio/USB DMA policy is unchanged. The switch does not start Wi-Fi or Web Remote.
JC4880 does not compile/link the codec, core or Ethernet worker; the board build
validator now enforces that isolation.

Verification:

- Full P4 host runner PASS; settings suite includes 73 checks.
- Six Linux/WSL sanitizer suites PASS, including actual interface-bound three-port
  localhost UDP, failed interface binding, oversize, flood fairness, timeout and
  teardown. Run `bash tests/djlink/run_linux_sanitizers.sh` in Linux/WSL with GCC.
  The script is pinned to LF for Windows checkouts and runs in Linux CI.
- Ordinary ESP-IDF 6.0.2 builds PASS: JC4880 2,557,056 bytes; JC1060 2,562,976
  bytes. Both fixed-budget/project/BSP/isolation validators PASS; locks unchanged.
- Product 800x480 and 1024x600 simulator gates PASS. Reviewed the wide Settings
  capture before changing only its two Settings/restored hashes; shared wide
  preview Settings has the identical reviewed pixels and also passes.
- `git diff --check` PASS. No hardware/OTA/audio test was run for I1-I3.

I is still incomplete: single-session/request DBServer TCP browse, bounded 2,000
row cache, visible-page metadata/folders/playlists and common source/load admission
are pending. Socket mock success is not JC1060 task timing or actual peer
interoperability acceptance. J download/cache and K sync remain separate packages.

CI independently passed all eight jobs on pushed I3 `656f982f`:
[run 37227824038](https://github.com/dvucinozd/Pajoniiir/actions/runs/37227824038).
This includes both clean ordinary targets, both preview targets and the isolated
recorder/storage experiments, plus the full host/simulator/sanitizer gate.

## I4a: serialized DBServer client and TCP mock

Imported the donor's heap-free sans-I/O DBServer client with both codec and
application MIT copyright notices. It owns one session and one request; its
transport hooks do not perform I/O in the model. Port query, greeting, context
setup, all-tracks and folder/playlist menus, render batches and visible-row
metadata are exercised. Blob/path helpers are preserved but remain inactive in
the firmware; their storage/source integration belongs to J.

Local corrections enforce a 2,000-row hard cap (even if the owner supplies a
larger limit), exact prefix framing and source/connection generations. Old
connected/data/closed events cannot alter a restarted session. Setup/request IDs,
numeric menu counts and render row counts are validated. A premature footer
fails instead of publishing a completed partial list or metadata record.
Metadata replies are bounded to 64 fields; malformed UTF-16 surrogates cannot
consume the following valid character. Folder/playlist requests force source
order regardless of global sort. The donor IP/player/track FNV persistent-key
API is excluded; IP/player numbers remain transport locators only.

Verification on 2026-10-04:

- Windows portable client suite PASS: 2,000 rendered rows from a 2,023-row
  advertisement, byte fragmentation/coalescing, hierarchy/order, visible detail,
  UTF-16, malformed/short/incomplete replies, source restart, stale TCP callbacks,
  cancel and timeout.
- Full P4 host runner PASS (exit 0); `git diff --check` PASS.
- Eight Linux/WSL suites PASS under ASan/UBSan/leak checking. The new actual
  localhost TCP mock passes port discovery/setup/fragmented browse, malformed
  reply, disconnect and silent-peer timeout. Existing UDP/NFS suites remain PASS.
- JC1060 ESP-IDF 6.0.2 ordinary build and board validator PASS (2,563,120 bytes);
  dependency locks unchanged. The client compiles but is not yet driven by the
  JC1060 worker, so this is model/codec verification, not on-device browse.

Remaining I4b: Ethernet-bound nonblocking TCP adapter, owned bounded row cache
with truncation/progress, live source-epoch validation at result publication and
Library source/visible-page bridge. Incoming load must enter the existing
load-lock/admission path; no direct deck mutation or premature network load ack.
J then supplies verified local downloads; K supplies authoritative-clock sync.

## I4b: runtime browse and software closure

The Ethernet-bound nonblocking TCP adapter, worker-owned 2,000-row PSRAM cache,
copied visible metadata pages and actual Library source/menu bridge are now
implemented. Invalid source/claim epochs, old command IDs and incomplete lists
cannot become completed UI rows. Incoming load uses LVGL stopped/busy admission;
metadata-only audio remains unavailable until J and is never acknowledged early.

The [2026-10-05 I software closure](FORK_IMPROVEMENTS_PACKAGE_I_SOFTWARE_20261005.md)
supersedes the pending I4b statements above. Full host runner, nine Linux
sanitizer suites, all eight simulator presentations and both ordinary IDF 6.0.2
builds pass; dependency locks are unchanged. JC1060 final local image is
2,581,280 bytes and JC4880 remains 2,557,216 bytes. Actual Ethernet/peer/DDJ-400
hardware acceptance is NOT RUN. No OTA/COM/publication occurred; operator-deferred
audio and final soak remain open. Next work is J.
