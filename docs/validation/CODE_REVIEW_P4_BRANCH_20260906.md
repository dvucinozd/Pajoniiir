# Overall code review: P4-only branch, 2026-09-06

Follow-up: the fixes and their current verification are recorded in
[CODE_REVIEW_P4_REMEDIATION_20260906.md](CODE_REVIEW_P4_REMEDIATION_20260906.md).
The findings and baseline results below describe the pre-remediation snapshot.

## Disposition and reviewed revision

**Do not treat this branch as release-ready yet.** The default firmware build,
main host suite and UI regression pass, but this review identifies five P1
runtime/lifecycle findings, three P2 runtime findings and two failing controller
test gates. Hardware qualification remains a separate, open requirement.

- Branch: `feat/p4-dual-usb-host`.
- HEAD and fetched tracking revision:
  `af597d8813f8a6a7fc20898bb25b5943230edab1`.
- Review includes the current production source, not just the last commit.
- The pre-existing staged renames, unstaged edits and untracked replacement
  documents were preserved. The worktree was already dirty when review began.
- No production firmware, existing tests, baseline images, signing keys or
  installed device firmware were changed by the review.
- This is a risk-focused branch review, not a claim that every line of imported
  libraries has been audited. Detailed inspection covered audio scheduling and
  lifecycle, MIDI dispatch/profile activation, USB ownership/recovery/UAC,
  storage removal, library/load publication, web and OTA admission, UI ownership
  and the corresponding build/test entry points. Imported USB HCD behavior was
  checked where the application depends on its completion semantics.

Evidence labels below distinguish **executed** observations from **code-derived**
failure schedules. A deterministic host boundary stub demonstrates a software
interleaving; it does not establish its frequency or timing on physical P4.

## Findings

### R1 — P1: Replace the task-context busy spin in controller runtime

Location: `firmware/main-deck-p4/components/controller_runtime/controller_runtime.c:34-40`.

`runtime_lock()` spins on an atomic flag without blocking, disabling preemption
or providing priority inheritance. Its callers run in the USB task (priority 7
while streaming), dispatch task (4), and profile task (3); these tasks are not
pinned. For example, the profile worker can hold the flag in
`controller_runtime_request_snapshot()`, then USB and dispatch can each occupy
one CPU spinning for it. The priority-3 owner cannot run to release the flag.
The USB owner also services UAC callbacks, so this can freeze control/audio
service and eventually trigger watchdog recovery.

The claimed bounded-memory-only lock body is also inaccurate: runtime paths call
`controller_profile_runtime_active/map/emit_snapshot()`, which take a FreeRTOS
mutex with `portMAX_DELAY` in production.

Evidence: **code-derived**, including actual task priorities in
`p4_local_controller.c:276-298` and `controller_usb_audio_stream.c:298-299`.
The controller runtime host tests are sequential and compile profile locking
out with `CONTROLLER_PROFILE_RUNTIME_PC_TEST`.

Required fix: use a scheduler-aware mutex or move mutable mapping/dispatch state
under one task owner. Do not simply replace this with a critical section while
retaining blocking profile calls. Exercise priority inversion and concurrent
profile activation/MIDI/dispatch on the firmware scheduling path.

### R2 — P1: Do not retire audio task ownership after an incomplete startup abort

Locations: `audio_engine.c:4201-4207` and `audio_engine.c:4238-4244` in the audio
engine component.

When output-task creation or partial loader/decoder creation fails, the abort
path waits up to 1500 ms per worker. If a worker is still blocked in media I/O,
the code correctly avoids immediately freeing its cache, but then unconditionally
calls `audio_fw_runtime_mark_stopped()` and `audio_fw_task_context_reset()`.
This clears the worker count, task handles and its still-live context.

A retry now passes through STOP as though no workers exist and can close/free
the old preload or bind a new session into the same structures. The old worker
retains `fw`, `runtime`, `eng` and `ctx` pointers and does not revalidate its
generation after every I/O operation. When it resumes, it can write into the
new session, use freed storage, or use the reset context. Resetting a D2 context
also changes `ctx->deck` to zero before the old worker signals completion.

Evidence: **code-derived** from both abort branches, `ae_loader_task()` and
`audio_fw_task_context_reset()`. The managed MSC transport contains 5000 ms
transfer waits, so the 1500 ms join timeout is not an upper bound on all I/O.

