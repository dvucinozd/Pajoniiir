# Documentation Status

> **Feature-branch status (2026-09-02):** `feat/p4-dual-usb-host` now has one
> active firmware/release target: ESP32-P4. P4 directly hosts USB0 storage and
> USB1 FLX4 MIDI/audio. The S3 firmware target and dedicated tests are removed;
> dated validation remains for reference. S3 is absent from CMake, CI,
> UI/web status and OTA packaging. The focused 2026-08-29 signed OTA and USB0
> hotplug smoke passed while USB1 FLX4 stayed active. The 2026-09-01 exact clean
> candidate then passed signed OTA, idle, dual playback, one physical USB1 FLX4
> reconnect and post-reconnect dual playback while USB0 remained mounted. These
> close the focused reproductions, but not the VBUS/brownout, repeated-reconnect,
> remove-during-decode or long dual-active acceptance gates. The newer exact
> `RC2-111-g4ee76a6` candidate passed a strict 30-minute dual-active MP3
> seek/restart soak with zero gated audio/USB counter deltas. This closes the
> bounded 30-minute gate, not the multi-hour or electrical qualification.

Last full status reconciliation: **2026-09-02**. The `migration/esp-idf-6.0.2`
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
`validation/RC2_51_P4_OTA_DEPLOYMENT_20260822.md`. The later P4-only
remediation and hardware smoke are recorded in
`validation/P4_DUAL_USB_HOTPLUG_OTA_SMOKE_20260829.md`. The bounded USB1 fault
recovery implementation and exact-candidate smoke are recorded in
`validation/P4_USB1_FAULT_RECOVERY_OTA_SMOKE_20260901.md`. The
30-minute exact-image soak is recorded in
`validation/P4_EXACT_IMAGE_DUAL_DECK_SEEK_SOAK_20260902.md`. The focused migrated
hardware path now passes display/touch/Library, FLX4 MIDI/LED, MAIN/headphone audio and
real-MP3 playback. The RC2 line is still **not** release-qualified because the
real WAV/FLAC, sustained USB/cache, repeated recovery and fault-injection rows
remain. The 2026-08-29 run passed one hands-free post-OTA mount and one USB0
remove/reinsert cycle with USB1 FLX4 active. The 2026-09-01 run passed one USB1
remove/reconnect cycle with USB0 mounted plus post-reconnect dual playback; this
is still not the repeated matrix.

Historical feature-branch addendum (2026-08-09): the experimental
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
task priorities and FLX4-gated shifted LED mirrors.
The same work adds headphone gain ramps, three Beat Jump pages, jog loop
boundary adjustment, gapless slip-reverse Censor and rate-limited UAC health
alarms. The complete P4 host suite and the ESP-IDF 6.0.2 P4-local feature build
pass.

Feature-branch hardware follow-up (2026-08-29): the P4 accepted signed
`RC2-106-gfa55e43-dirty`, booted `ota_0`, mounted USB0 and activated direct
USB1 FLX4 MIDI/UAC. After physical USB0 removal/reinsertion, status remained on
the same boot and reported 2/2 mounts, clean unmount/uninstall and zero
host/recovery failures; the Library reloaded and two track loads completed.
The tested source was committed as `77aa23a`. This closes the focused hotplug
reproduction. The electrical blocker, repeated insertion-order/reconnect
matrix, audio-quality stress and long combined-load soak remain open.

Feature-branch USB1 recovery follow-up (2026-09-01): commit `269036b` bounds a
USB1 audio/controller fault to one recovery epoch, halts and flushes the failed
UAC path before teardown, cancels a pending soft recovery on physical
`DEV_GONE`, and prevents the prior high-rate recovery-request storm. A clean
ESP-IDF 6.0.2 build produced `RC2-109-g269036b`; its signed P4-only bundle was
independently verified, installed over OTA and booted `ota_0`. The exact image
passed 20 seconds idle, 30 seconds dual playback, one approximately 6.6-second
physical FLX4 removal/reconnect and 20 seconds post-reconnect dual playback.
USB0 remained mounted with the 100-track Library available, and the accepted
intervals added no host/controller recovery request, audio late/drop/overflow,
daemon-failure or UAC underrun counters. Repeated reconnect, removal during
decode, long combined load and electrical qualification remain open.

