#pragma once
#include "audio_engine.h"

/* Observation-time diagnostics only. Call without the audio state mutex. */
void audio_engine_snapshot_memory(audio_engine_diagnostics_snapshot_t *snapshot);