Required fix: retain ownership and the original context until every started
worker has acknowledged exit; reject/retry LOAD while teardown is pending.
Fault-inject partial task creation plus a delayed media operation and verify
that no session/context/cache is reused prematurely.

### R3 — P1: Preserve consumed worker-exit acknowledgements across STOP retries

Location: `audio_engine.c:4329-4345`.

STOP counts consumed semaphore tokens in the local variable `exited`. On timeout
it returns without decrementing or otherwise recording the joined workers in
`runtime->tasks_started`. If one of two workers exits during the first STOP,
the next STOP still demands two tokens even after the second worker has exited.
Those workers can signal only once each. Subsequent STOP/LOAD attempts therefore
keep returning `ESP_ERR_TIMEOUT` and LOAD reports `STOP ERR` until reboot.

Evidence: **executed** using the complete, unchanged production
`audio_engine_stop_for_deck()` function with deterministic semaphore/task stubs,
linked with the real `audio_fw_runtime.c`:

```text
CONTROL: two exits available -> STOP succeeds
FIRST: rc=0x107 remaining_tokens=0 tasks_started=2
RETRY after final worker exit: rc=0x107 remaining_tokens=0 tasks_started=2
THIRD: rc=0x107 tasks_started=2
```

Required fix: persist outstanding worker ownership or an acknowledged-exit mask
across attempts. The acceptance test must delay one worker past the first
timeout and require a later STOP and LOAD to succeed after its exit.

### R4 — P1: Remove remaining blocking metadata locks from the audio output task

Locations: `audio_engine.c:1387`, called from `ae_output_task()` at 3359-3360;
also scratch-abort dispatch at 3391-3392 into the seek function at 4431.

The new output bookkeeping correctly uses `AE_TRY_LOCK()`, but
`complete_eof_drain_if_ready()` still calls `AE_LOCK()` from the shared output
task. `AE_LOCK()` waits indefinitely on the metadata mutex. A decoder can hold
that same mutex during FLAC seek/recovery or cache-miss I/O. Thus D1 reaching
EOF while D2 seeks/recovers can stop delivery of D2 audio for the duration of
that I/O. The scratch-abort branch likewise synchronously calls the locking
seek publisher from the output task.

Evidence: **code-derived** from the call paths and decoder lock scope; no new
physical deadline measurement was performed. Ordinary playback and the previous
bookkeeping repair do not cover these exceptional paths.

Required fix: defer EOF completion if the lock is unavailable and publish
scratch-abort seek requests without blocking the output owner. Test D1 EOF and
scratch-abort while D2 holds the decoder mutex for longer than one output period.

### R5 — P1: Quiesce audio producers before resetting the UAC resampler

Locations: `controller_usb_audio_stream.c:453-455`, `460-463`, `475-528`.

The USB task closes `s_accepting` and resets `s_resampler` during cleanup/start,
but a producer checks admission only once at the beginning of
`controller_usb_audio_stream_write()`. There is no in-flight producer count,
generation handshake or shared ownership around the resampler. The output task
can still be converting/publishing a block when the USB owner zeroes the same
state. Ring critical sections protect the individual ring operation, not the
complete admission/conversion/publication transaction.

Evidence: **executed** with the actual stream, ring, resampler and packetizer
sources. A boundary stub performs real `request_stop()` and `poll_cleanup()`
between conversion and ring publication:

```text
STOP before ring publication -> rc=0 quiesced=1 accepting=0 queued=129
```

Cleanup therefore reports quiescence while an old producer can still publish.
Concurrent reset inside the 48-to-44.1 kHz converter is additionally a data race
on its rate/phase/channel fields; the probe demonstrates the lifecycle violation,
not a physical crash or a measured reconnect frequency.

Required fix: close admission, account for admitted producers, and reset only
after they finish, or let the output task alone reset converter state through
an epoch command. The waiting mechanism must not starve the lower-priority
producer. Cover disconnect during both equal-rate and 48 kHz conversion.

### R6 — P2: Inspect individual isochronous packet completion results

Location: `controller_usb_audio_stream.c:187-198`.

