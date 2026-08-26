# Documentation Status

Last full status reconciliation: **2026-08-22**. The `migration/esp-idf-6.0.2`
branch has been **merged into `master`** and deleted; there is no separate
migration head any more. `master` now builds only under **ESP-IDF v6.0.2**
(`firmware/*/main/idf_component.yml` pins `idf: "==6.0.2"`) and carries the
bounded compressed audio cache, paginated Library UI, immutable track sort,
recorder safety hardening and the full `fix/release-blockers-and-concurrency`
stabilisation set.

Latest read-only source audit: **2026-08-16**, at
`10c91c2aa536be3852cdd6a41e831088d85625d7`. It is tracked in
[`CODE_REVIEW_REMEDIATION_20260816.md`](CODE_REVIEW_REMEDIATION_20260816.md)
and adds six open P1 release blockers plus
the ordered P2/P3 remediation plan. That audit is the current code-review gate
list; it does not supersede dated build or hardware validation evidence.

Because that is a different build baseline, the release prefix moved from `RC1`
to **`RC2`**: the annotated tag `RC2` sits on `56905c89` and the latest clean
dual-target release build is `RC2`, recorded in
`validation/CLEAN_RELEASE_RC2_BUILD.md`. Builds after the tagged commit report
`RC2-<distance>-g<hash>`.

Hardware acceptance is in progress — see
`migration/ESP_IDF_6_0_2_MIGRATION.md` and
`validation/P4_IDF6_SDMMC_SMOKE_20260802.md` plus
`validation/S3_IDF6_WIRED_FLASH_20260802.md` and
`validation/RC2_FOCUSED_FUNCTIONAL_SMOKE_20260802.md`. A later P4-only signed
OTA deployment is recorded in
`validation/RC2_51_P4_OTA_DEPLOYMENT_20260822.md`. The focused migrated
smoke now passes display/touch/Library, FLX4 MIDI/LED, MAIN/headphone audio and
real-MP3 playback. The RC2 line is still **not** release-qualified because the
real WAV/FLAC, sustained USB/cache, recovery and fault-injection rows remain;
the 2026-08-22 P4 run specifically required a physical USB reinsert after the
post-OTA enumerator exhausted its fast recovery cycles.

Feature-branch addendum (2026-08-09): the experimental
`feat/p4-dual-usb-host` image at `fc03034` was wired-flashed to the P4, booted
cleanly and loaded 191 Rekordbox tracks through the USB0 storage path. This is
partial bring-up evidence only; direct USB1 FLX4 enumeration, P4-local
MIDI/LED behavior, independent recovery and the dual-active soak remain open.
See `validation/P4_DUAL_USB_INITIAL_WIRED_SMOKE_20260809.md`.

Feature-branch follow-up (2026-08-12): a later bench power arrangement allowed
USB0 storage and the direct USB1 FLX4 path to enumerate together. The stick
mounted a 191-track library and the controller reached USB-MIDI ready as
VID:PID `2B73:0045`. A disconnect/recycle panic was captured, reduced to an
upstream hub child-removal race and fixed in the pinned `esp-usb` fork. The
complete P4 host suite and an ESP-IDF v6.0.2 feature build pass.

That does **not** clear the electrical gate. In the controlled isolation run a
single deck played for 33 seconds with zero reported late blocks or underruns,
but two decks caused a raw `BROWNOUT` reset after about 6.5 seconds even with
the FLX4 disconnected. The branch remains non-mergeable until the 5 V/VBUS
delivery path is measured and stabilised, then the full dual-device matrix is
repeated. See `validation/P4_DUAL_USB_RUNTIME_SMOKE_20260812.md` and the updated
`migration/P4_DUAL_USB_NEXT_SESSION.md`.

Feature-branch software follow-up (2026-08-27): the direct USB1 FLX4 path now
includes four-channel 44.1 kHz UAC output, exact 48→44.1 kHz resampling,
bounded ring clock correction, MIDI session generations, transition/active
task priorities, FLX4-gated shifted LED mirrors and S3 monitor-link fallback.
The same work adds headphone gain ramps, three Beat Jump pages, jog loop
boundary adjustment, gapless slip-reverse Censor and rate-limited UAC health
alarms. The complete P4 host suite and the ESP-IDF 6.0.2 P4-local feature build
pass. No new physical acceptance is claimed; the electrical blocker and all
installation/reconnect/audio-quality soak rows remain open.

