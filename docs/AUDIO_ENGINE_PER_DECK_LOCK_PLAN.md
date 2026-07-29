# Per-deck audio engine locking — analysis and deferred plan

**Status: deferred, not started.** The analysis below was done on
`migration/esp-idf-6.0.2` after R7; implementation was deferred because it
cannot be verified with what is available. Read the "Why this is deferred"
section before picking it up.

## The problem

`AE_LOCK` is a single global recursive mutex (`s_file_mutex`, named for its
original file-access purpose). Every deck shares it, so deck 1's decode task and
deck 2's decode task serialise against each other even though they touch
disjoint state. On a two-deck player that is the normal operating case, not an
edge case.

R7 already removed the worst symptom — the decode task no longer holds the lock
across a blocking USB read — but the lock itself is still global.

## What the analysis established

All of this was checked against the source, not assumed:

- **State is already per-deck.** `s_engines[]`, `s_pcm_rings[]`,
  `s_pcm_timelines[]`, `s_scratch_*[]` and the rest are arrays indexed by deck.
  The lock is the only global thing in the design.
- **The decode task's locked regions are single-deck.** All four regions index
  exclusively by `ctx->deck`. The only non-per-deck static they touch is
  `s_ring_flush_mux`, which is itself a lock rather than protected data.
- **Only two locked regions span both decks:** the output task's per-block
  bookkeeping (positions, peaks, limiter stats) and
  `audio_engine_get_diagnostics_snapshot`. Neither needs cross-deck atomicity —
  nothing reads both decks expecting a consistent instant. The output task's
  audio I/O (`i2s_channel_write`, `esp_codec_dev_write`) is already outside the
  lock; only the bookkeeping is inside.
- **`audio_output_service_open_codec` is deck-agnostic** and protects
  `s_output_codec_open` / `s_main_i2s_tx`.

## Proposed shape

- `s_deck_mutex[AUDIO_ENGINE_DECK_COUNT]` recursive mutexes, plus one
  `s_shared_mutex` for deck-agnostic state (codec open, limiter stats).
- `AE_LOCK_DECK(d)` / `AE_LOCK_SHARED()` replacing `AE_LOCK()`.
- **Invariant: never hold more than one of these at a time.** The two cross-deck
  sites take and release per deck in sequence, then take the shared lock
  separately. With no nesting there is no lock order to get wrong, which is what
  makes the change reviewable by inspection.
- A runner gate can enforce the invariant structurally by rejecting any
  `AE_LOCK_*` that appears between another `AE_LOCK_*` and its unlock.

## Why this is deferred

Two verification gaps, both of which have to close before this is safe to land:

1. **No hardware.** There is no board to flash, so the change cannot be run at
   all in its real configuration.
2. **The host suite does not exercise the firmware lock path.** `audio_engine.c`
   is compiled for host tests with `AUDIO_ENGINE_PC_TEST`, where `AE_LOCK` is a
   plain pthread mutex — the FreeRTOS recursive-mutex path never executes.

The residual risk is **not deadlock**, which the no-nesting invariant rules out
by construction. It is that the global lock also provided mutual exclusion
*between the two decode tasks* for anything shared that they reach through a
function call inside a locked region. Ruling that out requires a call-graph
audit of everything invoked under `AE_LOCK`, and its failure mode is a rare race
that inspection can miss and no test here would catch.

## What would unblock it

Either of:

- **A board**, plus a dual-deck soak that exercises simultaneous decode, seek and
  scratch on both decks.
- **A firmware-configuration host harness**: compile `audio_engine.c` against the
  fake RTOS in `tests/support/rtos` rather than `AUDIO_ENGINE_PC_TEST`, so the
  real lock path runs. `fake_semaphore_recursion_depth()` already exists and
  would let a test assert that deck 1's mutex is free while deck 0's is held.
  The cost is fakes for the I2S, codec and timer dependencies `audio_engine.c`
  pulls in — the largest part of the work, and worth doing only if it also
  unlocks other audio-engine testing.

## Related, already done

- R7 (`perf(audio): warm the compressed cache before taking the engine lock`) —
  removed the blocking USB read from under the lock.
- `library.c` media gate scoping — the catalog walk no longer holds
  `media_io_gate` across the whole PDB parse, so building the library stops
  stalling playback. Same class of benefit, and testable, which is why it went
  first.
