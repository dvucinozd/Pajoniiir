#pragma once
#include "dj_link_service.h"
#include "media_catalog.h"
typedef struct {
    uint8_t peer,slot;
    uint64_t source_epoch;
    dj_link_peer_track_t track;
} dj_link_remote_track_t;
typedef struct {
    void *ctx;
    bool (*current)(void *ctx);
    void (*progress)(void *ctx,uint32_t done,uint32_t total);
} dj_link_download_io_t;
/* Blocking worker operation. A single SD download reservation spans all
 * files; no deck is touched. Returned ANLZ ownership belongs to caller. */
esp_err_t dj_link_download_prepare(const dj_link_remote_track_t *ref,
    const media_persistent_id_t pins[2],const dj_link_download_io_t *io,
    media_catalog_track_t *item,media_loaded_track_t *loaded,anlz_metadata_t *meta);
