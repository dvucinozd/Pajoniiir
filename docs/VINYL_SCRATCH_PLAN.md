# Vinyl / Scratch Mode — Implementation Plan

Status: **Vinyl/scratch remediation complete and hardware-validated
(2026-07-11)**. Batches 1–3J cover lossless jog transport, touch reliability,
canonical PSRAM PCM history/lookahead, real-time hot-path performance, release
and re-grab, waveform position, clean window edges, dual-deck stress, paused/CUE
scratch with centered pre-roll, active-loop wrapping and deferred pitch handoff.
Phased roadmap for real turntable scratch on the
P4. Written 2026-07-10 so work can resume cleanly.

The phase-local `pending`, `starting point` and `no audible scratch yet` text
below is preserved as implementation history. It is superseded by this status
header and must not be read as current product state.

## Goal

Touching the FLX4 jog **platter top** while a deck plays should let you drag
playback back and forth and **hear the scratch** (forward + reverse), like
vinyl. Releasing the platter hands control back to normal forward playback. The
jog **side ring** (platter not touched) keeps doing the pitch-bend nudge that is
already implemented.

## Non-goals (for now)

- Motorized-platter inertia / spin-down simulation.
- Simultaneous heavy scratch on both decks (validate one deck first).
- Time-stretch / key-lock during scratch.

## Current state (2026-07-10) — the starting point

- **Jog nudge (done):** jog while playing → transient pitch-bend nudge
  (`audio_engine_deck_jog_nudge` → per-deck `s_jog_bend`; output task applies
  `pitch_factor × (1 + bend)` and decays it). `deck_core.c` `on_jog` nudges while
  playing, scrubs the position while paused. Both `JOG_SCRATCH` (platter) and
  `JOG_BEND` (ring) currently route to `on_jog`.
- **Waveform tracking (done):** the mixer snapshot carries
  `effective_speed_permille` (fader × bend), fed to the Overview position
  interpolator.
- **`JOG_TOUCH` (Phase 1 done):** the S3 emits `CTRL_ID_DECK{1,2}_JOG_TOUCH`
  (button press/release, from `FLX4_BTN_JOG_TOUCH` 0x36). `deck_core` now
  consumes it (`handle_jog_touch` → per-deck `s_jog_touched[]`) and gates the
  jog: touched → scrub, untouched + playing → bend.
- **Platter-hold (Phase 1 done):** touching the platter while playing silences
  the deck and freezes its position via `audio_engine_deck_set_hold()`
  (per-deck `s_deck_hold[]`; `deck_output_active()` returns false so the mixer
  skips it — no output, no ring advance). Release resumes forward playback. This
  is an output-level mute; the logical play state stays on (LEDs stay lit).
  **No audible scratch yet — that is Phase 4.**
- **Audio pipeline (forward-only):** decode task → `audio_pcm_ring`
  (`AUDIO_PCM_RING_FRAMES` = 8192 ≈ **186 ms** FIFO) → `audio_resampler_next`
  (forward-only: `fraction += pitch`, pops while `>= 1.0`; **no reverse**) →
  `audio_output_mixer` → I2S. Seek: `audio_engine_deck_seek` only sets
  `seek_target_ms`; the **decode task** performs the real seek (PVBR O(1) or
  linear) and refills the ring.
- **No reverse playback and no random-access PCM buffer exist today.**

## Design overview

The new core piece is a per-deck **scratch engine**:

1. A large PSRAM **scratch buffer** (~3–4 s of decoded stereo PCM) that
   continuously captures the played stream, position-tagged, so a window of past
   (for reverse) and near-future audio is randomly addressable.
2. A **scratch read head** — a fractional frame position driven by jog velocity
   while the platter is touched. Output = linear-interpolated reads of the buffer
   at the head; velocity may be negative (reverse).
3. **Handoff** between normal playback (forward, ring-fed) and scratch (buffer,
   jog-driven): touch-down seeds the head from the current play position;
   touch-up seeks normal playback to the head position, resumes forward, with a
   short cross-fade to avoid a click.
4. **Touch gating:** per-deck `JOG_TOUCH` decides scratch (touched) vs bend
   (ring/not touched).

Everything is **additive and behind `CONFIG_AUDIO_SCRATCH_ENABLED`** so that when
scratch is inactive the output path is byte-for-byte today's. Phases 1–3 do not
change the normal playback output at all.