This page explains which documents describe the current product and which are
historical design or validation records. Four states must not be conflated:

- **latest clean tagged release artifacts:** `RC2` (`56905c89`, built with
  ESP-IDF 6.0.2 on 2026-07-30; see
  `validation/CLEAN_RELEASE_RC2_BUILD.md`). The images were signed, packaged and
  later installed through OTA on both boards. That deployment is not full
  hardware acceptance;
- **previous release line:** `RC1-259-gdaf4639` (ESP-IDF 5.5.4, 2026-07-26).
  Superseded; `RC1` is closed and no further `RC1-*` builds are expected;
- **current bench state:** P4 accepted the signed `RC2-51-g050ab43` bundle on
  2026-08-22 and COM15 confirmed `ota_0`, `valid`, the ESP-IDF v6.0.2
  bootloader and a mounted 29,520 MB SDHC card. The already-inserted USB medium
  exhausted eight fast recovery cycles, then mounted as exFAT and loaded 324
  tracks after physical reinsertion. S3 was not updated and the P4 control link
  reported it as `RC2-44-g1923a3b`, `ota_1`, `valid`. The earlier 2026-08-02
  focused functional smoke remains the latest evidence for P4
  UI/Settings/Library, FLX4 MIDI/LED and both product audio outputs; the
  attempted WAV row was a missing file in the PDB rather than a decoder run;
- **fully functionally hardware-accepted:** `RC1-123-g587cd7a1`, accepted on
  2026-07-14 after positive updates, the rejection matrix, interrupted uploads,
  forced rollback and final UI/audio/controller smoke. Later releases have
  extensive focused acceptance but have not replaced this complete-system baseline.

Since RC1-168 the product added and hardware-tested the single-resolver ANLZ
metadata path, the structured
`/sd/logs/system.log` service journal with its read-only
`GET /api/diagnostic-log`, controller disconnect and control-link CRC/gap
summaries, `/api/status` control-link/service-log health, Beat FX headroom
fixes, corrected loop timing, the
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

[`LIBAPTA_P4_INTEGRATION_PLAN.md`](LIBAPTA_P4_INTEGRATION_PLAN.md) is an active
future implementation plan, not a description of current firmware. Until its
entry and acceptance gates pass, the verified product snapshot below and the
existing Rekordbox/PDB/ANLZ path remain authoritative.

## Verified product snapshot

