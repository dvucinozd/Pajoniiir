# Shared P4 reliability monitoring plan

Status: integrated-image physical Campaign A/B NOT RUN. Historical M3 results
are retained in the M3 repository and do not qualify the shared-core image.
Adapted from M3 review source e95417c4e2fea007d2c1dcb693790914692c62ba.

## Purpose

This plan preserves the exact procedure for investigating two residual
monitoring findings without treating them as confirmed release failures:

1. the initial FLX4 claim can collide with simultaneous USB mass-storage
   enumeration and then recover automatically on a new address;
2. isolated audio `output-late` events have occurred without audible or visual
   consequences, PCM underruns, UAC data loss, or resets.

No firmware change is justified until the campaign produces a repeatable
failure or a measurable regression. The final clean integrated image must be identified before a campaign.
The historical accepted M3 image remains a wired recovery reference.

## Required monitor utility

The host-side `tools/monitor_p4_reliability.ps1` utility is read-only. It:

- poll `/api/firmware` and `/api/status` at a configurable interval;
- periodically fetch `/api/library` and count tracks;
- save timestamped raw JSON and a compact CSV summary;
- capture the initial and final `/api/diagnostic-log` snapshots;
- calculate counter deltas instead of judging cumulative boot history;
- record HTTP errors, response latency, controller recovery time, library
  recovery time, firmware version, running slot, and unexpected reboot signs;
- stop with a non-zero exit code on a hard failure;
- preserve all output under a timestamped ignored artifact directory.

Use `-Mode TimingSoak` for Campaign B. This mode also fails when the measured
workload leaves the required dual-deck mixed-rate, loop, opposing-pitch and
48-kHz-output profile. Use `-Mode UsbRecovery` for each Campaign A cycle; it
records controller/library recovery time and applies the 15/20-second
investigation thresholds. `-Mode Observe` only captures evidence.

The historical M3 monitor was live-smoked against `M3-51-beta.1-6-g483063f` in PowerShell 7
and Windows PowerShell 5.1. Both runs produced parseable JSONL, CSV, diagnostic
logs and a final result document without modifying device state.

The summary must include at least:

- `controller.present`, `midi_in`, `midi_out`, and `usb_audio`;
- library track count;
- deck state, source sample rate, pitch, and playback position;
- `output_late_count` and `output_late_max_us`;
- both PCM underrun counters;
- UAC dropped blocks, overflow frames, active underflow delta, ring state, and
  `data_loss`;
- service-log queue depth and dropped-record count;
- free internal heap and PSRAM;
- OTA state, running version, and running slot.

## Campaign A: USB enumeration and recovery

Run the following 30 controlled cycles with the Rekordbox medium containing the
accepted 191-track library:

| Scenario | Cycles | Initial connection state |
| --- | ---: | --- |
| Cold boot, both devices attached | 10 | FLX4 on USB2 and media on USB3 |
| FLX4 unplug/replug | 10 | USB3 remains attached; decks stopped |
| USB3 first, then FLX4 | 5 | connect in the stated order |
| FLX4 first, then USB3 | 5 | connect in the stated order |

For every cycle, record:

- time until the web API responds;
- time until the controller is present with MIDI In, MIDI Out, and UAC active;
- time until the library reports 191 tracks;
- USB address transitions visible in the diagnostic log;
- whether recovery needed another replug, reset, or power cycle;
- LED snapshot correctness after reconnect.

### Campaign A PASS/FAIL

PASS requires automatic recovery in every cycle, with no manual reset or extra
replug. A cycle is flagged for investigation if the FLX4 is not fully ready
within 15 seconds or the accepted library is not restored within 20 seconds.

Hard failure conditions:

- controller state never becomes fully ready;
- stale or incorrect LED state survives recovery;
- library does not return to 191 tracks;
- panic, watchdog, reset loop, deadlock, or required manual intervention;
- new PCM underrun, UAC drop/overflow, or service-log drop caused by recovery.

## Campaign B: worst-case output timing soak

Run for 60 minutes initially. Extend to 120 minutes only if the first hour is
clean and a longer confidence run is useful.

```powershell
.\tools\monitor_p4_reliability.ps1 -Mode TimingSoak -DurationMinutes 60
```

Required workload:

- both decks playing continuously;
- one 44.1-kHz source and one 48-kHz source;
- Master Tempo enabled on both decks;
- opposing pitch values, nominally D1 `+5%` and D2 `-5%`;
- active loops on both decks;
- waveform zoom at 4 or 8 visible beats;
- PCM5102A master and FLX4 headphones active;
- display, touch, USB3 media, and Wi-Fi enabled;
- `/api/status` polled every 250 ms;
- `/api/library` checked every 40 status polls;
- `/api/firmware` checked every 120 status polls.

