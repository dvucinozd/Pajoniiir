# Donor provenance

Imported from kayrozen/Pajoniiir
`428b97dd4a175f03d3a172c8db9c4d5ed94195fb` (v323), directory
`firmware/main-deck-jc1060/components/djlink`.
MIT copyright/license and Deep Symmetry protocol credits are retained verbatim.
No donor application, UI, audio or cache identity code is imported here.

Local corrections: DB typed-field tags are checked, short/empty blobs and strings
are not read as integers, oversized/null builder payloads are rejected, and a
prefix parser reports consumed bytes for fragmented/coalesced TCP transport.
Host suites include donor codec tests and focused malformed-frame coverage.
The donor POSIX NFS fixture supplied a 110-byte name from a 100-byte remaining
buffer. ASan reproduced that fixture overread; the test now owns the full name
buffer and retains all codec capacity checks.
The donor NFS codec remains portable and read-only; this import starts no network
service or download and does not advertise this device's library as browsable.
