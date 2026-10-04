# Package G — SDMMC experiments and fail-closed recorder

Status: **software verified; all physical gates NOT RUN**.
Branch `codex/fork-improvements`, following F `0666a9a`.
Donor reference `428b97dd4a175f03d3a172c8db9c4d5ed94195fb` (v323).
The SD idle adaptation preserves p3a copyright and Apache-2.0 with local
LICENSE/NOTICE. The shared core remains in the P4 tree; no APTA code is merged.
Production M2.4, public OTA channels and partition sizes remain unchanged.

## Boundaries

- Ordinary builds have recorder and SD idle experiment disabled. Enabling REC
  requires both the dedicated CMake experiment flag and Kconfig marker.
  The production OTA packager rejects either recorder marker or idle experiment.
- IDF is exactly 6.0.2. Both CMake and C source require explicit reconsideration
  on upgrade. SPI and disabled SD wait use the original implementation.
- Enabled SD wait probes immediately then yields one scheduler tick between
  probes. It rechecks the five-second deadline after yielding, before CMD13.
  An in-flight command retains the driver's own timeout; the shim cannot
  preempt it. No allocation or logging occurs in the polling loop.
- JC4880 USB DMA stays internal. Optional JC1060 PSRAM USB DMA requires an
  8 KiB, 64-byte-aligned internal DMA bounce allocation before SD mount.
  Both SD reads and writes use the host alignment callback; external buffers
  cannot bypass the bounce. Allocation failure leaves `/sd` unavailable and
  preserves USB playback. One buffer is retained across mount retries.
- Existing FAT/media locks are preserved. Atomic long-operation reservation
  excludes REC and future track downloads through START/STOP/fault cleanup.
  Package J must reserve DOWNLOAD across its complete transaction; no Link
  download implementation is claimed by G.

## Recorder ownership and partial files

The audio producer only copies to the preallocated bounded ring. On the first
lost block it closes admission and signals the writer. The writer publishes
ERROR; STOP aborts the take, retaining `.part`. A shortened take is never
promoted as a successful `.wav`.

START/STOP serialize with a control mutex. RECORDING is published only after
writer creation succeeds. STOP closes producer admission, waits up to five
seconds for producer quiescence, then separately waits up to five seconds for
writer exit. A timeout retains the ring, writer ownership and activity
reservation; retry can retire resources once exit is acknowledged. These are
ownership wait bounds, not a guarantee of total filesystem STOP latency.

Clean publication requires header patch, fflush/fsync, close and rename.
Boot recovery validates the stereo 16-bit PCM header, truncates to complete
frames, applies the same finalize sequence and uses `.recovered.wav`.
Failed durability/publish stages retain the partial. Existing recovery output
is not overwritten; only actual published files count as recovered events.
Physical media removal, power loss and FAT durability remain NOT RUN.

## Diagnostics and repeatable A/B builds

Optional `/api/status.sd_metrics` reports saturating counts/maxima for SD idle
polls/errors/timeouts, sector reads/writes/errors and FAT gate wait/hold.
Idle counters describe only the enabled shim; zero with the shim disabled
does not prove the original driver never waited. Sector maxima include driver
transfer latency, not a separate filesystem latency distribution.
Experimental `/api/recording` additionally reports `fsync_max_us` alongside
existing write/gate/ring diagnostics. Reading snapshots performs no filesystem
operation. Existing audio/USB/runway and memory diagnostics remain authoritative.

Build-only commands (Windows, no device access):

```powershell
.\tools\build_storage_experiment.ps1 -Project main-deck-p4 -Variant idle-off
.\tools\build_storage_experiment.ps1 -Project main-deck-p4 -Variant idle-on
.\tools\build_storage_experiment.ps1 -Project main-deck-jc1060 -Variant idle-on
.\tools\build_storage_experiment.ps1 -Project main-deck-jc1060 -Variant psram
```

Each variant has its own build directory and generated SDKCONFIG. The idle
pair differs only by the SD wait flag. The JC1060 DMA pair differs by USB
PSRAM and required SD bounce; P4 DMA policy is unchanged.
CI builds both ordinary targets, both recorder targets and JC1060 PSRAM/bounce,
checks immutable locks, image budget/project identity and linked SD/DWC wraps.
Experimental artifacts have separate names and explicit provenance.

## Software evidence

New selectable suites compile production modules: SD idle core and enabled/
disabled wrapper, SD DMA host configuration and sector wrappers, recorder
runtime and actual sink recovery. They cover deadlines/errors, SPI fallback,
bounce allocation failure/reuse, transfer argument/result preservation,
download exclusion, overflow, disk-full, writer failure, STOP timeout ownership,
sync/rename failure, partial-frame truncation and recovery overwrite rejection.
Existing WAV/ring/STOP/finalize/gate suites remain enabled.