The operator should periodically confirm that master audio, headphones,
waveforms, touch, and controller response remain normal. Do not perform USB
removal during this timing-only phase.

### Campaign B PASS/FAIL

Hard failure conditions:

- audible click, crackle, interruption, or incorrect playback speed;
- waveform deformation, display flash, or UI stall;
- panic, watchdog, restart, or controller disconnect that does not recover;
- any new PCM underrun;
- any new UAC dropped block or overflow frame;
- active-playback underflow or `data_loss=true` caused by the run;
- any new service-log drop.

An isolated `output-late` increment is recorded but is not automatically a
failure when it has no physical or data-loss consequence. Open an engineering
investigation when either condition occurs:

- `output_late_max_us` exceeds 15,000 us; or
- three or more new output-late events occur within one minute.

These are investigation triggers, not retroactive release-failure thresholds.

### Campaign B result — 2026-09-21

The required 60-minute run completed with 13,805 valid workload samples and
no HTTP error, workload violation, PCM underrun, UAC drop/overflow/active
underflow, UAC data-loss state, service-log drop, reset, or physical symptom.
The operator confirmed normal master and headphone audio, waveforms, display,
touch, and FLX4 response throughout the run.

The run recorded 64 `output-late` events, a maximum of 12,074 us, and a
largest rolling one-minute cluster of six. The clustering crossed the
investigation trigger, but no event crossed 15,000 us and none had an audible,
visual, PCM, or UAC consequence. Keep the measured pattern as an R3 monitoring
baseline; it does not independently justify a firmware change. See the
[dated historical M3 validation record](https://github.com/dvucinozd/Pajoniiir-M3/blob/e95417c4e2fea007d2c1dcb693790914692c62ba/docs/validation/2026-09-21-reliability-timing-soak.md).

## Evidence and completion

Store the monitor command, firmware identity, raw samples, CSV summary,
diagnostic-log snapshots, operator observations, and final counter deltas in a
dated validation record. Do not claim a zero-late result unless the measured
delta is actually zero.

When Campaign A is completed:

1. update `docs/RISK_REGISTER.md` with measured recovery rates and timing;
2. update `docs/DEVELOPMENT_PLAN.md` and `docs/DOCUMENTATION_STATUS.md`;
3. link the dated validation record from this plan;
4. only change firmware if evidence identifies a repeatable defect.

## Shared-core evidence identity

The read-only monitor requires matching board/project/source identities from
status and firmware APIs, a clean source, a 64-hex compiled ELF fingerprint and
all required counters. Use `-ExpectedSourceSha` and `-ExpectedElfSha256` from
the signed candidate manifest. The ELF fingerprint identifies the compiled
program; `manifest.json` separately binds it to the full binary SHA-256 and
signed bundle SHA-256. These digests are not interchangeable.

Uptime decreases, identity changes, missing telemetry and critical allocation
failures cannot produce a software PASS. Resource snapshots preserve stack
minima and allocation counters; `/api/status` retains internal/DMA/PSRAM
metrics. Use `tools/check_ui_runtime_budget.py` with board-specific physical
baseline/candidate evidence for the existing resource budget gate. Host tests
exercise its rejection boundaries; no hardware budget has been measured on
the integrated M3 candidate.

Example for the M3 engineering candidate:

```powershell
.\tools\monitor_p4_reliability.ps1 -ExpectedBoard m3 -Mode TimingSoak `
  -DurationMinutes 60 -ExpectedSourceSha <full-source-sha> `
  -ExpectedElfSha256 <manifest-image-elf-sha256>
```

Monitor PASS reports counter/workload evidence only. The result deliberately
keeps `physical_operator_acceptance: NOT RUN`; listening, panel/touch inspection
and operator confirmation belong in the separate exact-image acceptance record.
An isolated output-late event remains monitoring evidence, not a zero-late claim.

The 2026-10-06 bench uses a different, operator-confirmed 100-track export.
Select `-ExpectedLibraryTracks 100` for that medium; the historical 191-track
campaign does not define the count of every export. The shared library API
publishes a `generation` and `tracks` array, whose actual length the monitor
counts, including zero/single-row catalogs. It does not read the historical
M3-only `loaded` field.

`TimingSoak` requires both decks' authoritative `master_tempo` and
`loop_active` fields. Missing telemetry fails qualification. `Observe` can
capture an earlier compact shared status without those fields, but its result
does not qualify the Campaign B workload. The common status publisher now adds
MT and active loop boundaries from deck_core snapshots without changing playback.