## Phases

### Phase 1 — Touch state + platter brake/scrub (tactile, no audible scratch) ✅ DONE (2026-07-11)
Low risk; gets the touch signal + "grab the platter" feel working end-to-end and
is the foundation for the scratch handoff. Implemented on branch
`feature/vinyl-phase1-touch-scrub` (commit `7edb21f5`).
- `deck_core`: consumes `CTRL_ID_DECK*_JOG_TOUCH` (`handle_jog_touch`) → per-deck
  `s_jog_touched[]` / `s_jog_hold_active[]`.
- Touch-down while playing → enters a **platter-hold** state: seeds the position
  from the live playhead, mutes the deck output and freezes the position (the
  decoder/ring stay live for instant resume) and remembers a hold was entered.
  Jog while touched → **scrub** the position (seek), like the paused scrub.
  Touch-up → releases the hold and resumes forward from the new position.
- `audio_engine`: per-deck `s_deck_hold[]` + `audio_engine_deck_set_hold()`;
  `deck_output_active()` returns false while held so the mixer skips the deck
  (silence + no ring advance — preferred over full pause so resume is instant).
  Release clears any residual jog nudge; hold is cleared on deck reset/load.
- Gate: touched → scrub+hold; not touched + playing → pitch bend (existing).
- Files: `deck_core.c`, `audio_engine.c/.h`. Tests: `deck_core_dual`
  (`test_platter_touch_holds_and_scrubs_while_playing`,
  `test_platter_touch_while_paused_does_not_hold`) + hold stub + two static
  guards in `run_p4_host_tests.ps1`. Build OK; full host suite passes; verified
  on HW (COM15) — platter stops audio, jog scrubs, release resumes, bend ring
  still nudges.

### Phase 2 — Scratch buffer (capture only, passive) ✅ DONE (2026-07-11)
Build the random-access PCM history without changing playback.
- `audio_scratch_buffer.c/.h` (in the `audio_engine` component): a circular
  stereo-int16 store bound to a caller-owned PSRAM buffer (pure C, host-tested).
  Capacity is fixed for `AE_SCRATCH_SECONDS` (4) at `AE_SCRATCH_MAX_RATE`
  (48 kHz) → 192 000 frames ≈ 768 KB/deck; hi-res sources get a shorter window
  (fewer seconds, since the store holds source frames). Tracks `write_index`,
  `filled`, `sample_rate` and the `newest_pos_ms` of the last frame; frames are
  assumed contiguous at `sample_rate`, so `index_for_ms()` maps a track position
  to a stored frame (rejecting future or evicted-past positions).
- `audio_engine` decode task pushes every decoded source frame into the buffer
  alongside the ring and marks the batch's newest source position; binds the
  rate + resets on track load; resets on a user seek (position discontinuity);
  resets on stop. One-shot INFO log when the window first fills (diagnostic).
- Passive: playback path is byte-for-byte unchanged (the mixer/output never
  reads the scratch buffer yet — that is Phase 4). Verified: HW audio streams
  clean (no ring under-runs) with the added per-frame capture write.
- Files: `audio_scratch_buffer.c/.h`, `audio_engine.c` wiring, `CMakeLists.txt`.
  Tests: `tests/audio_scratch_buffer` (push, used-cap, wrap-window mapping,
  ms→index in-window + future/evicted bounds, reset, null/unset guards) + a
  static guard in `run_p4_host_tests.ps1`. Full host suite + build pass.
- Known edge (deferred to Phase 5): a gapless loop wrap keeps the ring but
  introduces a position discontinuity in the capture window (no reset on loop),
  so `index_for_ms` near a loop boundary can be off — fine for capture-only.
- Risk: low–medium (memory + one cheap write in the hot path).

### Phase 3 — Scratch DSP (bidirectional interpolated read) ✅ DONE (2026-07-11)
The pure scratch read, host-tested in isolation (not wired into the output yet —
that is Phase 4).
- `audio_scratch.c/.h` engine (pure DSP, no ESP deps): a fractional read head
  measured in `frames back from the newest captured frame` (0 = newest) plus a
  `velocity` = forward source-frames advanced per output sample. `render()`
  linear-interpolates the two frames bracketing the head, then does
  `head_back -= velocity` (positive = forward/newer, negative = reverse/older).
  Running off a window edge (`past_new_edge`/`past_old_edge`) → silence, and the
  head keeps integrating so a reversal walks it back in.