`isoc_callback()` treats a completed transfer as successful without inspecting
`isoc_packet_desc[]`. The pinned USB HCD's `_buffer_parse_isoc()` assigns packet
statuses such as `SKIPPED`/`ERROR`, then sets the overall transfer status to
`COMPLETED` regardless. Samples already removed from the ring can therefore be
lost on USB while `transfer_failures`, ring underflow and dropped-block counters
remain unchanged. Current healthy-counter acceptance cannot detect this loss.

Evidence: **executed** against the production callback, using the completion
combination produced by the pinned HCD:

```text
SKIPPED packets=4 -> transfer_failures=0 faulted=0 resubmits=1
```

Required fix: account for each packet's status and transferred byte count before
refilling it, expose lost-packet/frame counters, and define a bounded recovery
policy. Add mixed-success and all-skipped packet tests and include these counters
in hardware acceptance. Do not necessarily reset USB for every isolated packet.

### R7 — P2: Retry authoritative controller connection-state delivery

Location: `p4_local_controller.c:95-113`.

`set_semantic_connection()` commits its atomic state before attempting the
zero-wait downstream queue send. If that queue is full, the state event is lost;
later calls with the same state return early instead of retrying it. This path
bypasses the runtime's durable held-state reconciliation.

A lost CONNECTED edge leaves `deck_core` unaware of the controller and its VU
task disabled. A lost DISCONNECTED edge can leave `s_flx4_connected` true so the
next CONNECTED event does not force a reconnect LED snapshot. Releasing durable
held controls does not repair this separate connection-state/LED problem.

Evidence: **code-derived** from the queue return path and consumers in
`deck_core.c:1731-1751` and `2085-2088`.

Required fix: keep desired and acknowledged connection state/epoch separately
and retry from the dispatcher until accepted. Saturate the deck queue across
disconnect/reconnect and verify eventual state convergence and forced LEDs.

### R8 — P2: Normalize low-rate sources before the fixed-rate UAC output

Locations: `audio_engine.c:2758-2759`,
`controller_usb_audio_stream.c:471-473` and `audio_engine.c:3673-3674`.

The decoder chooses the source rate as the output clock for sources below
48 kHz, while the UAC writer rejects all rates below 44100 Hz. A valid 32 kHz
MP3 or PCM16 WAV loaded as the first track can play through PCM5102A while all
FLX4 cue blocks are rejected. The caller discards that return value. Since the
shared output clock is retained while another deck remains loaded, this can
also affect a subsequently loaded 44.1/48 kHz deck.

Evidence: the production UAC write function **executed** with 32000 Hz returns
`ESP_ERR_INVALID_ARG (0x102)`; the output-clock and ignored-return paths are
**code-derived**. Physical low-rate playback was not tested.

Required fix: select a supported 44.1/48 kHz output rate for every accepted source
and use the existing deck resampler, or explicitly reject unsupported input rates
before declaring the deck playable. Test low-rate-first mixed-deck load order.

### R9 — P2: Restore the missing USB audio test suite referenced by CI

Location: `tests/controller_usb_host/run_tests.ps1:33` and
`.github/workflows/p4-dual-usb-spike.yml:67-69`.

`tests/controller_usb_audio/run_tests.ps1` does not exist in the working tree or
the reviewed Git tree. The USB-host runner passes its codec and MIDI gate tests,
then terminates when invoking it. The dedicated workflow also calls the missing
suite directly. This is a reproducible clean-checkout gate failure, not an
uninstalled compiler or a hardware-only limitation.

Evidence: **executed**, nonzero runner exit; `git ls-tree -r --name-only HEAD
tests/controller_usb_audio` returns no entries.

Required fix: restore and commit the intended portable UAC tests and required
fixtures, including ring/packetizer/descriptors; cover the production completion
and cleanup glue as well. Make the standard qualification command execute the
controller gates so its green result cannot omit them.

### R10 — P2: Reconcile LED diagnostics expectations with the exercised policy

Location: `tests/controller_led_runtime/test_controller_led_runtime.c:165-168`.

The LED runner fails at `diagnostics.dynamic_packets == 2u`. Earlier packet
assertions now exercise a generic profile with FLX4 fallback disabled, but the
final cumulative expectations still describe the older fallback behavior.
The current sequence builds four dynamic packets (including send attempts),
two builtin packets, one builtin fallback and five unsupported requests.

