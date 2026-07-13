# Documentation Status

Last full audit: **2026-07-13** on `master` at `27150de0`.

This page explains which documents describe the current product and which are
historical design or validation records. The hardware-accepted release at the
time of the audit is **`RC1-106-g717b6ab3`** on both processors, running from
`ota_0` in `valid` state.

## Source-of-truth order

When documents disagree, use this order:

1. current firmware, host tests and build configuration;
2. active operational documents: `ARCHITECTURE.md`, `CONTROL_LINK_PROTOCOL.md`,
   `DDJ_FLX4_MIDI_MAP.md`, `HARDWARE_WIRING.md`, `OTA-UPDATE.md` and
   `STARTUP_CHECKLIST.md`;
3. dated validation records under `validation/`;
4. dated design records under `superpowers/specs/`;
5. imported or vendor reference material under `reference/`.

Dated design records are intentionally retained. They explain why a feature
was built, but their original pending tasks do not override the current status
in the active documents.

## Verified product snapshot

| Area | Current state |
| --- | --- |
| Controller | Pioneer DDJ-FLX4 enumerates on the S3 USB host; input mapping and P4-owned LED feedback are operational |
| Playback | Two independent P4 decks, Rekordbox library, MP3/WAV/FLAC, hot cues, loops, beat jump, sync and mixer controls |
| Vinyl | Forward/reverse scratch, paused/CUE scratch, loop wrapping, release/re-grab and dual-deck stress hardware-validated 2026-07-11 |
| Master Tempo | P4 key-lock callback and Overview `MT` control implemented; basic hardware behavior accepted 2026-07-12 |
| Audio | PCM5102A RCA MAIN plus simultaneous FLX4 USB headphone cue via the P4-to-S3 PCM link |
| Media | FAT32/exFAT on superfloppy, MBR and GPT USB layouts |
| UI | Overview, Library, Hot Cues and Settings tabs; stopped-deck VU meters decay to zero |
| OTA | Dual-slot update/rollback hardware-accepted 2026-07-13; ECDSA P-256 `.ddjota` verification implemented and host/build-verified, with signed-path hardware acceptance pending |
| Profiles | SD-card controller-profile loading, registry matching and S3 transfer implemented and hardware-verified with the FLX4 profile |

## Remaining work

- hardware-accept the signed OTA transition and define production key custody,
  provisioning and rotation beyond the current `rel-001` development key;
- perform enclosure power, thermal, RF and long-duration audio soaks;
- run longer simultaneous dual-deck key-lock quality/CPU testing;
- validate a first non-FLX4 controller profile;
- complete only the still-pending hardware rows identified in the MIDI and
  validation documents.

The one-time Deck 1 pad sweep observed for one initial load ordering is tracked
as a low-priority cosmetic indication. Routing, playback and pad state were
correct and no stack/audio fault accompanied it.

## Non-Markdown artifacts in `docs/`

| File | Purpose | Treatment |
| --- | --- | --- |
| `images/p4.jpg` | P4 board photograph | Current repository asset |
| `images/overview.jpg` | Overview UI screenshot | Illustrative; live UI may contain newer details |
| `images/library.jpg` | Library UI screenshot | Illustrative |
| `images/settings.jpg` | Settings UI screenshot | Illustrative; OTA/status controls may be newer |
| `reference/Pioneer-DDJ-FLX4.midi.xml` | Mixxx FLX4 mapping | Authoritative address seed; not runtime behavior |
| `reference/DDJ-FLX4_MIDI_message_List_E1.pdf` | Pioneer MIDI reference | Vendor reference, preserved unchanged |

Binary and vendor artifacts are intentionally not rewritten during
documentation-only audits.
