# P4 Risk Register

Status: **active P4-only register, reconciled 2026-09-29**.

| Priority | Risk | Current evidence | Required disposition |
| --- | --- | --- | --- |
| OPEN / physical NOT RUN (K) | Network LOCKED could be mistaken for audible phase qualification | Drift/jitter/loss/epoch/hold and handoff models, session-fenced mutations and UI status have automated coverage; no sink latency or real peer phase measurement exists | Keep Link default OFF. Measure each sink/rate and exact image, apply only that calibration (RAM-only), verify real CDJ/rekordbox handoff and listening. WAIT retains tempo; large errors never cause periodic seek. LOCKED alone is not acceptance |
| OPEN / physical NOT RUN (I/J) | JC1060 Link peer interoperability and worker/storage timing are unqualified | Ethernet-only discovery/browse/NFS, full-identity cache, interruption/integrity/A-B swap and gated SD loader regressions pass software tests; real localhost socket suites run ASan/UBSan | Keep Link default OFF; record real JC1060/CDJ/rekordbox/card models and versions. Verify SD removal/power-loss/full-card, concurrent decode/output/USB deadlines and active PSRAM/stack reserves. Metadata-only or incomplete remote LOAD cannot replace a deck. No physical support claim follows from software closure |
| OPEN / conservative policy (J) | NFS attributes, PDB digest or same locator cannot prove a volume identity | J requires full audio/PDB proof plus a new random mount/session nonce; no cross-session hit occurs, including reboot/reconnect. A/B with identical attributes has automated isolation | Keep session-local reuse only while volume identity is unknown. Local cue edits cannot migrate across these sessions. Add durable reuse only with trustworthy export/media identity and revalidation; never substitute IP/player/track or size/mtime as identity |
| OPEN / physical NOT RUN | H runtime reserves and stack-sampling cost are unqualified | Single selected tree, bounded artwork/render ownership and allocation/stack diagnostics pass host/build/simulator tests; no candidate runtime measurements | Run matching network/USB idle and dual-playback resource gate; measure output deadlines including throttled self-stack scan, verify zero critical allocation failures and repeat exact-image listening/reconnect/180-minute soak. Recovery API lacks largest-block evidence: collect a real comparable baseline, never substitute zero or synthetic values |
| OPEN / hardware FAIL (H3 preview) | Optional dj_ui consumes internal heap needed by USB and HTTP task stacks | Installed `M2.4-49-gcf28290` shows the new UI but loses USB0/FLX4 and AP. SD boots 578-585 report `0xB008` HTTP task failure, about 15 KiB free internal heap and only a 5 KiB largest block after Wi-Fi startup attempts; power cycle does not restore it | Verified previous image restored on authorized COM15 with matching flash read-back; AP/API, USB0 mount and FLX4 MIDI/UAC presence recovered, controls confirmed by operator. Custom PSRAM-only LVGL allocator passes software/CI, physical effect NOT RUN. Retain the previous product design; do not enable preview by default |
| CLOSED (bench) | Common 5 V or downstream VBUS is unsafe, undersized or backfed | On 2026-09-11 the operator reported that all requested continuity, backfeed, voltage, drop and current measurements were ideal or inside their allowed limits, with no brownout/reset; on 2026-09-20 the operator confirmed this is the existing enclosure wiring | Preserve the accepted wiring; repeat and record numeric measurements after any wiring, supply or enclosure change |
| ACCEPTED LIMITATION (M2.4) | USB0/USB1 do not enumerate automatically after an OTA software reboot | The earlier matrix had passing K1/K2 and L1/L2 recovery, but both exact M2.3 and M2.4 push installations reproduced a boot with root power active and neither device enumerated. A full power cycle restored USB0 storage, FLX4 MIDI/UAC and clean playback on M2.4 boot 560 | Require a full power cycle after M2.4 update; reproduce and fix unattended dual-root recovery before claiming automatic post-OTA availability in a later release |
| ACCEPTED LIMITATION | USB1 FLX4 control/audio recovery loses USB0, repeats recovery or leaves controls latched | H1--H5, I1, I2 and jog-held J1 passed. I3--I5 and J2--J5 were explicitly waived by the operator on 2026-09-14 | Preserve the 8 accepted reconnect results; Groups I/J are closed and the untested held-control variants are accepted release-scope limitations |
| CLOSED (M2.1 baseline) | Sustained bounded-cache I/O blocks audio | Real WAV/FLAC natural EOF plus simultaneous MP3+FLAC and MP3+WAV windows passed with zero locked-read, PCM, late and BNA deltas and clean audio | Repeat after cache, decoder, filesystem or USB scheduling changes |
| CLOSED (M2.1 baseline) | Audio teardown, Master Tempo or combined DSP misses real P4 deadlines | Bounded WSOLA fixed the reproduced WDT; focused mixed-rate dual-Master-Tempo and the final 180.156-minute combined scratch/FX/MAIN/cue soak passed without strict fault delta or audible defect | Direct cycle-margin profiling remains uncaptured; repeat after audio/DSP scheduling changes |
| CLOSED | Lifetime idle UAC underflow is mistaken for active data loss | Session-scoped health passed focused transitions and the final multi-hour soak | Preserve session-scoped flags and regression coverage |
| CLOSED | First remote transport action is consumed only to dismiss the screensaver | Fixed and exact-image smoked after more than 120 seconds idle | Preserve remote queue semantics and authoritative state confirmation |
| CLOSED (maintenance candidate) | Two Rekordbox exports with the same raw numeric track ID share persistent Hot Cue state | On 2026-09-28 two independent one-track exports both exposed `track_key=1`; A recalled 11000 ms across remount and software reboot, B recalled 22000 ms after remount, both cues were cleared, and strict health evidence remained clean | Preserve full `media_persistent_id_t` storage keys and rerun the two-media gate after identity, PDB, persistent cue or media lifecycle changes |
| ACCEPTED LIMITATION (M2.4) | The duplicate raw track-ID result is not repeated on the exact tagged M2.4 binary | The hardware gate passed on `M2.2-37-g751d3c6`; on 2026-09-28 the operator explicitly accepted exact-release repetition as unrun and the guided rerun was stopped before any Hot Cue pad action | Do not report exact-M2.4 duplicate isolation as passed; rerun after identity, PDB, persistent cue or media lifecycle changes, or before a deployment that requires exact-image proof |
| ACCEPTED LIMITATION (M2.4) | Real Rekordbox cue A/C or loop slot/time import differs from the controller/application display | The direct comparison was not run. On 2026-09-28 the operator explicitly removed it from the current release scope because it is not relevant to the deployed workflow | Do not report the comparison as passed; rerun it if cue import fidelity, ANLZ parsing or loop persistence becomes release-critical or changes |
| CLOSED (M2.4) | Pull OTA cannot validate the production edge certificate chain | Exact M2.3 reproduced `ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED`; M2.4 enables ESP-IDF cross-signed certificate-bundle verification while keeping CA/hostname validation. Exact M2.4 then completed live pre-public and post-public probes against the production HTTPS origin | Preserve the active Kconfig regression and repeat a live production pull probe after TLS, CA bundle, OTA client or hosting-chain changes |
| CLOSED (M2.2 baseline) | Interrupted push OTA or web mutation corrupts product state | M2.1 recovery evidence remains applicable; M2.2 additionally passed marked authoritative SYNC, interrupted-upload rejection, signed opposite-slot installation and embedded web UI smoke | Repeat the relevant matrix after OTA upload, web mutation or partition-layout changes |
| ACCEPTED LIMITATION | Final enclosure changes power, temperature, RF or service access | Operator reports the unit has operated in its current intended enclosure for approximately two months and confirms wired recovery access; dedicated numeric thermal/RF/strain evidence was not captured | Preserve the current topology and repeat qualification after any enclosure, wiring, supply or RF-layout change |
| ACCEPTED LIMITATION | Shared service credential permits nearby disruption | Operator selected one shared service password; WPA2/WPA3 transition mode advertises PMF capability; firmware updates remain signature-protected | Verify association from the actual service client on the final image; restrict distribution/exposure and revisit per-device credentials or WPA3-only PMF-required mode if deployment expands |
| CLOSED FOR M2.4 / DEFERRED | Signing key is lost or cannot rotate | Operator confirmed encrypted offline primary and separately stored encrypted backup copies for `rel-001` on 2026-09-20; M2.4 retains that key and the successor-key-before-retirement rotation boundary is documented. Untested backup recovery signing remains accepted deferred maintenance | Before a planned key rotation, verify backup recovery without exposing key material and implement multi-key overlap; emergency replacement retains wired recovery |
| ACCEPTED LIMITATION (M2.4) | Physical access can bypass software-only trust because hardware-rooted protection is absent | There is no spare P4 board; Secure Boot, Flash Encryption and security eFuses remain disabled so the only unit and its wired recovery path are not put at irreversible risk | Preserve controlled physical access, closed enclosure, signed OTA and wired recovery; qualify RSA-PSS Secure Boot v2 plus release-mode Flash Encryption on a dedicated pilot before any later provisioning |
| P2 | A non-FLX4 profile is advertised without real hardware evidence | Host fixtures pass only | Keep non-FLX4 support out of first-release claims until physical descriptor/MIDI/LED/UAC acceptance |
| P3 | Recorder or SD idle workaround ships on an unqualified card | G allows only explicit experimental recorder builds; both experiments remain off in ordinary builds, and production packaging rejects experiment flags | Keep release disabled until exact-card SD A/B, fault injection, listening and 180-minute strict-counter qualification pass; G physical gates NOT RUN |

