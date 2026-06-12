#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "deck_core.h"
#include "rekordbox_anlz.h"
#include "ui_beat_indicator.h"

#ifndef WIN32
#include "audio_engine.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_OVERVIEW_WAVEFORM_SOURCE_NONE = 0,
    UI_OVERVIEW_WAVEFORM_SOURCE_LOADED_MEDIA,
    UI_OVERVIEW_WAVEFORM_SOURCE_METADATA,
} ui_overview_waveform_source_kind_t;

typedef struct {
    ui_overview_waveform_source_kind_t kind;
    const uint8_t *waveform_low;
    bool has_waveform;
} ui_overview_waveform_source_info_t;

typedef struct {
    bool valid;
    char title[96];
    char artist[64];
    uint16_t bpm;
    uint32_t duration_ms;
} ui_deck_track_info_t;

typedef struct {
    uint64_t now_us;
    uint32_t now_ms;
    int active_tab;

    uint8_t active_deck;
    deck_state_t deck_state[DECK_CORE_DECK_COUNT];
    deck_state_t active_state;

    uint32_t deck_duration_ms[DECK_CORE_DECK_COUNT];
    uint16_t deck_bpm[DECK_CORE_DECK_COUNT];
    uint32_t deck_speed_permille[DECK_CORE_DECK_COUNT];

    const anlz_metadata_t *deck_meta[DECK_CORE_DECK_COUNT];
    const ui_deck_track_info_t *deck_info[DECK_CORE_DECK_COUNT];
    ui_overview_waveform_source_info_t overview_wave_source[DECK_CORE_DECK_COUNT];

    uint32_t active_duration_ms;
    uint16_t active_base_bpm;
    const anlz_metadata_t *active_meta;
    ui_beat_indicator_state_t active_beat_state;
    bool active_beat_state_valid;

    bool overview_slow_update;

#ifndef WIN32
    bool ae_loading;
    uint8_t ae_load_pct;
    audio_engine_mixer_snapshot_t mixer_snapshot;
#else
    bool ae_loading;
    uint8_t ae_load_pct;
#endif
} ui_frame_context_t;

#ifdef __cplusplus
}
#endif