| Area | Current state |
| --- | --- |
| Controller | Pioneer DDJ-FLX4 enumerates on the S3 USB host; input mapping and P4-owned LED feedback are operational and were operator-confirmed again on the migrated RC2/IDF6 pair 2026-08-02 |
| Playback | Two independent P4 decks, Rekordbox library, MP3/WAV/FLAC, hot cues, loops, beat jump, sync and mixer controls; compressed audio uses a bounded LRU page cache (8 × 32 KiB per deck) instead of whole-file PSRAM allocation. Focused real-MP3 playback passed on RC2; real WAV/FLAC remain untested because those physical files were missing from the audited USB export |
| Vinyl | Forward/reverse scratch, paused/CUE scratch, loop wrapping and release/re-grab; canonical-only scratch storage and final dual-deck stress hardware-validated 2026-07-14 |
| Master Tempo | P4 key-lock callback and Overview `MT` control implemented; basic hardware behavior accepted 2026-07-12; deterministic five-minute simultaneous dual-deck host soak passed 2026-07-26 with zero source drift, detected clicks or clipping |
| Audio | PCM5102A RCA MAIN plus simultaneous FLX4 USB headphone cue via the P4-to-S3 PCM link; both paths were operator-confirmed on the migrated RC2 pair 2026-08-02, while the numeric sustained-link soak remains separate |
| Media | FAT32/exFAT on superfloppy, MBR and GPT USB layouts; immutable track records with compact double-buffered sort order. P4 ESP-IDF 6.0.2 now reuses ESP-Hosted's initialized SDMMC controller for microSD slot 0; a repaired 59,688 MB exFAT SDHC card mounted in 4-bit mode in the 2026-08-02 focused smoke |
| UI | Overview, Library, Hot Cues and Settings tabs; Library table is paginated (one 8-row page with PREV/NEXT, max 40 live LVGL cells); stopped-deck VU meters decay to zero; a pinned headless LVGL gate now drives real button callbacks and locks exact 800×480 screenshots for D1/D2, all tabs and the screensaver restore path; DSI-synchronised 49.981 Hz dual-waveform path passed the 132-second development smoke and a more-than-71-second exact signed-candidate COM15 re-smoke on 2026-07-17 with no underrun, visible flash, watery motion or jitter. Display, touch, PSRAM-backed UI, Settings SD-online state and paginated Library were operator-confirmed again on RC2/IDF6 on 2026-08-02 |
| Effects | Beat FX Filter/Echo/Flanger/Delay all have recorded hardware acceptance as of 2026-07-24 (Flanger re-tuned; Echo/Delay confirmed as-is). A measured headroom defect in all three - wet added on unity dry peaks at up to 3.34x and hard-clipped inside the effect - was fixed with a soft knee in `RC1-223-gdfa619a9` |
| OTA | ECDSA P-256 signed `.ddjota`, dual-slot update, rejection, interruption safety and forced rollback hardware-accepted on both targets 2026-07-14; both RC2 applications installed successfully through OTA on 2026-08-02. A P4-only `RC2-51-g050ab43` push on 2026-08-22 reached `ota_0 / valid`, while S3 remained on `RC2-44-g1923a3b`; this did not repeat the negative matrix or full functional smoke. Pull OTA is hardware-proven and software-hardened with newer-only policy, offer expiry, channel hash/size checks, strict relative paths, mDNS and dynamic Host validation. OTA does not replace the bootloader, so the IDF 6 boot chain requires a wired flash per target |
| Profiles | SD loading, registry matching and S3 transfer are hardware-verified with FLX4; `generic_midi_ci` and the official-specification-derived Hercules Inpulse 500 profile are compile/registry/runtime/LED host-tested, with Hercules P4 Sync Off/autoloop behavior covered; atomic web overwrite/rescan/reactivation and all non-FLX4 physical/audio paths still await hardware acceptance |

## Remaining work

- close the P1 and applicable P2 gates from
  [`CODE_REVIEW_REMEDIATION_20260816.md`](CODE_REVIEW_REMEDIATION_20260816.md)
  before claiming a new release candidate
  is hardware-accepted or production-ready;
- **ESP-IDF 6.0.2 hardware acceptance**: display/touch, paginated Library,
  FLX4 MIDI/UAC, PCM5102A and the focused monitor path now pass. USB MSC
  recovery, sustained monitor/cache counters, ESP-Hosted AP/OTA and migrated
  rollback coverage still must pass before RC2 can be release-qualified;
- re-export real WAV/FLAC fixtures, verify they physically exist under
  `Contents`, then hardware-validate bounded cache under sustained dual-deck
  MP3/WAV/FLAC load;
- define production key provisioning and rotation beyond the current backed-up
  `rel-001` development key, preferably with encrypted or hardware-backed
  signing;
- perform enclosure power, thermal, RF and long-duration audio soaks;
- run longer simultaneous dual-deck key-lock quality/CPU testing on P4
  hardware; the five-minute deterministic PC regression is complete but does
  not measure device deadlines or listening quality;
- close the detailed E1A Beat FX transition/timing/target rows that go beyond
  the recorded per-effect sound and headroom acceptance;
- run the Phase 20 USB queue-pressure/recovery, guarded web-mutation and UART
  integrity acceptance set;
- hardware-validate the host-qualified Hercules Inpulse 500 profile, including
  descriptor identity, two-deck MIDI/LED/reconnect behavior, RGB pads and
  four-channel USB audio, before advertising non-FLX4 compatibility;
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
