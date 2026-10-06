#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * A PLAY request may race the decode producer after load/seek. The output task
 * must not mark the deck active until it has a small runway, otherwise every
 * failed per-frame pop becomes a counted and audible startup underrun.
 */
bool audio_start_gate_ready(uint32_t future_frames,
                            uint32_t minimum_frames,
                            bool source_eof);
