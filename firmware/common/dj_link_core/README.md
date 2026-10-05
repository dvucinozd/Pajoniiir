# Bounded Pro DJ Link application model

`dj_link_discovery` is a single-owner, heap-free discovery and dual-player claim
model. Claim stages/timing and the 5/6 keep-alive compatibility byte derive from
kayrozen/Pajoniiir `428b97dd4a175f03d3a172c8db9c4d5ed94195fb`,
`firmware/main-deck-jc1060/components/dj_link/dj_link_session.c` (MIT).
The associated codec and its license are preserved in `../djlink`.
`LICENSE` retains kayrozen's codec attribution; `LICENSE.Pajoniiir` preserves
the frozen donor application's MIT copyright notice for the derived session
and DBServer modules.

Local changes replace independent sessions with one two-player admission
decision. Fewer than two free numbers leaves both decks in observer mode.
Any conflict withdraws both identities and starts a new claim. Names are distinct
but share the board's real MAC/IP. Peer snapshots include reconnect/replacement
epochs, copied status/beat/position and bounded counters. No library is advertised,
no audio position extrapolated, no packet I/O or filesystem/UI access occurs.

Epochs are session-local runtime identity, never durable cache keys.
J adds content identity verification and common load admission.

`dj_link_db` is the frozen donor's sans-I/O DBServer client, adapted for explicit
source and per-connection epochs and a hard 2,000-row request limit. Transport
must pass the epoch captured for that connection to every connected/data/closed
event; old events are ignored after cancel/restart/port discovery. Result owners
must additionally check the live discovery source epoch before publication.
TCP framing uses the corrected codec's exact consumed prefix length. Malformed
menu fields, wrong transaction/setup IDs and premature render footers fail the
request. Folder/playlist menu requests always use source order.

The donor's IP/player/track FNV cache-key API was intentionally excluded.
`dj_link_cache` uses the existing full SHA-256 media identity, complete audio
proof, normalized path/size/timestamp and caller-supplied export/session proof.
The runtime cannot establish a trustworthy NFS volume UUID: every fresh mount
adds a nonce and requires downloading again. Local edits do not migrate between
these unidentified sessions. `dj_link_pdb_stamp_matches` is a retained donor
optimization hint, unused by runtime and never sufficient for a hit.
Versioned completion manifests are published last, after read-back verification.
Cache limits are 1 GiB total/audio, 64 MiB PDB and 64 MiB free-space reserve.
Both deck identities and the active download group prevent pruning. Runtime SD
operations yield at 4 KiB boundaries and serialize handle cleanup after cancel.

`dj_link_pdb` and `dj_link_anlz` are narrow MIT imports from frozen `428b97dd`.
PDB title fixture index 17 is corrected with a distinct index-18 sentinel. Paths
cannot silently truncate. ANLZ round trips use the current common parser and
memory cue ordering/truncation policy, not a second donor library implementation.
The null-output preview crash is fixed. DB file paths are lossless UTF-16;
over-capacity asset blobs fail rather than publishing partial analysis/JPEG.

`dj_link_tcp` implements a worker-owned nonblocking socket adapter shared with
the real localhost tests. Each port-query/database connection requires both
`SO_BINDTODEVICE` and the current Ethernet IPv4 address. Failed binding has no
fallback. Connect/select/send/receive never wait; sends copy to a 128-byte queue
and honor partial consumption. Receive work is bounded at 1,024 bytes per tick.
Descriptor replacement on a callback discards its previous readiness/epoch.

`dj_link_browse` owns metadata in a caller-provided 2,000-row array (PSRAM in
firmware). Partial lists expose progress only. Completed pages are copied to the
LVGL owner; visible-page requests prioritize detail and never borrow cache
pointers. A source or claim generation change closes TCP and hides all rows.
Runtime IDs are not persistent identities. Incoming load routing validates the
known sender address, source epoch, claimed destination and a three-second TTL;
the firmware status socket is unicast-only. Routing never acknowledges or changes
a deck. J connects LVGL admission to the existing load worker; NFS, DB assets,
hashing and JPEG remain outside LVGL/audio output. Incomplete downloads preserve
the old deck. Owned metadata/artwork publish only after accepted audio binding.

`dj_link_sync` is the heap-free worker-owned master coordinator. Peer inventory
epochs cannot revoke our own player claim. Selection remains sticky after loss;
explicit follow can choose a new authority. Handoff checks source IP/epoch,
claimed destination and a three-second deadline. Status goes to known peers on
50002; beats/handoff use the Ethernet-bound 50001 socket. No local library is
advertised. The shared `deck_net_sync` follower derives from frozen donor
428b97dd under the root MIT license, with periodic hard resync removed.
Network snapshots never own audio state: deck_core applies output through the
nonblocking audio-session guard. Clock/latency are value copies, with no socket
or filesystem operation in the audio task. See package K's validation record.
