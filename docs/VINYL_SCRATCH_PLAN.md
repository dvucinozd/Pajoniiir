# Vinyl / Scratch Mode — Implementation Plan

Status: **Phases 1–5 done (HW-verified 2026-07-11)** — scratch works: backward /
around a cued point sounds like a real scratch, a still-held platter is silent,
click-free release. Two inherent limits are documented follow-ups (see Phase 5).
Phased roadmap for real turntable scratch on the
P4. Written 2026-07-10 so work can resume cleanly.

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

**Documented follow-ups (inherent limits, not yet addressed):**
- *Forward-from-live scratch is short (~155 ms).* You cannot scratch forward past
  "now" because the future is not decoded; the runway is the decode lead (the PCM
  ring depth). Bumping the ring to 2× gave no perceptible gain (a blink at scratch
  speed) and costs internal RAM + short-loop-set latency, so it was reverted. A
  real fix needs a dedicated multi-second forward decode-ahead into the scratch
  buffer, decoupled from the ring — a larger, riskier change.
- *Waveform judders slightly while scratching.* The audible playhead is frozen
  during scratch (the scratch source consumes nothing from the ring) but the UI
  still reports ~1× speed, so the position interpolator extrapolates and snaps
  back. A naïve "report 0 speed during scratch" made it worse and was reverted;
  wants a considered fix (e.g. drive the waveform from the scratch head position).
- Still open from before: both-decks stress + CPU headroom sweep on the core-0
  output task; update `firmware/main-deck-p4/CLAUDE.md`.

## Key parameters (HW-calibrated — `audio_scratch.h`)

- `SCRATCH_SECONDS` = 4 (buffer length).
- `FRAMES_PER_TICK` = 250 (velocity = tick_rate × this ÷ sample_rate).
- `RATE_WINDOW` = 256 samples (~5 ms rate-estimate window).
- `SLEW_COEF` = 0.18 (per-sample approach to the target velocity).
- `VELOCITY_MAX` = 6× ; `HOLD_WINDOWS` = 3 (~16 ms to declare the platter stopped).
- Handoff cross-fade = 480 frames (~10 ms).
- Interpolation: linear (matches `audio_resampler_next`).

## Thread model

- **decode task:** fills the ring + the scratch buffer (position-tagged).
- **control task (`deck_core`):** sets touch state; feeds scratch velocity / bend.
- **output task (core 0):** reads scratch (touched) or resampler-from-ring
  (normal); performs the handoff cross-fade.
- Shared state uses the existing relaxed-atomic / plain-float pattern (like
  `s_jog_bend`); **no locks in the hot path.**

## Open questions / decisions

- Forward scratch beyond decoded audio: lookahead vs clamp.
- Touch-and-hold-still: sound fully stops (velocity 0) vs slow creep.
- Freeze the pitch fader during scratch (probably yes).
- Reverse across a loop boundary / track start/end.
- Scratch while paused (cue scratch)? — Phase 4+.

## Safety / rollback

Additive + behind `CONFIG_AUDIO_SCRATCH_ENABLED`. Scratch inactive → the output
path is unchanged. Phases 1–3 never alter normal playback output.