- New buffer primitive `audio_scratch_buffer_read_frame_back(buf, frames_back)`
  reads frame N slots before the newest — the frame-accurate step the head walks.
- Velocity model: `audio_scratch_jog(ticks)` adds an impulse
  (`velocity_per_tick`, clamped to ±`velocity_max`); velocity decays toward 0
  each rendered sample (`velocity_decay`) so a platter held still coasts to a
  stop — and a stopped platter (|velocity| < ε) is silent, like a still record.
  Parameters are placeholders to tune on hardware in Phase 5.
- Files: `audio_scratch.c/.h`, `audio_scratch_buffer.{c,h}` (primitive),
  `CMakeLists.txt`. Tests: `tests/audio_scratch` (inactive/stopped silence,
  forward ascends toward newest, reverse descends toward oldest, linear
  interpolation, past-newest + past-oldest silence, reversal re-enters the
  window, jog accumulate/clamp, velocity decay) + a static guard. Full host
  suite + firmware build pass.
- Risk: medium but isolated + host-tested.

### Phase 4 — Integrate scratch into the real-time output (behind the CONFIG flag)
Wire it so touching the platter actually scratches the audio. Split into 4a
(routing + handoff via a plain seek) and 4b (click-free cross-fade handoff).

**Phase 4a ✅ DONE (2026-07-11) — audible scratch, plain-seek handoff.**
- `CONFIG_AUDIO_SCRATCH_ENABLED` (Kconfig, default n; enabled in the P4
  `sdkconfig.defaults` so the product build scratches — flip to roll back to the
  Phase 1 hold). Additive: with it off the output path is byte-for-byte today's.
- Mixer: `audio_output_mixer_deck_t` gained an optional scratch source
  (`scratch_active` + `scratch_render` + `scratch_ctx`); `next_deck_frame` draws
  the deck frame from it (consuming nothing from the ring) when active, and the
  normal EQ/FX chain still applies.
- `audio_engine`: per-deck `audio_scratch_t` + `s_scratch_playing[]`;
  `audio_engine_deck_scratch_begin` (seed the head at the current playhead within
  the window), `_move` (jog → head velocity), `_end` (head position → track ms →
  seek forward). The output task routes a scratching deck to `ae_scratch_render_cb`;
  the decode task **freezes scratch capture** while scratching so the window's
  newest frame stays fixed under the head.
- `deck_core`: `#if CONFIG_AUDIO_SCRATCH_ENABLED`, touch-down → `scratch_begin`,
  jog-while-touched → `scratch_move`, release → `scratch_end` (else the Phase 1
  hold/scrub path).
- Tests: mixer scratch source (replaces ring, consumes 0; silence path) + static
  guards. HW: forward + reverse scratch confirmed audible; feel OK (params to
  tune later).

**Phase 4b ✅ DONE (2026-07-11; HW feel test pending) — click-free handoff.**
- The release no longer snaps from the scratch source to forward playback. The
  scratch render callback (`ae_scratch_render_cb`) runs a per-sample cross-fade
  state machine: `FADE_OUT` ramps the scratch tail to silence (~10 ms), then
  `FADE_IN` ramps the resumed forward audio — popped straight from the just-seeked
  ring — up from silence, **waiting at silence if the ring has not refilled yet**
  (no gap-click), then `RING` hands the deck back to the resampler at the next
  block. `AE_SCRATCH_XFADE_FRAMES` = 480 (~10 ms/side).
- `scratch_end` issues the seek then arms `FADE_OUT` (keeping `s_scratch_playing`
  set through the handoff); the decode task **skips the scratch-buffer reset while
  `s_scratch_playing`** so the fade-out keeps reading the intact window (the ring
  is still flushed + refilled). The output task clears `s_scratch_playing` only
  once `FADE_IN` reaches full gain.
- Verified: full host suite + firmware build pass; static guard. **HW feel test
  (click-free release) still pending.**
- Risk: **HIGH** (real-time). Mitigated: flag + additive + one deck first + short
  cross-fades + underrun → silence.

Small residual (Phase 5): the fade-in reads the ring without counting it toward
the deck position, so the playhead trails the audio by ~the handoff length
(~10–20 ms) until the next seek — cosmetic, self-corrects.

### Phase 5 — Feel, edge cases, HW validation ✅ DONE (2026-07-11)

