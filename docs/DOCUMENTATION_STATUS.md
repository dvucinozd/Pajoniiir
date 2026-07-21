# Documentation Status

Last full audit: **2026-07-21** after the matching `RC1-168-gb69f1b19` rollout
that carries the unified ANLZ metadata loader, the structured microSD service
journal and the accepted master-output recorder.

This page explains which documents describe the current product and which are
historical design or validation records. Three states must not be conflated:

- **last matching signed OTA rollout baseline:** `RC1-168-gb69f1b19` on both
  processors, signed with key ID `rel-001`, with P4 and S3 both on `ota_0` after
  the 2026-07-21 rollout (S3 reported `valid`, independently confirmed through
  P4's nested firmware report);
- **current bench state:** boards are **no longer matched** — the P4 runs
  `RC1-175-ge40b7225` from `ota_0` (recorder service-journal events, host
  test-runner repairs, the redesigned web controller and the httpd
  `lru_purge_enable` fix) while the S3 remains on `RC1-168-gb69f1b19`;
- **fully functionally hardware-accepted:** `RC1-123-g587cd7a1`, accepted on
  2026-07-14 after positive updates, the rejection matrix, interrupted uploads,
  forced rollback and final UI/audio/controller smoke. RC1-168 is **not** a full
  functional acceptance.

RC1-168 adds, on top of the earlier code-review remediation and Beat FX
Flanger/Delay: the single-resolver ANLZ metadata path, the structured
`/sd/logs/system.log` service journal with its read-only
`GET /api/diagnostic-log`, the master-output microSD recorder with its Settings
control and guarded `/api/recording` API, and two fixes found on hardware (the
USB preload media gate is now released between chunks, and the httpd URI-handler
limit sits above the registered endpoint count). Accepted on hardware in this
build: the recorder functional `.wav` acceptance and the service journal.
Still open: the targeted Phase 20 rows, Flanger/Delay audio smoke and the
remaining E1A functional set.

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
| Effects | Beat FX Filter/Echo have recorded hardware acceptance; Flanger and Delay value `4` are software-tested and OTA-deployed, with focused physical audio/target/beat/depth smoke pending |
| OTA | ECDSA P-256 signed `.ddjota`, dual-slot update, rejection, interruption safety and forced rollback hardware-accepted on both targets 2026-07-14; matching `RC1-131-gc391e306` rollout boot/status-verified 2026-07-16; paired `RC1-133-gbd5e43ce` bundles and manifest verified 2026-07-17, with only the exact P4 payload deployed by wired flash |
| Profiles | SD loading, registry matching and S3 transfer are hardware-verified with FLX4; atomic web overwrite/rescan/reactivation is deployed in `RC1-131-gc391e306` and still awaits its dedicated hardware acceptance |

## Remaining work

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
