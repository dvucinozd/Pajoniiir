#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "deck_loaded_track_types.h"
#include "rekordbox_anlz.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

#define DECK_LOADED_TRACK_COUNT 2u

typedef struct {
    uint32_t media_generation;
    uint32_t track_key;
    uint32_t duration_ms;
    uint16_t bpm;
    const anlz_metadata_t *anlz;
} deck_loaded_track_payload_t;

typedef enum {
    DECK_LOADED_TRACK_OK = 0,
    DECK_LOADED_TRACK_INVALID,
    DECK_LOADED_TRACK_STALE,
    DECK_LOADED_TRACK_NO_MEMORY,
} deck_loaded_track_result_t;

typedef struct {
    deck_loaded_track_summary_t summary[DECK_LOADED_TRACK_COUNT];
    anlz_metadata_t meta[DECK_LOADED_TRACK_COUNT];
    bool meta_valid[DECK_LOADED_TRACK_COUNT];
    uint32_t next_generation;
    uint32_t media_floor;
#ifdef ESP_PLATFORM
    StaticSemaphore_t mutex_storage;
    SemaphoreHandle_t mutex;
#else
    bool writer_active;
    uint32_t reader_count;
#endif
} deck_loaded_track_store_t;

/*
 * Stores must have static storage duration or be zero-initialized before their
 * first reset. The store owns every published ANLZ clone; reset/clear releases
 * those allocations after all concurrent readers leave.
 */
void deck_loaded_track_store_reset(deck_loaded_track_store_t *store);

deck_loaded_track_result_t deck_loaded_track_store_publish(
    deck_loaded_track_store_t *store,
    uint8_t deck,
    const deck_loaded_track_payload_t *payload);

deck_loaded_track_result_t deck_loaded_track_store_clear(
    deck_loaded_track_store_t *store,
    uint8_t deck,
    uint32_t media_generation);

deck_loaded_track_result_t deck_loaded_track_store_clear_all(
    deck_loaded_track_store_t *store,
    uint32_t media_generation);

bool deck_loaded_track_store_get(
    const deck_loaded_track_store_t *store,
    uint8_t deck,
    deck_loaded_track_summary_t *out);

bool deck_loaded_track_store_clone(
    const deck_loaded_track_store_t *store,
    uint8_t deck,
    deck_loaded_track_summary_t *out_summary,
    anlz_metadata_t *out_anlz);
