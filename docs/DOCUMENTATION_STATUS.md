# Documentation Status

Last full status reconciliation: **2026-07-26**. The repository baseline
entering this audit is `RC1-257-g42b741a`; it contains the Pajoniiir rebrand and
the synchronized host-simulator support. That source is newer than the firmware
currently installed on the boards.

This page explains which documents describe the current product and which are
historical design or validation records. Three states must not be conflated:

- **current repository source:** `RC1-257-g42b741a` before the release-gate
  repair from this audit; this source has not yet been packaged or deployed;
- **last known bench state:** P4 and S3 were rematched at
  `RC1-254-g21f21963` on 2026-07-24 after pull OTA was proven end to end on the
  P4; the boards match each other but are behind current source;
- **fully functionally hardware-accepted:** `RC1-123-g587cd7a1`, accepted on
  2026-07-14 after positive updates, the rejection matrix, interrupted uploads,
  forced rollback and final UI/audio/controller smoke. Later releases have
  extensive focused acceptance but have not replaced this full-system baseline.

Since RC1-168 the product added and hardware-tested the single-resolver ANLZ
metadata path, the structured
`/sd/logs/system.log` service journal with its read-only
`GET /api/diagnostic-log`, Beat FX headroom fixes, corrected loop timing, the
idle screensaver and P4 pull OTA through a temporary STA visit. The
master-output recorder and guarded `/api/recording` API remain in the tree but
are **compiled out by default since 2026-07-24** because card-level latency
stalls failed the long soak. Still open: the targeted Phase 20/E1A rows,
remote-profile replacement/recovery, enclosure endurance and production
hardening.

## Source-of-truth order

When documents disagree, use this order:

1. current firmware, host tests and build configuration;
2. active operational documents: `ARCHITECTURE.md`, `CONTROL_LINK_PROTOCOL.md`,
   `DDJ_FLX4_MIDI_MAP.md`, `HARDWARE_WIRING.md`, `OTA-UPDATE.md`,
   `CONTROLLER_PROFILE_UPDATE.md`, `POST_R5_PLAN.md` and
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
| Vinyl | Forward/reverse scratch, paused/CUE scratch, loop wrapping and release/re-grab; canonical-only scratch storage and final dual-deck stress hardware-validated 2026-07-14 |
| Master Tempo | P4 key-lock callback and Overview `MT` control implemented; basic hardware behavior accepted 2026-07-12 |
| Audio | PCM5102A RCA MAIN plus simultaneous FLX4 USB headphone cue via the P4-to-S3 PCM link |
| Media | FAT32/exFAT on superfloppy, MBR and GPT USB layouts |
| UI | Overview, Library, Hot Cues and Settings tabs; stopped-deck VU meters decay to zero; DSI-synchronised 49.981 Hz dual-waveform path passed the 132-second development smoke and a more-than-71-second exact signed-candidate COM15 re-smoke on 2026-07-17 with no underrun, visible flash, watery motion or jitter |
| Effects | Beat FX Filter/Echo/Flanger/Delay all have recorded hardware acceptance as of 2026-07-24 (Flanger re-tuned; Echo/Delay confirmed as-is). A measured headroom defect in all three - wet added on unity dry peaks at up to 3.34x and hard-clipped inside the effect - was fixed with a soft knee in `RC1-223-gdfa619a9` |
| OTA | ECDSA P-256 signed `.ddjota`, dual-slot update, rejection, interruption safety and forced rollback hardware-accepted on both targets 2026-07-14; both boards last matched at `RC1-254-g21f21963` on 2026-07-24; P4 pull OTA AP→STA→HTTPS download→signature verify→inactive-slot flash→reboot is proven end to end |
| Profiles | SD loading, registry matching and S3 transfer are hardware-verified with FLX4; atomic web overwrite/rescan/reactivation is deployed in `RC1-131-gc391e306` and still awaits its dedicated hardware acceptance |

## Remaining work

- define production key provisioning and rotation beyond the current backed-up
  `rel-001` development key, preferably with encrypted or hardware-backed
  signing;
- perform enclosure power, thermal, RF and long-duration audio soaks;
- run longer simultaneous dual-deck key-lock quality/CPU testing;
- close the detailed E1A Beat FX transition/timing/target rows that go beyond
  the recorded per-effect sound and headroom acceptance;
- run the Phase 20 USB queue-pressure/recovery, guarded web-mutation and UART
  integrity acceptance set;
- validate a first non-FLX4 controller profile;
- hardware-accept web profile overwrite, corrupt/interrupted rejection,
  automatic S3 reactivation and reboot persistence;
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