The Phase 3/4 velocity model (each jog tick = a velocity impulse that decays per
sample) did not survive HW: the FLX4 platter emits many small (±1) ticks at a
high rate, so the impulse+decay model chopped between ticks and felt rubbery /
"didn't follow". A serial capture of a real scratch (≈250 ±1 ticks/s) showed the
first estimator (`ticks ÷ samples-since-last-tick`) divided by a bursty,
noisy render-call count — a tick just after an output block gave an ~8× velocity
spike, a tick after a gap gave ~0 — so the head alternately froze and shot to the
window edge.

**Rewritten to a FIXED-WINDOW rate estimate** (`audio_scratch.c`): jog ticks are
banked (atomic); every `rate_window_samples` the banked ticks become
`velocity = ticks × frames_per_tick / window` (rate over a CONSTANT time base),
which the playback velocity slews toward. Between tickless windows the velocity
HOLDS so steady motion stays continuous; after `hold_windows` empty windows the
platter counts as stopped → silence. HW-calibrated defaults: `frames_per_tick`
250, window 256 (~5 ms, longer than the ~4 ms tick interval so most windows carry
a tick and a resting hand does not judder in place), slew 0.18, |v|≤6×, hold
3 windows (~16 ms).

Verified on HW: backward / around-a-cued-point scratch sounds like a scratch;
still-held platter is silent; release is click-free; no regression to loops or
normal playback.

**Historical follow-ups (resolved by remediation batches 3B–3J):**
- The former ~155 ms forward-from-live limit is replaced by roughly two seconds
  of canonical decode lookahead, with the remaining four-second store retained
  as history.
- The waveform now reads the audible scratch-head position and explicitly
  disables forward interpolation while that coordinate is authoritative.
- Both-deck playback/scratch stress passed without WDT or monitor PCM drops;
  observed output-late maximum was ~12.13 ms.

### Reliability remediation batch 1 (implemented, HW validation pending)

- S3 and P4 queue saturation now **accumulates** relative jog deltas instead of
  replacing an earlier delta with the newest one. Absolute fader/pitch values
  retain latest-value semantics.
- `JOG_TOUCH` edges evict high-rate traffic when a full queue is saturated by
  jog/fader events; FLX4 disconnect forces both platter touches released.
- `JOG_BEND` remains a bend even while the platter top is touched; only the
  touched `JOG_SCRATCH` stream drives scratch.
- Scratch begin now freezes/awaits an in-progress decode capture batch and
  rejects missing, empty, future or evicted PCM windows, falling back to the
  Phase-1 platter hold rather than routing to silence.
- Scratch release resets the frozen history after fade-out and before capture
  resumes at the seek target, preventing discontinuous PCM timelines from being
  appended into one circular window.
- Release fade-in now consumes the normal pitch/sample-rate resampler and counts
  those source frames toward playback position. Scratch velocity calibration is
  scaled from the 48 kHz hardware baseline to the source rate.
- Added a scratch-enabled `deck_core_dual` host-test build and disconnect/side-
  ring regressions. Full firmware hardware feel/stress validation remains open.

### Timeline remediation batch 2 (implemented, HW validation pending)

- Scratch capture buffers now carry a non-zero generation counter which changes
  on every discontinuity/reset and is exposed in audio diagnostics together with
  active/frozen state, used/capacity and scratch-head distance.
- Async seeks carry an explicit reason (`USER`, `LOOP`, `SCRATCH_RELEASE`, or
  `SCRATCH_ABORT`) instead of inferring behavior from `s_scratch_playing`.
- A user/hot-cue/beat-jump seek received during scratch is deferred to the output
  block boundary. The output task tears down the scratch reader, mutes that deck
  while decode flushes/refills, and decode establishes a fresh capture generation
  before normal output resumes.
- Scratch release keeps its frozen generation only through fade-out; its existing
  post-fade reset then starts capture at the release target.
- Gapless loop wrap still preserves the normal PCM ring, but now resets scratch
  capture when scratch is inactive so loop-end and loop-start PCM are never
  presented as one contiguous scratch timeline.
- Host coverage verifies generation changes/reset guards; the full P4 host suite
  and P4 firmware build pass. Hardware loop/seek/scratch stress remains open.

### Canonical PCM timeline batch 3A (foundation implemented, not active yet)

