#pragma once
#include "audio_engine.h"

/* Observation-time diagnostics only. Call without the audio state mutex.
 * Largest internal/DMA remain sampled; PSRAM largest is unavailable to avoid
 * an unbounded IRQ-masking TLSF pool walk during ongoing UAC silence/audio. */
void audio_engine_snapshot_memory(audio_engine_diagnostics_snapshot_t *snapshot);
