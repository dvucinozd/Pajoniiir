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