- Added `audio_pcm_timeline.c/.h`, a pure-C, caller-storage-backed stereo PCM
  timeline intended for a per-deck PSRAM allocation. It uses monotonic absolute
  frame sequence numbers (`oldest_seq`, `play_seq`, `write_seq`) rather than
  ambiguous wrapped ring offsets.
- The producer may evict only history that normal playback has already consumed.
  A full timeline refuses additional writes instead of overwriting the unplayed
  forward runway. Normal playback advances `play_seq`; scratch/random access can
  read retained frames or reposition the playback cursor by an absolute sequence.
- Reset/discontinuity increments the timeline generation so positions from two
  decoder epochs cannot be treated as one continuous PCM range.
- Host tests cover history/future accounting, physical wrap, history seeking,
  generation reset and protection of unplayed PCM. The core is compiled into the
  P4 `audio_engine` component and the full P4 host suite and firmware build pass.
- This batch deliberately does **not** connect the timeline to decode, output or
  scratch yet. Consequently the running firmware still uses the existing PCM
  ring plus scratch-history buffer and its forward scratch runway remains about
  155–171 ms. There is no hardware behavior change in batch 3A.

Integration contract for batch 3B:

- The decode task is the sole producer and owns advancement of `write_seq`.
- The output task is the sole normal-playback consumer and owns advancement of
  `play_seq`; it must never block and treats missing PCM as an underrun/silence.
- Touch-down freezes producer mutation for that deck, snapshots the retained
  generation/range and seeds scratch from the current `play_seq`. Scratch reads
  retained absolute sequences without advancing normal playback.
- Scratch release atomically selects the scratch sequence as the new `play_seq`,
  then resumes producer/output operation through the existing handoff fade.
- User/loop/abort discontinuities reset the timeline generation before new PCM is
  published. Old-generation scratch coordinates must be rejected.
- PSRAM capacity will be sized for roughly two seconds of future PCM per deck;
  unused capacity remains playable history. Allocation failure must fall back to
  the existing audio path rather than prevent deck startup.

### Canonical PCM timeline batch 3B (integrated, HW validation pending)

- The four-second, stereo int16 canonical timeline is now allocated once per
  deck in PSRAM (192,000 source frames / 768 KiB at the configured 48 kHz
  maximum). Allocation failure selects the legacy PCM ring plus independent
  scratch buffer for that deck; track loading and normal playback remain usable.
- In canonical mode the decoder is the sole writer and throttles at about two
  seconds of unplayed source PCM. Normal output/resampling consumes `play_seq`;
  consumed capacity automatically becomes retained scratch history instead of
  being copied to a second buffer.
- The scratch DSP receives a read-only metadata view over the same PSRAM store.
  Touch-down freezes decode and seeds the scratch head exactly from
  `write_seq - play_seq`, avoiding millisecond rounding and exposing both the
  retained history and the predecoded forward runway.
- Canonical scratch release does not seek or decode the track again. It moves
  `play_seq` directly to the frame selected by the scratch head, resets the
  resampler and uses the existing fade-out/fade-in handoff. The legacy fallback
  retains its decoder-seek release behavior.
- Timeline cursors use acquire/release publication for the SPSC decode/output
  boundary. Diagnostics expose whether each deck uses the timeline plus its
  history, future and generation counters.
- Full P4 host tests and the P4 firmware build pass. Required hardware checks:
  forward scratch runway, release continuity, both-deck PSRAM pressure, loop
  wrap, external seek during scratch and output underrun/CPU counters.

### Canonical PCM timeline batch 3C (real-time hot-path remediation)

- First 3B hardware run exposed CPU0 task-watchdog events: `ae_output` blocks
  reached 105–110 ms versus an 11.6 ms deadline. This was not a stack overflow;
  the RV32 output path was paying for 64-bit cursor atomics and 64-bit modulo on
  every source frame read from the timeline.
- Timeline cursors are now native 32-bit SPSC publications (more than 24 hours
  before reset at 48 kHz), with independent physical oldest/play/write indices.
  Normal `push`/`pop` advances those indices with branch-wrap, so its generated
  P4 code contains no division, modulo or 64-bit atomic helper.
- Output `pop` loads only the producer's published `write_seq`; the producer is
  prohibited from evicting at/after `play_seq`, so no per-frame `oldest_seq`
  load is required. Random absolute reads and scratch repositioning retain the
  canonical sequence API but stay outside the normal output hot path.
