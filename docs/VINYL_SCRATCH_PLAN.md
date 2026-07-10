# Vinyl / Scratch Mode — Implementation Plan

Status: **planned** (not started). Phased roadmap for real turntable scratch on
the P4. Written 2026-07-10 so work can resume cleanly.

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
- **`JOG_TOUCH`:** the S3 emits `CTRL_ID_DECK{1,2}_JOG_TOUCH` (button
  press/release, from `FLX4_BTN_JOG_TOUCH` 0x36). **`deck_core` does not consume
  it yet** — this is the signal that will gate scratch (touched) vs bend (ring).
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

### Phase 1 — Touch state + platter brake/scrub (tactile, no audible scratch)
Low risk; gets the touch signal + "grab the platter" feel working end-to-end and
is the foundation for the scratch handoff.
- `deck_core`: consume `CTRL_ID_DECK*_JOG_TOUCH` → per-deck `s_jog_touched[]`.
- Touch-down while playing → enter a **platter-hold** state: mute the deck output
  (keep the decoder/position live for instant resume) and remember it was
  playing. Jog while touched → **scrub** the position (seek), like the paused
  scrub. Touch-up → unmute and resume from the new position.
- `audio_engine`: a per-deck hold/mute flag that silences the deck frame while
  held (preferred over full pause so resume is instant).
- Gate: touched → scrub+hold; not touched + playing → pitch bend (existing).
- Files: `deck_core.c`, `audio_engine.c/.h`. Tests: `deck_core_dual` (touch mutes
  + holds, jog scrubs, release resumes; bend still works untouched). HW: platter
  stops audio, jog moves position, release resumes. No flag needed (safe).

### Phase 2 — Scratch buffer (capture only, passive)
Build the random-access PCM history without changing playback.
- New `audio_scratch_buffer` component: PSRAM circular stereo-int16 buffer,
  ~`SCRATCH_SECONDS` (start 4 s ≈ 176 400 frames ≈ 700 KB). Stores frames plus
  the track position of the newest frame so a target `ms` → buffer index.
- The decode/consume path writes each produced frame into the buffer (in
  addition to the ring), keeping a rolling window ending near the playhead.
- Passive: playback unchanged; verify the buffer fills correctly via diagnostics.
- Files: new `audio_scratch_buffer.c/.h`, `audio_engine` write wiring,
  `CMakeLists.txt`. Tests: host (push, wrap, position→index mapping, bounds).
- Risk: low–medium (memory + one cheap write in the hot path).

### Phase 3 — Scratch DSP (bidirectional interpolated read)
The pure scratch read, host-tested in isolation.
- `audio_scratch` engine: given a fractional read head + per-output-sample
  velocity, produce output frames by linear interpolation of the buffer,
  advancing the head by velocity (negative → reverse). Clamp to the buffered
  window; silence (or hold) outside it.
- Velocity model: jog delta (ticks) → target platter velocity in
  frames/output-sample, with smoothing/decay so it feels analog (a still-but-
  touched platter → velocity 0 → sound stops; released → handoff).
- Files: `audio_scratch.c/.h` (pure DSP, no ESP deps), tests (forward, reverse,
  interpolation, out-of-range). Risk: medium but isolated + host-tested.

### Phase 4 — Integrate scratch into the real-time output (behind the CONFIG flag)
Wire it so touching the platter actually scratches the audio.
- `CONFIG_AUDIO_SCRATCH_ENABLED` (Kconfig); additive so scratch-inactive == today.
- Output task: a deck in scratch (touched) produces its frames from the scratch
  engine (buffer at the jog-driven head) instead of resampler-from-ring; else the
  normal path.
- `deck_core`: while touched, feed jog deltas to the scratch velocity
  (`audio_engine_deck_scratch_move(deck, delta)`) instead of scrub/bend.
- Handoff: touch-down → seed the head from the current play position (ensure the
  buffer covers it); touch-up → seek normal playback to the head position, resume
  forward, cross-fade a few ms.
- Files: `audio_engine` output task + scratch state, `deck_core`, Kconfig. Tests:
  host (scratch state machine + handoff position math); HW for feel.
- Risk: **HIGH** (real-time). Mitigate: flag + additive + one deck first + short
  cross-fades + underrun → silence.

### Phase 5 — Feel, edge cases, HW validation
- Tune velocity/sensitivity/decay; click-free reverse + handoff; underrun
  (scratch past the buffered window) → clamp/silence; both-decks stress; CPU
  headroom on the core-0 output task.
- Update `firmware/main-deck-p4/CLAUDE.md`; mark the vinyl-mode pending item done.

## Key parameters (initial — tune on hardware)

- `SCRATCH_SECONDS` = 4 (buffer length).
- Jog tick → Δvelocity; velocity decay per output sample; max forward/reverse
  speed.
- Handoff cross-fade ≈ 5–10 ms.
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
