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

void audio_scratch_buffer_push(audio_scratch_buffer_t *b, int16_t left,
                               int16_t right)
{
    if (!b || !b->frames || b->capacity == 0u) return;

    uint32_t idx = b->write_index;
    b->frames[idx * 2u] = left;
    b->frames[idx * 2u + 1u] = right;

    /* Advance with a branch instead of a modulo — capacity is not a power of
     * two and this runs once per decoded frame in the decode task. */
    idx++;
    if (idx >= b->capacity) {
        idx = 0u;
    }
    b->write_index = idx;

    if (b->filled < b->capacity) {
        b->filled++;
    }
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

bool audio_scratch_buffer_index_for_ms(const audio_scratch_buffer_t *b,
                                       uint32_t pos_ms, uint32_t *out_index)
{
    if (!b || !out_index || !b->newest_valid || b->filled == 0u ||
        b->sample_rate == 0u || b->capacity == 0u) {
        return false;
    }
    if (pos_ms > b->newest_pos_ms) {
        return false;  /* future frames are not captured (Phase 2) */
    }

    uint64_t back_frames =
        ((uint64_t)(b->newest_pos_ms - pos_ms) * b->sample_rate) / 1000ull;
    if (back_frames >= b->filled) {
        return false;  /* older than the buffered window */
    }

    /* The newest frame lives at (write_index - 1) mod capacity; step back. */
    uint32_t newest_idx = b->write_index == 0u ? b->capacity - 1u
                                               : b->write_index - 1u;
    uint32_t back = (uint32_t)back_frames;
    uint32_t idx = newest_idx >= back ? newest_idx - back
                                      : newest_idx + b->capacity - back;
    *out_index = idx;
    return true;
}

bool audio_scratch_buffer_read(const audio_scratch_buffer_t *b, uint32_t index,
                               int16_t *out_left, int16_t *out_right)
{
    if (!b || !b->frames || !out_left || !out_right || index >= b->capacity) {
        return false;
    }
    *out_left = b->frames[index * 2u];
    *out_right = b->frames[index * 2u + 1u];
    return true;
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