Feature-branch exact-image soak follow-up (2026-09-02): clean candidate
`RC2-111-g4ee76a6` on `ota_1` ran two real MP3 tracks for 1,800 seconds with
seven controlled seek-to-zero restarts. USB0 storage and direct USB1 FLX4
MIDI/UAC remained active on the same boot; audio late, PCM underrun, UAC
drop/overflow, recovery, disconnect and service-log drop deltas stayed zero.
This closes the bounded 30-minute dual-active gate. The measured electrical,
repeated reconnect/remove-during-decode, verified WAV/FLAC and multi-hour gates
remain open.

This page explains which documents describe the current product and which are
historical design or validation records. Four states must not be conflated:

- **latest clean tagged release artifacts:** `RC2` (`56905c89`, built with
  ESP-IDF 6.0.2 on 2026-07-30; see
  `validation/CLEAN_RELEASE_RC2_BUILD.md`). The images were signed, packaged and
  later installed through OTA on both boards. That deployment is not full
  hardware acceptance;
- **previous release line:** `RC1-259-gdaf4639` (ESP-IDF 5.5.4, 2026-07-26).
  Superseded; `RC1` is closed and no further `RC1-*` builds are expected;
- **current bench state:** P4 accepted signed clean `RC2-109-g269036b` on
  2026-09-01 and returned on `ota_0` with USB0 Rekordbox storage mounted and
  direct USB1 FLX4 MIDI/UAC active. One physical USB1 FLX4 remove/reconnect
  cycle passed without reboot while the 100-track Library remained available,
  followed by successful dual playback. The exact tested source is `269036b`.
  The earlier 2026-08-02 focused smoke remains the broader display/touch and
  listening baseline; the attempted WAV row was a missing file in the PDB
  rather than a decoder run;
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
| Controller | Pioneer DDJ-FLX4 enumerates directly on P4 USB1 as `2B73:0045`; P4-local MIDI mapping, profile activation and LED ownership are active. Commit `269036b` bounds USB1 controller/audio fault recovery to one epoch. The 2026-09-01 exact-candidate smoke passed one physical FLX4 remove/reconnect and post-reconnect dual playback without repeated recovery requests, audio drops or overruns; `RC2-111-g4ee76a6` then passed a 30-minute dual-active seek/restart soak. Repeated reconnect and reconnect-LED acceptance remain open |
| Playback | Two independent P4 decks, Rekordbox library, MP3/WAV/FLAC, hot cues, loops, beat jump, sync and mixer controls; compressed audio uses a bounded LRU page cache (8 × 32 KiB per deck) instead of whole-file PSRAM allocation. Focused real-MP3 playback passed on RC2; real WAV/FLAC remain untested because those physical files were missing from the audited USB export |
| Vinyl | Forward/reverse scratch, paused/CUE scratch, loop wrapping and release/re-grab; canonical-only scratch storage and final dual-deck stress hardware-validated 2026-07-14 |
| Master Tempo | P4 key-lock callback and Overview `MT` control implemented; basic hardware behavior accepted 2026-07-12; deterministic five-minute simultaneous dual-deck host soak passed 2026-07-26 with zero source drift, detected clicks or clipping |
| Audio | PCM5102A RCA MAIN plus direct four-channel FLX4 USB Audio on P4 USB1: MAIN channels 1/2 and cue/headphones channels 3/4, with exact-rational 48→44.1 kHz resampling and bounded ring correction. One 2026-09-01 direct-UAC physical reconnect plus post-reconnect dual playback passed; the 2026-09-02 exact image also passed 30 minutes with zero new audio late, PCM underrun or UAC drop/overflow counters. Repeated reconnect, listening-quality and multi-hour data-loss-counter soaks remain open |
| Media | FAT32/exFAT on superfloppy, MBR and GPT USB layouts; immutable track records with compact double-buffered sort order. USB0 uses storage-task reconciliation, indexed idle-only recovery, callback-safe teardown and fixed 8 KiB MSC transactions. One 2026-08-29 USB0 hotplug cycle remounted and reloaded a 100-track Library without reboot; USB0 also remained mounted through the 2026-09-01 USB1 reconnect and the 2026-09-02 30-minute dual-MP3 soak. Repeated removal-during-decode, verified WAV/FLAC and multi-hour cache stress remain open |
| UI | Overview, Library, Hot Cues and Settings tabs; Library table is paginated (one 8-row page with PREV/NEXT, max 40 live LVGL cells); stopped-deck VU meters decay to zero; a pinned headless LVGL gate now drives real button callbacks and locks exact 800×480 screenshots for D1/D2, all tabs and the screensaver restore path; DSI-synchronised 49.981 Hz dual-waveform path passed the 132-second development smoke and a more-than-71-second exact signed-candidate COM15 re-smoke on 2026-07-17 with no underrun, visible flash, watery motion or jitter. Display, touch, PSRAM-backed UI, Settings SD-online state and paginated Library were operator-confirmed again on RC2/IDF6 on 2026-08-02 |
| Effects | Beat FX Filter/Echo/Flanger/Delay all have recorded hardware acceptance as of 2026-07-24 (Flanger re-tuned; Echo/Delay confirmed as-is). A measured headroom defect in all three - wet added on unity dry peaks at up to 3.34x and hard-clipped inside the effect - was fixed with a soft knee in `RC1-223-gdfa619a9` |
| OTA | ECDSA P-256 signed P4-only `.ddjota`, dual-slot update, rejection, interruption safety and forced rollback. Signed clean `RC2-109-g269036b` installed successfully on 2026-09-01 and returned on `ota_0`; the older dual-target evidence is historical. Pull OTA is hardware-proven and software-hardened with newer-only policy, offer expiry, channel hash/size checks, strict relative paths, mDNS and dynamic Host validation. OTA does not replace the bootloader |
| Profiles | SD loading, registry matching and P4-local FLX4 activation are hardware-verified; `generic_midi_ci` and the official-specification-derived Hercules Inpulse 500 profile are compile/registry/runtime/LED host-tested, with Hercules P4 Sync Off/autoloop behavior covered. Atomic web overwrite/rescan/reactivation and all non-FLX4 physical/audio paths still await hardware acceptance |

## Remaining work

- close the P1 and applicable P2 gates from
  [`CODE_REVIEW_REMEDIATION_20260816.md`](CODE_REVIEW_REMEDIATION_20260816.md)
  before claiming a new release candidate
  is hardware-accepted or production-ready;
- **ESP-IDF 6.0.2 hardware acceptance**: display/touch, paginated Library,
  FLX4 MIDI/UAC, PCM5102A and the focused monitor path now pass. One USB0
  hotplug and one USB1 reconnect have passed independently while the other root
  stayed active; the repeated insertion-order/reconnect matrix,
  remove-during-decode, sustained monitor/cache counters, ESP-Hosted AP/OTA and
  migrated rollback coverage still must pass before RC2 can be release-qualified;
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
- repeat the Phase 20 direct USB queue-pressure/recovery matrix and run the
  guarded web/profile/OTA mutation acceptance set;
- hardware-validate the host-qualified Hercules Inpulse 500 profile, including
  descriptor identity, two-deck MIDI/LED/reconnect behavior, RGB pads and
  four-channel USB audio, before advertising non-FLX4 compatibility;
- hardware-accept web profile overwrite, corrupt/interrupted rejection,
  automatic P4-local reactivation and reboot persistence;
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
