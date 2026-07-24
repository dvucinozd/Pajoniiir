#include "audio_pcm_timeline.h"

void audio_pcm_timeline_init(audio_pcm_timeline_t *t, int16_t *storage,
                             uint32_t capacity_frames)
{
    if (!t) return;
    t->frames = storage;
    t->capacity = capacity_frames;
    t->generation = 0u;
    audio_pcm_timeline_reset(t);
}

void audio_pcm_timeline_reset(audio_pcm_timeline_t *t)
{
    if (!t) return;
    t->oldest_seq = 0u;
    t->play_seq = 0u;
    t->write_seq = 0u;
    t->play_index = 0u;
    t->write_index = 0u;
    t->generation++;
    if (t->generation == 0u) t->generation = 1u;
}

bool audio_pcm_timeline_push(audio_pcm_timeline_t *t, int16_t left, int16_t right)
{
    if (!t || !t->frames || t->capacity == 0u) return false;

    uint32_t write_seq = __atomic_load_n(&t->write_seq, __ATOMIC_RELAXED);
    uint32_t oldest_seq = __atomic_load_n(&t->oldest_seq, __ATOMIC_RELAXED);
    uint32_t play_seq = __atomic_load_n(&t->play_seq, __ATOMIC_ACQUIRE);
    uint32_t used = write_seq - oldest_seq;
    if (used >= t->capacity) {
        /* Never overwrite the next frame normal playback still needs. */
        if (oldest_seq >= play_seq) return false;
        oldest_seq++;
        __atomic_store_n(&t->oldest_seq, oldest_seq, __ATOMIC_RELEASE);
    }

    uint32_t index = t->write_index;
    t->frames[index * 2u] = left;
    t->frames[index * 2u + 1u] = right;
    if (++index >= t->capacity) index = 0u;
    t->write_index = index;
    __atomic_store_n(&t->write_seq, write_seq + 1u, __ATOMIC_RELEASE);
    return true;
}

bool audio_pcm_timeline_read(const audio_pcm_timeline_t *t, uint64_t seq,
                             audio_mixer_frame_t *out)
{
    if (!t || !t->frames || !out || t->capacity == 0u) {
        return false;
    }
    uint32_t oldest_seq = __atomic_load_n(&t->oldest_seq, __ATOMIC_ACQUIRE);
    uint32_t write_seq = __atomic_load_n(&t->write_seq, __ATOMIC_ACQUIRE);
    if (seq > UINT32_MAX || seq < oldest_seq || seq >= write_seq) {
        return false;
    }
    /* play_seq/play_index are owned by the same output task that performs
     * random key-lock reads. Use that stable physical anchor rather than the
     * producer-owned eviction cursor; the retained window is at most one
     * capacity wide, so at most one wrap correction is required. */
    uint32_t play_seq = __atomic_load_n(&t->play_seq, __ATOMIC_RELAXED);
    int64_t index_from_play = (int64_t)t->play_index +
                              ((int64_t)(uint32_t)seq - (int64_t)play_seq);
    if (index_from_play < 0) index_from_play += t->capacity;
    if (index_from_play >= t->capacity) index_from_play -= t->capacity;
    uint32_t index = (uint32_t)index_from_play;
    out->left = t->frames[index * 2u];
    out->right = t->frames[index * 2u + 1u];
    /* The producer may evict and overwrite this exact slot after our first
     * range check. Discard a possibly torn frame if its sequence expired while
     * it was being copied; callers already treat false as unavailable PCM. */
    return seq >= __atomic_load_n(&t->oldest_seq, __ATOMIC_ACQUIRE);
}

bool audio_pcm_timeline_pop(audio_pcm_timeline_t *t, audio_mixer_frame_t *out)
{
    if (!t || !out) {
        return false;
    }
    uint32_t play_seq = __atomic_load_n(&t->play_seq, __ATOMIC_RELAXED);
    uint32_t write_seq = __atomic_load_n(&t->write_seq, __ATOMIC_ACQUIRE);
    /* Producer can evict only frames strictly before play_seq, therefore the
     * output owner never needs to load oldest_seq in this per-frame path. */
    if (play_seq >= write_seq) return false;
    uint32_t index = t->play_index;
    out->left = t->frames[index * 2u];
    out->right = t->frames[index * 2u + 1u];
    if (++index >= t->capacity) index = 0u;
    t->play_index = index;
    __atomic_store_n(&t->play_seq, play_seq + 1u, __ATOMIC_RELEASE);
    return true;
}

