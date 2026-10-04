# esp-djlink

Standalone, reusable ESP-IDF component implementing a **complete pure-C99
codec for the Pioneer Pro DJ Link network protocol** (UDP ports
50000/50001/50002, the TCP remote DB server and NFSv2 media download).
The core has no ESP-IDF or lwIP dependency, so it builds on the host, on
any MCU, and inside an ESP-IDF project unchanged. Version 0.3.0.

Implemented:

- **Common framing**: magic header (`Qspt1WmJOL`), type dispatch, 20-byte
  device-name field helpers, big-endian scalar accessors, pitch/BPM
  conversions (raw pitch `0x00100000` = 0%, effective BPM from track BPM +
  pitch).
- **Port 50000 — discovery / channel claims**: keep-alive build (virtual CDJ
  announcement, 0x36 B), initial announcement (0x0a), first-stage MAC claim
  (0x00), second-stage IP claim (0x02, auto/specific assignment), final-stage
  claim (0x04), assignment-finished (0x05) build, channel-conflict (0x08)
  and generic claim-info parse.
- **Port 50001 — beat / sync**: beat packet (0x28) parse **and build**;
  absolute-position packet (0x0b, CDJ-3000+, playhead ms) parse **and
  build**; sync control (0x2a: sync on/off/become-master) build+parse; tempo
  master takeover request (0x26) and response (0x27) build+parse; fader
  start (0x02) and channels-on-air (0x03) build+parse.
- **Port 50002 — status / control**: CDJ status (0x0a) parse — activity,
  source device/slot, rekordbox ID, play state, master/sync/on-air flag
  bits, physical + effective pitch, BPM, beat counter, beat-in-bar —
  defensive across nexus/nxs2/CDJ-3000 length variants; mixer status (0x29)
  parse (master flag, master-handoff byte, BPM, beat-in-bar); load-track
  command (0x19) build+parse and acknowledgment (0x1a).
- **Media slots**: media query (0x05) build and media response/broadcast
  (0x06) parse **and build** — media name/date (UTF-16BE with UTF-8
  conversion helpers), track/playlist counts, UI color, track type, My
  Settings flag, capacity/free space.
- **Remote DB server (TCP)**: port-discovery query to 12523 + reply parse,
  setup greeting, full typed-field message codec (int8/16/32, binary,
  UTF-16BE string; argument-tag mapping), generic message build/parse, and
  ready-made builders for the query-context setup, rekordbox track metadata
  request and menu render request. Response type constants (success, menu
  header/item/footer, waveform, beatgrid, cues, artwork...) are provided.
- **NFSv2 media download (0.3.0)**: read-only client for the media a player
  exports (`/C/` USB, `/B/` SD) — ONC RPC v2 over UDP with AUTH_UNIX uid 0,
  portmapper GETPORT, MOUNT v1 MNT, NFSv2 LOOKUP (one path element per call,
  UTF-16LE names as players expect) and READ (512..8192 bytes, default 1024
  so a reply fits one Ethernet frame without IP reassembly). The fetch
  client is an I/O-free state machine: the application sends the datagrams
  it builds, feeds back replies and calls `djlink_nfs_poll()`; the file is
  streamed **in order** to a `write` callback (made for a cache file). It
  keeps a window of up to 8 READs in flight with a caller-provided reorder
  buffer, retransmits with the same xid and exponential backoff (250 ms,
  5 retries by default), resumes after a short read, and reports errors as
  short texts (`EXPORT REFUSED (2)`, `NOT FOUND: <name>`, `TIMEOUT READ`...).
  Server-side codec helpers (call parse, reply build, fattr encode) are
  exported for mocks. Behaviour follows RFC 1057/1094/1833 and what
  Deep Symmetry's crate-digger learned about players; no code from it.

Not implemented (deliberately out of codec scope): rekordbox
`export.pdb`/ANLZ parsing (the Pajoniiir P4 has its own parser), NFS
*server* (exporting our own media), and the socket/timer layer — keep all
I/O in the application; it differs per target (lwIP, BSD sockets, mocks).

## Fidelity notes

Layouts follow the Deep Symmetry "DJ Link Ecosystem Analysis" and were
cross-checked against the validated `python-prodj-link` struct definitions
(big-endian multi-byte values; CDJ status field offsets; keep-alive field
order). Two packet builders embed assumptions the analysis leaves open and
are flagged in their headers: the final-stage claim length (0x2a, four
unidentified trailing bytes sent as zero) and the assignment-finished packet
(minimal form). Hardware verification on real CDJ/mixer gear is still
pending; treat those two as best-effort until captured.

## Credits

