# Bounded Pro DJ Link application model

`dj_link_discovery` is a single-owner, heap-free discovery and dual-player claim
model. Claim stages/timing and the 5/6 keep-alive compatibility byte derive from
kayrozen/Pajoniiir `428b97dd4a175f03d3a172c8db9c4d5ed94195fb`,
`firmware/main-deck-jc1060/components/dj_link/dj_link_session.c` (MIT).
The associated codec and its license are preserved in `../djlink`.

Local changes replace independent sessions with one two-player admission
decision. Fewer than two free numbers leaves both decks in observer mode.
Any conflict withdraws both identities and starts a new claim. Names are distinct
but share the board's real MAC/IP. Peer snapshots include reconnect/replacement
epochs, copied status/beat/position and bounded counters. No library is advertised,
no audio position extrapolated, no packet I/O or filesystem/UI access occurs.

Epochs are session-local runtime identity, never persistent cache keys.
Remote media/file identity verification and common load admission belong to J.