- Output-late logs now report canonical future/history values instead of the
  inactive legacy ring counters. Host coverage includes playback cursor wrap.
- Host suite and P4 firmware build pass. First post-flash run sustained playback
  without another WDT or 100+ ms block; the only late blocks were 11.8 and
  12.0 ms against the 11.6 ms warning threshold, and the monitor PCM link
  reported zero drops. Timeline telemetry showed ~86k future and ~106k history
  frames. One touch attempt fell back with `scratch unavailable`; that separate
  begin-gating edge still needs focused reproduction.

### Scratch touch/re-grab batch 3D (implemented, HW confirmed)

- Reason-coded hardware logging showed that `scratch unavailable` was not an
  empty timeline: a second touch arrived while the previous scratch release
  handoff still owned `s_scratch_playing`.
- `deck_core` now treats platter touch as an edge-driven level: duplicate press
  and duplicate release reports are idempotent and cannot start a second begin,
  handoff or platter-hold transition.
- A legitimate fast re-grab during fade-out/fade-in now cancels the handoff and
  returns immediately to the still-valid frozen scratch head. An output-owned
  re-grab request prevents a concurrently observed `RING` phase from tearing
  that state down; external transport abort remains higher priority.
- Canonical scratch begin also refreshes its compatibility metadata view from
  the frozen authoritative timeline before validation, and every remaining
  rejection path logs its exact reason.
- Duplicate-edge regressions pass in both scratch-enabled and Phase-1 host
  builds; full P4 host suite, firmware build and flash pass.

### Scratch waveform position batch 3E (implemented, HW confirmed)

- While steady scratch or scratch fade-out is audible, the public deck position
  now derives from the frozen window's `newest_pos_ms` minus the published
  fractional scratch-head distance. Normal `output_frames_since_seek` remains
  authoritative during forward playback and release fade-in.
- Deck status and direct position consumers now observe the same audible scratch
  coordinate. The mixer snapshot explicitly marks that coordinate as scratch-
  authoritative; Overview disables its normal forward speed extrapolation only
  while that flag is set, instead of treating speed zero as a missing value and
  falling back to the pitch-fader speed.
- Full P4 host suite (including Overview motion/interpolator coverage), firmware
  build and flash pass. Visual smoothness still requires an eyes-on hardware
  check without a serial session resetting/disturbing the USB host.

### Scratch window-edge batch 3F (implemented, HW confirmed)

- A fast forward or reverse throw can legitimately reach the newest/oldest edge
  of the frozen canonical window. Previously the held velocity kept pushing
  into the clamp; same-direction ticks repeatedly retriggered that path and
  could sound like a short stop followed by persistent chatter.
- The DSP now latches the reached edge, zeros velocity and emits clean silence
  for all further outward motion. Only an inward velocity estimate releases the
  latch and resumes interpolation into available PCM; a new seed/end clears it.
- Host coverage drives eight repeated outward ticks at the newest edge, verifies
  stable silence/head position, then verifies an inward reversal immediately
  resumes audio. Full P4 host suite, firmware build and flash pass.

### Dual-deck observability batch 3G (implemented, HW confirmed)

- Per-deck diagnostics now count actual PCM source-pop underruns. This exposes
  starvation that the resampler intentionally masks by holding its last frame.
- Scratch DSP counts transitions into either retained-window edge; repeated
  outward ticks while the edge latch is active do not inflate the count.
- Periodic output telemetry now reports future/history, underruns and edge hits
  for both canonical timelines, alongside active decks, consumed frames, limiter,
  late-block and heap/PSRAM data. It no longer prints inactive legacy-ring usage.
- Counters reset on audio-engine init and are included in the public diagnostics
  snapshot. Full host suite, P4 firmware build and flash pass; simultaneous
  simultaneous two-deck playback/scratch stress passed without WDT or monitor
  PCM drops; the worst observed output block was ~12.13 ms.

### Paused/CUE scratch batch 3H (implemented, HW confirmed)

- A loaded paused deck can now enter the same frozen canonical scratch reader as
  a playing deck. The output mixer treats that explicitly marked cue-scratch as
  active even though transport remains paused; stopped/EOF teardown for normal
  scratch keeps its previous behavior.
- Touch-down snapshots the origin timeline cursor and track position. Jog motion
  is routed to scratch while paused instead of issuing repeated decoder seeks.