## Release rule

The [first ordinary H candidate](validation/JC4880_H_CANDIDATE_20261004.md)
restored AP/USB and operator-confirmed previous design/controls, but resource
acceptance failed: two 254,976-byte waveform internal-DMA probes and HTTP stack
reserve 456 bytes. A focused PSRAM ownership correction is software checked;
installed-image resource acceptance, matching baseline, active sound/timing and
final soak remain blocking gates. Do not infer acceptance from startup `ready`.
The installed `M2.4-61-gc4912d5b` clears failed-allocation/HTTP-stack and absolute
largest-block failures in the empty/two-paused-track scenarios through ordinary
PSRAM LVGL and bounded buffer ownership. Full comparison still lacks the recovery
largest block; active audio/timing, listening and final soak remain unaccepted.

Development package E adds a JC1060 target and separate build/lock/CI, sharing
the P4 core. [E evidence](validation/FORK_IMPROVEMENTS_PACKAGE_E_SOFTWARE_20261004.md)
is software evidence only. Panel/touch/PSRAM, USB root topology, SD/Hosted
constructor interaction and RMII PHY/DHCP are unqualified. PCM5102A must remain
disabled because its pins overlap Ethernet; ES8311 is disabled for USB pacing.
[F software evidence](validation/FORK_IMPROVEMENTS_PACKAGE_F_SOFTWARE_20261004.md)
does not qualify DDJ-400 MAIN/cue. Four-channel full-speed UAC1 adaptive/synchronous
16-bit or packed 24-bit formats are bounded; feedback, UAC2/high-speed and 24-in-32
are unsupported. JC1060's larger periodic FIFO and physical EP0 detach/reconnect
must be measured on hardware. Sink latency, CPU deadline, internal/DMA heap and
stack reserves remain unmeasured. New-board hardware gates remain NOT RUN. No JC1060
image/channel may be represented as accepted or released on build evidence.

M2.4 is released with exact-image automated, hardware telemetry and acoustic
acceptance complete, subject to the listed post-OTA recovery limitation, and encrypted
primary/backup signing-key custody operator-confirmed. Untested backup recovery
signing is explicitly accepted as deferred future maintenance. The security
decisions themselves are recorded in
[`SECURITY_PROVISIONING_POLICY.md`](SECURITY_PROVISIONING_POLICY.md). A focused
smoke closes only the exact scenario it exercised; it must not be promoted to
unrelated acceptance.

For M2.4, duplicate raw track-ID isolation has passing candidate evidence on
`M2.2-37-g751d3c6`; its exact-tag repetition and the real Rekordbox cue A/C and
loop comparison are explicitly accepted as unrun. Exact-tag build, signed
installation, product smoke and publication verification remain mandatory
release steps.
