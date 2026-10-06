# Shared M3 dual-waveform regression, 2026-10-06

## Failed image and reproduction

Image `M3-dev-g11574f4eff16`, clean source
`11574f4eff162d74cdc9f2c17601fe2941139f3d`, M3 ota_1 VALID. Exact hashes are
in [the candidate record](M3_SHARED_CORE_11574_20261006.md).

The operator confirmed MT enabled on both decks and the required zoom setup.
D1 key 5 played a 44.1-kHz source at +5%, loop 30,000–32,400 ms; D2 key 139
played a 48-kHz source at -5%, loop 30,000–31,920 ms. PCM5102A output was 48 kHz,
FLX4 MIDI/UAC ready, USB3 catalog 324 tracks and Wi-Fi enabled.

The 60-minute TimingSoak attempt started at 20:48:03 Europe/Zagreb. The operator
reported both main waveforms looked as if moving through water, at approximately
eight visible beats. This is a hard physical failure under the campaign rules.
Playback was stopped after capturing status/resources/firmware and diagnostic
logs, and the monitor was interrupted. It captured 256 rows over 76.68 seconds,
254 with both decks playing. No completed monitor result or 60-minute PASS exists.
The two final stopped rows are the deliberate diagnostic stop.

At the failure snapshot, PCM underruns, UAC dropped/overflow frames, output-late
and service-log drops were all zero. The existing idle UAC underflow counter
remained 10,356,091 during active playback; this is not an absolute zero-underflow
claim. These counters do not override the operator's visual failure. No explicit
audio listening PASS was supplied.

The operator then confirmed:

- Solo D1 with the same MT/zoom: sharp and fluid.
- Dual playback with the continuous API monitor stopped: both waveforms still
  deform. Polling is therefore not a sufficient explanation.

The live web LOAD LOCK test also returned HTTP 409 on both playing decks and
preserved title/play state. That functional result is separate from visual failure.

Private raw evidence is under
`.cache/m3-migration/20261006-11574f4e/{timing-soak,operator-failure}`.
The earlier local delivery ZIP is an immutable preflight snapshot; this failure
record supersedes its physical NOT RUN/partial status without rewriting artifacts.

## Repair candidate

The common frame orchestrator already selects M3 waveform-first and dual
top-to-bottom policy. However, each deck's combined update performs added
status/loaded-track/artwork work before writing its waveform, then repeats the
same sequence for the other deck. Consequently those noncritical operations
still precede one or both PPA transfers despite the outer waveform-first flag.

The repair candidate uses one shared scheduler to execute both waveform phases
before either deck's chrome for boards requesting waveform-first. Other boards
retain their combined per-deck path. Interpolation runs once per deck per frame,
and chrome reuses that position. Dual redraw cadence and top-to-bottom order are
preserved. LVGL stack high-water scanning moves after frame/render work.

No DSI timing, lane, burst mode, pixel format, panel init, audio/DSP or waveform
geometry changes are included. The ordering correction is a candidate repair;
the precise physical timing cause and its effectiveness still require retest.

Software gates: production scheduler phase-order/budget/fairness tests, full
host qualification, reviewed simulator baselines and ESP-IDF builds. Physical
gates: solo/dual at all zooms, artwork/library/Wi-Fi load, clean MAIN/PFL audio,
then restart the full 60-minute soak on the new exact signed image.

The local full host qualification completed with exit 0 and no optional SKIP.
The extended production scheduler gate passed. M3, JC4880 and JC1060 simulator
presentations retained their existing screenshot hashes, including M3's five
zoom views. A local ESP-IDF 6.0.2 M3 compile and documentation integrity passed.
Clean committed builds, exact-source CI, signed packaging, installation and
operator retest are separate subsequent gates; no repair PASS is inferred here.

## Installed repair candidate

The new signed candidate is now installed and VALID in M3 `ota_0`:

- Version `M3-dev-gc698fa672bc1`.
- Clean source `c698fa672bc1122224b130e35721f8fdb350768b`.
- ELF SHA-256 `73f34062b4d791de891fa80191fa6cd2ca38915aa62ddb3f7957d056602e3029`.
- Image 2,566,160 B, SHA-256 `ae36858df995823d4c1046abd0a513e3ce4f219ae5518838961cc3453adc407d`.
- Bundle 2,566,348 B, SHA-256 `6e717a89e1859cd2a34e171c667d21a6aca4acce70cdb0cb9219259e2aeac2ad`.