bool audio_pcm_timeline_set_playhead(audio_pcm_timeline_t *t, uint64_t seq)
{
    if (!t || seq > UINT32_MAX || seq < audio_pcm_timeline_oldest_seq(t) ||
        seq > audio_pcm_timeline_write_seq(t)) return false;
    uint32_t seq32 = (uint32_t)seq;
    uint32_t index = seq32 % t->capacity;
    t->play_index = index;
    __atomic_store_n(&t->play_seq, seq32, __ATOMIC_RELEASE);
    return true;
}

bool audio_pcm_timeline_set_playhead_frames_back(audio_pcm_timeline_t *t,
                                                 uint32_t frames_back)
{
    if (!t) return false;
    uint64_t write_seq = audio_pcm_timeline_write_seq(t);
    if (write_seq == 0u) return false;
    uint64_t newest = write_seq - 1u;
    if ((uint64_t)frames_back > newest) return false;
    uint64_t target = newest - frames_back;
    return audio_pcm_timeline_set_playhead(t, target);
}

uint64_t audio_pcm_timeline_oldest_seq(const audio_pcm_timeline_t *t)
{
    return t ? __atomic_load_n(&t->oldest_seq, __ATOMIC_ACQUIRE) : 0u;
}

uint64_t audio_pcm_timeline_play_seq(const audio_pcm_timeline_t *t)
{
    return t ? __atomic_load_n(&t->play_seq, __ATOMIC_ACQUIRE) : 0u;
}

uint64_t audio_pcm_timeline_write_seq(const audio_pcm_timeline_t *t)
{
    return t ? __atomic_load_n(&t->write_seq, __ATOMIC_ACQUIRE) : 0u;
}

uint32_t audio_pcm_timeline_history_frames(const audio_pcm_timeline_t *t)
{
    if (!t) return 0u;
    uint32_t play_seq = __atomic_load_n(&t->play_seq, __ATOMIC_ACQUIRE);
    uint32_t oldest_seq = __atomic_load_n(&t->oldest_seq, __ATOMIC_ACQUIRE);
    if (play_seq < oldest_seq) return 0u;
    return play_seq - oldest_seq;
}

uint32_t audio_pcm_timeline_future_frames(const audio_pcm_timeline_t *t)
{
    if (!t) return 0u;
    uint32_t write_seq = __atomic_load_n(&t->write_seq, __ATOMIC_ACQUIRE);
    uint32_t play_seq = __atomic_load_n(&t->play_seq, __ATOMIC_ACQUIRE);
    if (write_seq < play_seq) return 0u;
    return write_seq - play_seq;
}

uint32_t audio_pcm_timeline_used_frames(const audio_pcm_timeline_t *t)
{
    if (!t) return 0u;
    uint32_t write_seq = __atomic_load_n(&t->write_seq, __ATOMIC_ACQUIRE);
    uint32_t oldest_seq = __atomic_load_n(&t->oldest_seq, __ATOMIC_ACQUIRE);
    if (write_seq < oldest_seq) return 0u;
    return write_seq - oldest_seq;
}

uint32_t audio_pcm_timeline_generation(const audio_pcm_timeline_t *t)
{
    return t ? t->generation : 0u;
}

uint32_t audio_pcm_timeline_drop_newest(audio_pcm_timeline_t *t, uint32_t frames)
{
    if (!t || !t->frames || t->capacity == 0u || frames == 0u) return 0u;
    /* Producer-side rewind of the forward runway. play_seq is the floor: frames
     * at or before it have been handed to the output and are not ours to take
     * back. History below play_seq is untouched, so scratch keeps its window.
     * The caller must exclude the consumer while this runs. */
    uint32_t write_seq = __atomic_load_n(&t->write_seq, __ATOMIC_RELAXED);
    uint32_t play_seq = __atomic_load_n(&t->play_seq, __ATOMIC_ACQUIRE);
    uint32_t runway = write_seq - play_seq;
    if (frames > runway) frames = runway;
    if (frames == 0u) return 0u;
    uint32_t index = t->write_index;
    index = index >= frames ? index - frames : index + t->capacity - frames;
    t->write_index = index;
    __atomic_store_n(&t->write_seq, write_seq - frames, __ATOMIC_RELEASE);
    return frames;
}