- Release restores the origin cursor/position, resets the resampler, fades the
  scratch source only to silence and leaves transport paused. It deliberately
  skips the forward fade-in used by a scratch that began during playback.
- Reload/stop/reset clears all cue-scratch origin and return flags. Fast re-grab
  and external transport-abort priority remain compatible with the new state.
- Scratch-enabled host coverage verifies paused begin/move/end without a seek;
  the Phase-1 host build retains its legacy silent paused scrub. Full P4 host
  suite, firmware build and flash pass.

**3H follow-up — cue-centered pre-roll.** Hardware confirmed cue scratch audio,
but a fresh CUE seek initially exposed only the ~2 s forward runway (roughly four
beats at 120 BPM) because no PCM existed before the seek target. A paused user
seek now decodes up to 2 s before the requested cue, publishes that as retained
history, moves canonical `play_seq` to the cue inside the window, then fills the
normal ~2 s future runway. Immediate touch waits briefly for the memory-backed
pre-roll to become ready. The same 4 s PSRAM allocation now provides a centered
~2 s reverse + ~2 s forward cue-scratch range. Host suite, P4 build and flash
pass; hardware confirmed the expected centered reverse/forward range.

### Active-loop scratch batch 3I (implemented, HW confirmed)

- Canonical PCM already preserves the audible loop-end → loop-start sequence,
  but scratch position/release previously subtracted head time linearly from the
  newest track position. A head crossing backward through `loop_start` could
  therefore report or release outside the loop.
- Shared DSP mapping now treats a valid active loop as modular track time:
  backward travel through `loop_start` wraps to `loop_end`, including movement
  spanning multiple complete loop lengths. Without a loop it retains saturating
  linear track-time behavior.
- Both the UI/public scratch position and release target call the same mapping,
  so waveform, deck state and resumed canonical playhead cannot disagree at the
  boundary. Host DSP tests cover one wrap, multiple wraps and non-loop fallback;
  full P4 suite, firmware build and flash pass.

### Pitch-fader handoff batch 3J (implemented, HW confirmed)

- Scratch velocity remains exclusively platter-driven. Pitch-fader changes made
  while a deck is scratching no longer mutate the normal resampler rate behind
  the frozen source; the latest per-deck value is banked as pending.
- At the scratch fade-out zero crossing, the output owner commits the pending
  pitch and resets the resampler before forward fade-in reveals audio. Paused/CUE
  scratch commits it at fade-out for the next PLAY. External seek/abort commits
  pending pitch before refill; reload/stop discards stale pending state.
- Multiple fader movements during one scratch use latest-value semantics and
  remain deck-local. Full P4 host suite, firmware build and flash pass.

## Key parameters (HW-calibrated — `audio_scratch.h`)

- `SCRATCH_SECONDS` = 4 (buffer length).
- `FRAMES_PER_TICK` = 250 (velocity = tick_rate × this ÷ sample_rate).
- `RATE_WINDOW` = 256 samples (~5 ms rate-estimate window).
- `SLEW_COEF` = 0.18 (per-sample approach to the target velocity).
- `VELOCITY_MAX` = 6× ; `HOLD_WINDOWS` = 3 (~16 ms to declare the platter stopped).
- Handoff cross-fade = 480 frames (~10 ms).
- Interpolation: linear (matches `audio_resampler_next`).

## Thread model

- **decode task:** is the sole canonical timeline producer and maintains the
  target forward runway; it freezes publication during scratch.
- **control task (`deck_core`):** sets touch state; feeds scratch velocity / bend.
- **output task (core 0):** owns normal `play_seq`, reads the frozen canonical
  store for scratch and performs handoff cross-fades.
- Shared state uses the existing relaxed-atomic / plain-float pattern (like
  `s_jog_bend`); **no locks in the hot path.**

## Closed decisions

- Forward lookahead/history share one four-second canonical PSRAM window.
- A still touched platter is silent; a reached window edge latches cleanly.
- Pitch changes are deferred until the release fade reaches silence.
- Active-loop scratch position/release wraps modularly inside the loop.
- Paused/CUE scratch returns to a centered cue origin and stays paused.

## Safety / rollback

Vinyl routing remains behind `CONFIG_AUDIO_SCRATCH_ENABLED`. The canonical PCM
timeline is now also the normal playback source when its PSRAM allocation
succeeds; allocation failure falls back per deck to the legacy ring/scratch
stores so track startup remains available.
