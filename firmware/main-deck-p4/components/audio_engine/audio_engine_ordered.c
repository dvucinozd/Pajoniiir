/*
 * Production ordering wrapper for audio_engine.c.
 *
 * The Master Tempo command applicator is intentionally defined before the
 * concrete DSP storage definitions in audio_engine.c. These compatible
 * tentative declarations make that storage visible at the point of use while
 * the complete array definitions later in the included implementation remain
 * the single translation-unit definitions.
 *
 * The retired audio_output_remaining_delay_ms() helper always returned zero.
 * Hardware writes already pace the output loop, and the loop retains its
 * explicit periodic one-tick yield. Collapse the remaining monolithic call at
 * preprocessing time without restoring a public no-op API.
 */
#include "audio_engine.h"
#include "audio_keylock.h"
#include "audio_resampler.h"

#if !defined(AUDIO_ENGINE_PC_TEST)
static audio_resampler_state_t s_resamplers[];
static audio_keylock_t s_keylocks[];
static uint32_t s_master_tempo_command_epoch[];
static uint32_t s_master_tempo_applied_epoch[];
#endif

#define audio_output_remaining_delay_ms(...) 0u
#include "audio_engine.c"
#undef audio_output_remaining_delay_ms
