#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "library.h"

#define FIXTURE_TRACK_COUNT 5

static library_track_t s_tracks[FIXTURE_TRACK_COUNT];
static int s_loaded_index;
static uint32_t s_generation;
static anlz_metadata_t s_current_meta;
static bool s_initialized;

static void copy_text(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) {
        return;
    }
    snprintf(dst, cap, "%s", src ? src : "");
}

static void fill_waveform(library_track_t *track, uint8_t seed)
{
    for (uint32_t i = 0; i < ANLZ_WAVEFORM_LOW_LEN; i++) {
        uint8_t height = (uint8_t)(3u + ((i * 7u + seed * 11u) % 27u));
        uint8_t whiteness = (uint8_t)((i / 13u + seed) & 0x07u);
        track->waveform_low[i] = (uint8_t)((whiteness << 5) | height);
    }
    track->has_waveform = 1;
}

static void set_track(int index, uint32_t id, const char *title,
                      const char *artist, const char *key, uint16_t bpm,
                      uint32_t duration_ms)
{
    library_track_t *track = &s_tracks[index];
    memset(track, 0, sizeof(*track));
    track->track_id = id;
    track->bpm = bpm;
    track->duration_ms = duration_ms;
    copy_text(track->title, sizeof(track->title), title);
    copy_text(track->artist, sizeof(track->artist), artist);
    copy_text(track->album, sizeof(track->album), "Pajoniiir Simulator");
    copy_text(track->key, sizeof(track->key), key);
    snprintf(track->path, sizeof(track->path), "fixture://track/%lu.wav",
             (unsigned long)id);
    snprintf(track->anlz_path, sizeof(track->anlz_path), "fixture://anlz/%lu",
             (unsigned long)id);
    fill_waveform(track, (uint8_t)(index + 1));
}

esp_err_t library_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    set_track(0, 1001, "Midnight Circuit", "Pajoniiir", "8A", 124, 243000);
    set_track(1, 1002, "Neon Harbor", "Signal Drift", "9A", 128, 198000);
    set_track(2, 1003, "Static Bloom", "Blue Current", "5B", 122, 271000);
    set_track(3, 1004, "Afterimage", "Phase Garden", "11A", 130, 225000);
    set_track(4, 1005, "Low Orbit", "Vector Youth", "3A", 118, 304000);
    s_initialized = true;
    s_generation++;
    return ESP_OK;
}

void library_clear(void)
{
    memset(s_tracks, 0, sizeof(s_tracks));
    s_initialized = false;
    s_loaded_index = 0;
    library_free_current_anlz();
    s_generation++;
}

uint32_t library_generation(void) { return s_generation; }
int library_count(void) { return s_initialized ? FIXTURE_TRACK_COUNT : 0; }

esp_err_t library_get(int index, library_track_t *out)
{
    if (!out || index < 0 || index >= library_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = s_tracks[index];
    return ESP_OK;
}

esp_err_t library_get_summary(int index, uint16_t *out_bpm,
                              uint32_t *out_duration_ms)
{
    if (index < 0 || index >= library_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_bpm) {
        *out_bpm = s_tracks[index].bpm;
    }
    if (out_duration_ms) {
        *out_duration_ms = s_tracks[index].duration_ms;
    }
    return ESP_OK;
}

library_track_t *library_get_ptr(int index)
{
    return index >= 0 && index < library_count() ? &s_tracks[index] : NULL;
}

uint32_t library_track_key(const library_track_t *track)
{
    return track ? track->track_id : 0;
}

esp_err_t library_load_anlz(library_track_t *track)
{
    if (!track) {
        return ESP_ERR_INVALID_ARG;
    }
    library_free_current_anlz();
    memset(&s_current_meta, 0, sizeof(s_current_meta));
    s_current_meta.bpm = track->bpm;
    s_current_meta.beat_count = 64;
    s_current_meta.beats = calloc(s_current_meta.beat_count, sizeof(anlz_beat_t));
    if (!s_current_meta.beats) {
        return ESP_ERR_NO_MEM;
    }
    uint32_t beat_ms = track->bpm ? (60000u / track->bpm) : 500u;
    for (uint16_t i = 0; i < s_current_meta.beat_count; i++) {
        s_current_meta.beats[i].beat_phase = (uint16_t)(i & 3u);
        s_current_meta.beats[i].bpm_x100 = (uint16_t)(track->bpm * 100u);
        s_current_meta.beats[i].time_ms = i * beat_ms;
    }
    memcpy(s_current_meta.waveform_low, track->waveform_low,
           sizeof(s_current_meta.waveform_low));
    s_current_meta.has_waveform_low = true;
    s_current_meta.cue_count = 2;
    s_current_meta.cues[0] = (anlz_cue_t){
        .type = ANLZ_CUE_SINGLE, .index = 0, .start_ms = beat_ms * 8u,
    };
    s_current_meta.cues[1] = (anlz_cue_t){
        .type = ANLZ_CUE_LOOP, .index = 1,
        .start_ms = beat_ms * 24u, .end_ms = beat_ms * 32u,
    };
    track->has_anlz = 1;
    return ESP_OK;
}

void library_last_anlz_load_stats(uint32_t *elapsed_ms, uint8_t *source,
                                  bool *cache_written)
{
    if (elapsed_ms) *elapsed_ms = 1;
    if (source) *source = 0;
    if (cache_written) *cache_written = false;
}

esp_err_t library_clone_current_anlz(anlz_metadata_t *out)
{
    if (!out || !s_current_meta.beats) {
        return ESP_ERR_NOT_FOUND;
    }
    return anlz_clone(&s_current_meta, out);
}

void library_free_current_anlz(void)
{
    anlz_free(&s_current_meta);
}

static int compare_title(const void *lhs, const void *rhs)
{
    const library_track_t *a = lhs;
    const library_track_t *b = rhs;
    return strcmp(a->title, b->title);
}

static int compare_artist(const void *lhs, const void *rhs)
{
    const library_track_t *a = lhs;
    const library_track_t *b = rhs;
    return strcmp(a->artist, b->artist);
}

static int compare_bpm(const void *lhs, const void *rhs)
{
    const library_track_t *a = lhs;
    const library_track_t *b = rhs;
    return (a->bpm > b->bpm) - (a->bpm < b->bpm);
}

void library_sort(int field_type, bool descending)
{
    int (*compare)(const void *, const void *) = compare_title;
    if (field_type == 0) compare = compare_artist;
    if (field_type == 2) compare = compare_bpm;
    qsort(s_tracks, FIXTURE_TRACK_COUNT, sizeof(s_tracks[0]), compare);
    if (descending) {
        for (int i = 0; i < FIXTURE_TRACK_COUNT / 2; i++) {
            library_track_t tmp = s_tracks[i];
            s_tracks[i] = s_tracks[FIXTURE_TRACK_COUNT - 1 - i];
            s_tracks[FIXTURE_TRACK_COUNT - 1 - i] = tmp;
        }
    }
    s_generation++;
}

void mock_library_load_track_to_deck(int track_index)
{
    if (track_index >= 0 && track_index < library_count()) {
        s_loaded_index = track_index;
    }
}

int mock_library_get_current_track_index(void)
{
    return s_loaded_index;
}
