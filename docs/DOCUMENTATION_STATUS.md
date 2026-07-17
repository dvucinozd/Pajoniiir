# Documentation Status

Last full audit: **2026-07-17** after the P4 DSI-synchronised Overview
waveform implementation and focused hardware smoke.

This page explains which documents describe the current product and which are
historical design or validation records. Three states must not be conflated:

- **signed release baseline:** `RC1-131-gc391e306` on both processors, signed
  with key ID `rel-001`, with P4 recorded on `ota_1` and S3 on `ota_0 / valid`
  after the 2026-07-16 rollout;
- **current bench state:** P4 factory-slot development build
  `RC1-132-g2b0cfd59-dirty`, wired-flashed on 2026-07-17 for the
  DSI-synchronised waveform test; S3 remains on signed RC1-131. See the
  [focused display validation](validation/P4_OVERVIEW_DSI_SYNC_SMOKE_20260717.md);
- **fully functionally hardware-accepted:** `RC1-123-g587cd7a1`, accepted on
  2026-07-14 after positive updates, the rejection matrix, interrupted uploads,
  forced rollback and final UI/audio/controller smoke.

RC1-131 includes the code-review remediation and Beat FX Flanger and Delay. Its
signed update and mandatory startup paths passed, but the targeted Phase 20 and
Flanger/Delay hardware-smoke rows remain open. The newer P4 development build
has only focused display-path acceptance and must not be treated as a release.

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
| UI | Overview, Library, Hot Cues and Settings tabs; stopped-deck VU meters decay to zero; DSI-synchronised 49.981 Hz dual-waveform path passed a focused 132-second COM15 smoke on 2026-07-17 with no underrun, visible flash or watery motion |
| Effects | Beat FX Filter/Echo have recorded hardware acceptance; Flanger and Delay value `4` are software-tested and OTA-deployed, with focused physical audio/target/beat/depth smoke pending |
| OTA | ECDSA P-256 signed `.ddjota`, dual-slot update, rejection, interruption safety and forced rollback hardware-accepted on both targets 2026-07-14; matching `RC1-131-gc391e306` rollout boot/status-verified 2026-07-16 |
| Profiles | SD loading, registry matching and S3 transfer are hardware-verified with FLX4; atomic web overwrite/rescan/reactivation is deployed in `RC1-131-gc391e306` and still awaits its dedicated hardware acceptance |

## Remaining work

- commit, package and sign the P4 DSI-synchronised waveform change as a clean
  release candidate, then repeat the focused display smoke on that exact image;
- define production key provisioning and rotation beyond the current backed-up
  `rel-001` development key, preferably with encrypted or hardware-backed
  signing;
- perform enclosure power, thermal, RF and long-duration audio soaks;
- run longer simultaneous dual-deck key-lock quality/CPU testing;
- hardware-smoke Beat FX Flanger and Delay selection/audio, Delay beat timing
  and non-continuous resync behavior, Level/Depth, target routing, tails and
  Echo/Delay mode changes;
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
