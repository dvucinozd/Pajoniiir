#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t accepting;
    uint32_t active_producers;
} audio_recorder_stop_gate_t;

void audio_recorder_stop_gate_init(audio_recorder_stop_gate_t *gate);
bool audio_recorder_stop_gate_open(audio_recorder_stop_gate_t *gate);
void audio_recorder_stop_gate_close(audio_recorder_stop_gate_t *gate);
bool audio_recorder_stop_gate_try_enter(audio_recorder_stop_gate_t *gate);
void audio_recorder_stop_gate_leave(audio_recorder_stop_gate_t *gate);
bool audio_recorder_stop_gate_is_quiescent(const audio_recorder_stop_gate_t *gate);
uint32_t audio_recorder_stop_gate_active(const audio_recorder_stop_gate_t *gate);

#ifdef __cplusplus
}
#endif
