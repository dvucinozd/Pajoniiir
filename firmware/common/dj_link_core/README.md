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

Epochs are session-local runtime identity, never persistent cache keys.
Remote media/file identity verification and common load admission belong to J.

`dj_link_db` is the frozen donor's sans-I/O DBServer client, adapted for explicit
source and per-connection epochs and a hard 2,000-row request limit. Transport
must pass the epoch captured for that connection to every connected/data/closed
event; old events are ignored after cancel/restart/port discovery. Result owners
must additionally check the live discovery source epoch before publication.
TCP framing uses the corrected codec's exact consumed prefix length. Malformed
menu fields, wrong transaction/setup IDs and premature render footers fail the
request. Folder/playlist menu requests always use source order.

The donor's IP/player/track FNV cache-key API was intentionally excluded; no
persistent identity or cache storage is provided here. Blob/path protocol helpers
are retained for later J integration, not enabled in the current runtime.

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
a deck. The LVGL admission path refuses unavailable downloads until package J.