This implementation would not exist without the outstanding reverse
engineering work of **James Elliott (brunchboy) / Deep Symmetry, LLC** in the
[dysentery](https://github.com/Deep-Symmetry/dysentery) project and its
companion guide, the
[DJ Link Ecosystem Analysis](https://djl-analysis.deepsymmetry.org/djl-analysis/).

All protocol knowledge encoded here was learned from that documentation
(packet layouts, byte offsets, magic header, pitch/BPM encodings, keep-alive
and virtual-CDJ mechanics, dbserver message structure). Thank you — consider
supporting Deep Symmetry via their GitHub Sponsors or Liberapay.

This repository contains no code from dysentery (Clojure/Java); it is an
independent C implementation. If you redistribute this component, please keep
this credit section intact.

## Integration (ESP-IDF)

```
idf.py add-dependency "kayrozen/esp-djlink"
```

or as a git submodule / `EXTRA_COMPONENT_DIRS` entry. Component name:
`esp-djlink`, no other component required.

```c
#include "djlink.h"

djlink_beat_t beat;
if (djlink_beat_parse(rx_buf, rx_len, &beat) == DJLINK_OK) {
    float pitch_pct = djlink_pitch_raw_to_percent(beat.pitch_raw);
    float bpm = djlink_effective_bpm(beat.bpm100, beat.pitch_raw);
    /* feed your sync engine / UI */
}

/* Virtual CDJ announcement (broadcast on port 50000 every ~2 s): */
djlink_keepalive_t ka = { .name = "Pajoniiir", .device_number = 4, ... };
djlink_keepalive_build(&ka, tx_buf, sizeof(tx_buf));
```

Fetching a file from a player over NFS (the owner drives the socket):

```c
static uint8_t window_buf[4 * 1024];
djlink_nfs_fetch_cfg_t cfg = {
    .host_ip = peer_ip, .export_path = DJLINK_NFS_EXPORT_USB,
    .path = "PIONEER/rekordbox/export.pdb",
    .window = 4, .window_buf = window_buf, .window_buf_len = sizeof(window_buf),
    .xid_seed = esp_random(),
};
djlink_nfs_io_t io = { .send = my_udp_send, .write = my_cache_write, .ctx = me };
djlink_nfs_fetch(&nfs, &cfg, &io, now_ms);
while (djlink_nfs_state(&nfs) == DJLINK_NFS_BUSY) {
    /* on every datagram: djlink_nfs_on_datagram(&nfs, buf, len, now_ms); */
    djlink_nfs_poll(&nfs, now_ms);
}
```

Players answer from ephemeral ports, so match replies by xid (the client
does) rather than by source port. Replies above ~1.4 KB are IP-fragmented:
keep `read_size` at 1024 unless the stack reassembles.

The socket/timer layer (binding UDP 50000/50001/50002, keep-alive timer,
device registry) is intentionally left to the application: it differs per
target (lwIP, BSD sockets, mocks). Keep all I/O outside this component.

## Host tests

```
make -C test/host && make -C test/host run
```

Two codec suites (`djlink_test`, `test2`) cover byte-exact roundtrips,
documented byte sequences from the analysis (context-setup message, claim
offsets, mixer status template, media response template), UTF-16 conversion,
and truncation/type-safety error paths.

`nfs_test` (POSIX sockets + pthread) checks the RPC/XDR codec, then runs
the fetch client against a mock player on 127.0.0.1 — portmapper, MOUNT and
NFS on three UDP sockets serving an in-memory tree, verifying AUTH_UNIX uid 0
and UTF-16LE names. It covers GETPORT, MNT, multi-level LOOKUP (including a
non-ASCII element), a > 1 MiB file with a lost request, a lost reply, a
duplicate and a reordered reply (content checked byte by byte), a short
read, 8 KiB reads with an 8-deep window, empty file, unknown export, missing
path element, file-as-directory, directory-as-file, size limit, refusing
sink, unregistered MOUNT, silent portmapper (timeout after backoff) and
cancel.

## Known issues (to report upstream)

Found while integrating 0.2.0 into Pajoniiir (v247). The application works
around both (0.3.0 only adds the NFS module).

- `djlink_load_track_t.target_player` (byte `0x28`) is misnamed. Per the
  djl-analysis "Loading Tracks" page, `0x28` is the player the track is
  loaded **from** (its source device `Dr`, with slot `Sr` at `0x29`). The
  player that must load it is the one the packet is unicast to. The
  `0x40` byte is the zero-based destination. Suggested rename:
  `source_player`.
- `djlink_keepalive_build()` leaves byte `0x35` at `0`. The CDJ-3000
  sets it to `0x64` when it uses player numbers 5/6. Pajoniiir patches the
  built packet for D > 4.