All three clean-source ESP-IDF 6.0.2 ordinary builds and unchanged dependency
locks passed. All three signed packages passed candidate verification. The
exact-source [12-job matrix](https://github.com/dvucinozd/Pajoniiir/actions/runs/37516104001),
[USB gate](https://github.com/dvucinozd/Pajoniiir/actions/runs/37516104138) and
[documentation gate](https://github.com/dvucinozd/Pajoniiir/actions/runs/37516104057)
passed. A separate GitHub Advanced Security run on the preceding documentation
commit failed with HTTP 402/monthly quota; it did not complete a security review.

Signed OTA returned HTTP 200 and runtime confirmed the exact board/project/source/
ELF, VALID health and idle service. The 324-track catalog remounted and both
selected tracks again reached READY at 44.1/48 kHz. Opposing pitches and loops
were restored while stopped. MT reset on reboot and must be enabled by the
operator before the comparable retest. Wi-Fi remains enabled.

The operator enabled MT on both decks and selected eight visible beats. Both
decks were started with the same opposing pitches and 44.1/48-kHz loops, without
continuous API polling. The operator reported the watery deformation was still
present and that the image now also stuttered. Audio did not stutter according
to the operator. This candidate therefore also fails the physical visual gate.
Status/resources/firmware and diagnostic logs were captured before deliberately
stopping both decks. The new full 60-minute soak was NOT RUN.

At the failure snapshot, PCM underruns, output-late, UAC dropped/overflow frames
were zero and active UAC data-loss was false. The idle UAC underflow count was
447,174; it is not an absolute-zero claim. Allocation and critical allocation
failures were zero. These measurements do not overrule the visual failure.

The ordering correction alone is insufficient. The next candidate adds bounded
RAM-only scanout timing measurements exposed by read-only `/api/ui-timing`:
refresh wake latency, coalesced refreshes, frame intervals, Overview entry,
callback/handler duration and per-deck cache/blit/finish timing. No panel timing,
cadence or rendering policy is changed for this measurement step. Histograms
use upper bounds 1/2/4/8/12/20 ms and a final >20-ms bucket; deltas between
snapshots isolate each playback window from startup/load outliers.

Installation and software verification are not a physical repair PASS.
Raw installation and preflight evidence is private under
`.cache/m3-migration/20261006-c698fa67`. No public release/channel was modified.

## Installed measurement candidate and measured failure

Signed candidate `M3-dev-g2fa8a6373847`, clean source
`2fa8a6373847decad971a815e1bf6d817a8c5ad1`, is VALID in `ota_1` after HTTP 200 OTA.
ELF SHA-256 is `1cb10ca022ceec0e4b6cbc17d372af699a69682c3fa5296bb324796b944f58d3`.
Image: 2,568,176 B, SHA-256
`ba61d232b8dda816ff7fe45a314f6bb628da165cf7134e3c0d15e3ee8a75feae`.
Bundle: 2,568,364 B, SHA-256
`42e5e0a2f99b3d72487908ea0b5f324f0749f2f07b8b4a69725329ef63b6f535`.

All three clean-source ordinary builds, unchanged locks, signature/project/budget
checks and local full host qualification passed. Exact-source CI passed:
[12-job matrix](https://github.com/dvucinozd/Pajoniiir/actions/runs/37518777918),
[USB gates](https://github.com/dvucinozd/Pajoniiir/actions/runs/37518777839),
[documentation](https://github.com/dvucinozd/Pajoniiir/actions/runs/37518777872).
The separate [Advanced Security run](https://github.com/dvucinozd/Pajoniiir/actions/runs/37518788650)
failed with HTTP 402/monthly quota; no security review completed.

The operator re-enabled MT on both decks and eight-beat zoom. Three approximately
12-second windows used the same sources, pitches and loops; only initial/final
API snapshots were taken, without continuous polling. Both decks were stopped
automatically at completion. Raw records are private under
`.cache/m3-migration/20261006-2fa8a637/mt-8-beat`.

| Measurement | Stopped | Solo D1 | Dual |
|---|---:|---:|---:|
| Refresh interrupts | 608 | 607 | 606 |
| Coalesced refreshes | 0 | 0 | 57 |
| Frame interval mean, us | 19,999.1 | 19,999.1 | 22,063.2 |
| Wake latency mean, us | 12.5 | 188.6 | 2,631.9 |
| Overview entry after refresh mean, us | 96.5 | 415.3 | 3,827.6 |
| Callback duration mean, us | 475.3 | 5,652.7 | 17,165.7 |
| D1 cache / blit mean, us | No redraw | 186.9 / 3,841.5 | 211.9 / 4,173.8 |
| D2 cache / blit mean, us | No redraw | No redraw | 237.4 / 4,565.5 |
| D1 finish after refresh mean, us | No redraw | 4,623.4 | 8,636.6 |
| D2 finish after refresh mean, us | No redraw | No redraw | 15,521.3 |

There were 549 dual redraws; 42 D1 finishes and 113 D2 finishes exceeded 20 ms.
The operator confirmed solo D1 sharp, dual deformed, audio clean. PCM underrun,
UAC dropped/overflow and output-late counters stayed zero in this short test.
This is a visual FAIL and diagnostic measurement, not timing-soak acceptance.

The next focused repair removes the wrapper's duplicate whole-strip source
cache sync: the pinned IDF 6.0.2 PPA SRM driver already writes back its complete
input row window and invalidates the destination before DMA. Cache maintenance
is then owned by the driver and included in PPA duration. It also rebuilds the
waveform-first frame context after Library only when UI track/ANLZ publication or target/tab
changes. A changed load or media clear still refreshes immediately; unchanged
playback avoids a second round of audio/file-mutex-backed reads. Other board
policies retain their existing refreshed-context behavior. Panel timing,
two-deck redraw order, cadence and geometry remain unchanged. Quantitative and
operator retests must establish whether this repair is sufficient.

## Installed cache/context repair, 2026-10-07

The live runtime now confirms `M3-dev-g180b008c4f19` in VALID `ota_0`:

- Clean source `180b008c4f191456b98645aaf55f324d2a33a6ae`.
- ELF SHA-256 `791667086cc29dd7fb53119c9d89ce3fc8d8eca3f7a29b94b5913a409b8341cf`.
- Image 2,568,304 B, SHA-256 `3ce734e567dda38abe2a103b6f5133d922325b0231aa723c4168e650c87b0728`.
- Bundle 2,568,492 B, SHA-256 `4a56b5c0d27cb94b8d4db854813fe5f8cb33666976122905577c561ab9b29f3d`.

All three clean-source ordinary ESP-IDF 6.0.2 builds, unchanged locks, signed
packages and candidate verification passed. Full local host qualification and
all three product simulator presentations passed without baseline changes.
Exact-source CI passed: [12-job matrix](https://github.com/dvucinozd/Pajoniiir/actions/runs/37521456636),
[USB](https://github.com/dvucinozd/Pajoniiir/actions/runs/37521456864),
[documentation](https://github.com/dvucinozd/Pajoniiir/actions/runs/37521456739).
The separate [Advanced Security review](https://github.com/dvucinozd/Pajoniiir/actions/runs/37521478181)
failed with HTTP 402/monthly quota; no security review completed.

The upload returned HTTP 200. The installation script's 100-second polling
deadline then expired without confirming the new image. The later live check
confirmed exact source/ELF/board/project, VALID and idle. This distinguishes
successful current installation from an unestablished earlier startup timing;
it does not prove that the image became VALID within the polling deadline.
Raw upload evidence remains under `.cache/m3-migration/20261006-180b008c`;
later runtime verification and retest evidence are under
`.cache/m3-migration/20261007-180b008c`.

The 324-track catalog and FLX4 returned. D1 44.1 kHz / +5% and D2 48 kHz / -5%
were loaded, with the same 30-second loop starts. Both decks remain stopped
until the operator enables MT and eight-beat zoom for the comparable retest.
The operator enabled MT on both decks and eight-beat zoom. The same three
approximately 12-second windows completed and automatically stopped both decks.

| Measurement | Stopped | Solo D1 | Dual |
|---|---:|---:|---:|
| Refresh interrupts | 605 | 605 | 603 |
| Coalesced refreshes | 0 | 0 | 55 |
| Frame interval mean, us | 19,999.1 | 19,999.1 | 22,006.3 |
| Wake latency mean, us | 45.5 | 149.9 | 2,702.1 |
| Overview entry after refresh mean, us | 131.7 | 391.9 | 4,079.3 |
| Callback duration mean, us | 412.7 | 5,599.0 | 16,613.2 |
| D1 cache / blit mean, us | No redraw | 188.4 / 3,821.9 | 208.5 / 4,170.1 |
| D2 cache / blit mean, us | No redraw | No redraw | 238.0 / 4,542.4 |
| D1 finish after refresh mean, us | No redraw | 4,587.3 | 8,823.2 |
| D2 finish after refresh mean, us | No redraw | No redraw | 15,365.6 |

There were 549 dual redraws; 51 D1 and 109 D2 finishes exceeded 20 ms.
The operator again confirmed solo sharp, dual deformed, clean MAIN/headphones
sound. PCM underrun, UAC dropped/overflow and output-late counters remained zero
in the short test. This candidate fails the physical visual gate; the small
timing differences do not establish a material repair. The new 60-minute soak
was NOT RUN. No public release/channel was modified.

## Nonblocking display observation candidate

The frame still called the blocking decoder-mutex status/position APIs before
both waveform writes: two deck-position reads, two duration queries and an
active-deck loading query. Deck chrome repeated status and LOAD LOCK queries.
The historical M3 duration helper used metadata directly; integration added
decoded-duration selection but also repeated these blocking status reads.
This is a demonstrated blocking code path, not proof that it explains every
cache or scanout outlier.

The next candidate adds a zero-wait production status getter that returns
TIMEOUT without changing its caller-owned snapshot when decode owns the mutex.
One UI-owned observation per deck feeds position, session-checked decoded
duration, loading/error chrome and library progress. Track publication/clear
invalidates the retained observation. The existing position interpolator bridges
ordinary missed observations. Control state and the atomic playing flag are
sampled separately without querying decoder position; LOAD LOCK needs only
that playing flag. Authoritative transport/status APIs remain available for
LOAD/seek/CUE decisions. Scratch position uses the same audible-head calculation
in both status paths, without a nested decoder-mutex acquisition.

Tests exercise the actual production API with a decoder mutex held by another
thread, both decks, unchanged timeout payloads, invalid arguments and recovery
to identical authoritative status. Product simulators now execute the same
display-observation path and check retained duration while busy plus refresh
after release. Full host qualification, clean target builds, exact-source CI,
signed packaging and measured/operator retest are required before acceptance.
Panel timing, geometry, PPA order, redraw cadence and audio DSP remain unchanged.
