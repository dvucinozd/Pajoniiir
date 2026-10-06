#include "audio_scratch_buffer.h"

void audio_scratch_buffer_init(audio_scratch_buffer_t *b, int16_t *storage,
                               uint32_t capacity_frames)
{
    if (!b) return;
    b->frames = storage;
    b->capacity = capacity_frames;
    b->sample_rate = 0u;
    b->generation = 0u;
    audio_scratch_buffer_reset(b);
}

void audio_scratch_buffer_reset(audio_scratch_buffer_t *b)
{
    if (!b) return;
    b->write_index = 0u;
    b->filled = 0u;
    b->newest_pos_ms = 0u;
    b->newest_valid = false;
    b->generation++;
    if (b->generation == 0u) b->generation = 1u;
}

void audio_scratch_buffer_set_sample_rate(audio_scratch_buffer_t *b,
                                          uint32_t sample_rate)
{
    if (!b) return;
    b->sample_rate = sample_rate;
}

void audio_scratch_buffer_mark_newest_ms(audio_scratch_buffer_t *b,
                                         uint32_t pos_ms)
{
    if (!b) return;
    b->newest_pos_ms = pos_ms;
    b->newest_valid = true;
}

uint32_t audio_scratch_buffer_used(const audio_scratch_buffer_t *b)
{
    return b ? b->filled : 0u;
}

uint32_t audio_scratch_buffer_generation(const audio_scratch_buffer_t *b)
{
    return b ? b->generation : 0u;
}

bool audio_scratch_buffer_read_frame_back(const audio_scratch_buffer_t *b,
                                          uint32_t frames_back,
                                          int16_t *out_left, int16_t *out_right)
{
    if (!b || !b->frames || !out_left || !out_right || b->capacity == 0u ||
        frames_back >= b->filled) {
        return false;
    }
    uint32_t newest_idx = b->write_index == 0u ? b->capacity - 1u
                                               : b->write_index - 1u;
    uint32_t idx = newest_idx >= frames_back ? newest_idx - frames_back
                                             : newest_idx + b->capacity - frames_back;
    *out_left = b->frames[idx * 2u];
    *out_right = b->frames[idx * 2u + 1u];
    return true;
}