Evidence: **executed**, runner exit 1 at line 165; counter totals follow the
successful preceding calls and the counter increments in the production LED
builder. This is a failing test expectation, not evidence that the generic
controller should regain FLX4 fallback.

Required fix: update the test to assert the intended policy and preferably check
counter deltas per scenario. Then run the complete LED suite through its final
assertions.

## Verification performed

| Check | Result | Limits / evidence |
| --- | --- | --- |
| Fetch and tracking SHA | PASS | HEAD equals fetched upstream; original local docs changes remain |
| `tests/run_p4_host_tests.ps1` | PASS, exit 0 | 77 `run` steps; does not invoke all standalone controller suites |
| `tests/audio_keylock_soak/run_audio_keylock_soak.ps1` | PASS, exit 0 | 300 virtual seconds, two decks; zero consumed-frame drift, zero clicks/clipping; frequency errors 0.156% and 0.119% |
| `tests/ui_simulator/run_ui_simulator_e2e.ps1` | PASS, exit 0 | Scripted navigation and 7 exact screenshot hashes; no baseline updates |
| `tests/controller_runtime/run_tests.ps1` | PASS | Event buffer, builtin/profile mapping, recovery arbiter, recovery gate and topology; sequential host execution |
| `tests/controller_usb_host/run_tests.ps1` | FAIL | Codec and MIDI OUT gate pass; missing nested UAC runner then aborts |
| `tests/controller_led_runtime/run_tests.ps1` | FAIL, exit 1 | Diagnostics expectation at line 165 |
| `tests/p4_dual_usb_log_validator/run_tests.ps1` | PASS | 10 validator/evidence-tool tests |
| `tests/p4_dual_usb_spike/run_tests.ps1` | PASS | Parser reports `TESTS_RUN=36` |
| STOP timeout review probe | REPRODUCED | Exact production function and runtime source with semaphore stubs |
| UAC review probe | REPRODUCED | Production stream/ring/resampler/packetizer; RTOS/USB boundary stubs |
| ESP-IDF product build | PASS, exit 0 | ESP-IDF v6.0.2, fresh isolated output directory and generated config from defaults |
| Silicon and output config | PASS | `SELECTS_REV_LESS_V3=1`, `REV_MIN_FULL=100`, PCM5102A enabled |
| Dependency lock | PASS | No diff in committed `dependencies.lock` after build |
| `git diff --check`, `git diff --cached --check` | PASS | Existing LF-to-CRLF notices; no whitespace errors |

Build command, after loading the required ESP-IDF profile:

```powershell
idf.py -B build_overall_review_20260906 `
  -D SDKCONFIG=build_overall_review_20260906/sdkconfig `
  -D IDF_TARGET=esp32p4 build
```

The fresh application is 2,451,936 bytes, below the 3,670,016-byte project budget
by 1,218,080 bytes. SHA-256:
`faceb116b9a5c49362042b7c5f393027fa5c2d94fb5c72985a565e3d5b1e98a7`.
This is the review build, not a claim of identity with the installed image.
It was not signed, flashed or exercised on the device. Existing managed
dependencies were reused; the committed resolution did not change.

Local, ignored review evidence is under `tmp/overall-review-20260906/`:
the build/host/UI/keylock logs and the two reproducible Python probes
`reproduce_stop_timeout.py` and `reproduce_uac.py`. Probes compile production
sources with explicit boundary stubs and leave their generated files there.

## Remaining acceptance and repair order

1. Repair task lifetime and synchronization (R1-R5), with delayed-I/O,
   partial-task-creation, two-core scheduling and disconnect-during-write tests.
2. Repair observability and operator behavior (R6-R8), including packet-loss
   counters and saturated connection-state delivery.
3. Restore all controller qualification gates (R9-R10) and rerun the complete
   set against one exact candidate.
4. Run the existing physical power/VBUS, repeated USB lifecycle, real
   MP3/WAV/FLAC, combined DSP deadline/listening, web/OTA fault, multi-hour soak
   and enclosure acceptance from the active risk register and startup checklist.

No new hardware smoke, OTA deployment, voltage/current measurement, multi-hour
soak or enclosure test was performed during this code review. The previous
30-minute exact-image evidence remains scoped to its documented image and
scenario; it does not close the concurrency/fault-injection cases above.