The full host runner, 300-second virtual dual-deck Master Tempo soak and all
11 unchanged LVGL screenshot baselines pass locally. Firmware build/hash and
CI results are recorded at the final checkpoint below.

## Physical measurement gate — NOT RUN

Do not enable either experiment for release based on software tests. Record
exact firmware SHA/hash, board/panel revision, flash/PSRAM/USB topology, card
manufacturer/model/capacity/filesystem and operator before collecting results.
Ignore COM-connected devices belonging to other projects. No serial inventory,
connection or flashing is part of this program; future installation uses an
explicitly authorized OTA path.

For each A/B variant run the same dual-deck MP3/WAV/FLAC set, browse/artwork,
SD read/write/fsync workload and recording duration. Capture `/api/status`,
`/api/recording` and service log before/after: SD wait/transfer/gate maxima,
audio deadlines and decode runway, USB completions/ring watermarks, internal/
DMA heap and PSRAM minima, fragmentation and task stack reserves.
Maxima alone do not supply p95/p99; record operation timing distributions in
the hardware bench harness if that acceptance requires percentiles.

Exercise full card, removal during write/checkpoint/STOP, delayed writer,
power interruption and restart recovery. Verify WAV headers, complete PCM
frames and that faulted takes remain distinguishable. Download/REC contention
needs repetition after J integrates its real worker. Require no new strict
audio/USB fault events, no watchdog/reset/leak and operator-confirmed clean
MAIN/cue, including a 180-minute final-image dual-deck run. No physical test,
card performance improvement or audible acceptance is claimed here.

Rollback: use an ordinary build with original SD wait and recorder disabled;
keep partial recordings and apply the existing signed service OTA policy.

## Final checkpoint

Implementation `d9025101d62af38df43e247b6b60fab95842693c` was pushed and
verified against the remote branch. [CI run 37200184833](https://github.com/dvucinozd/Pajoniiir/actions/runs/37200184833)
passed all six jobs: host regression/simulator, both ordinary builds, both
recorder builds and JC1060 PSRAM/bounce. Each firmware job checks lock stability,
app budget/identity, resampler precision and linked SD/DWC wrappers.

Local final-source builds embed `M2.4-45-gd902510` and use IDF 6.0.2:

| Target / variant | Bytes | SHA-256 |
| --- | ---: | --- |
| JC4880 ordinary | 2,547,168 | `ef20dec5f8c16ed3e972ebb905349c55c082294bf90d9ad502ed4a399420fe37` |
| JC1060 ordinary | 2,544,640 | `b8d8e68100d2919699b553dc8041769583dd6f437e15e4b6d91ceb76ebd11932` |
| JC4880 REC / idle-on | 2,560,352 | `59658c9bcb4f7daa719c4086c902e6726186f41faa8b042e65ff245bfc3aaeae` |
| JC4880 REC / idle-off | 2,559,824 | `903ab60570f3e223d78209e0a9f72f38b6b2b5499b743c3266e5782f26284da2` |
| JC1060 REC / internal USB DMA | 2,557,888 | `469713d4673547cff4c85db3762966eab29c0d5d22f970fc194e3a9340f6e75f` |
| JC1060 REC / PSRAM USB DMA + SD bounce | 2,558,240 | `f900816924ef6583c611529f260ba9e7e7cc3ae65749ce010adc175de70ace0a` |

Both JC4880 A/B builds passed locally; comparing generated SDKCONFIG entries
confirmed their only configuration difference is `CONFIG_PAJONIIIR_SD_IDLE_WAIT`.
All are below the unchanged `0x380000` application limit. Both dependency locks
remain unchanged. Local host closure ran 117 compiled/functional suite entries
plus static/API/signing checks; the threaded reservation test completed 20,000
acquisitions without simultaneous REC/download ownership. The production OTA
packager was executed against all three experiment flag fixtures and rejected
them before binary/signing work.

Ordinary linker DIRAM used/free is 296,816/275,552 bytes on JC4880 and
293,461/278,907 on JC1060, 72 additional static bytes each versus F. Internal
`.text` remains 82,556 and 80,312 bytes respectively. These are linker counts,
not measured runtime DMA heap, PSRAM availability or stack reserves; those
remain NOT RUN. No blanket IRAM relocation was added.

No image was signed, installed or released. The subsequent closure checkpoint
only records evidence and does not change firmware source. H is next; the SD
workaround and recorder remain experimental pending physical measurement.
