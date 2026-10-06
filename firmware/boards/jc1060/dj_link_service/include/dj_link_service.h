#pragma once
#include "dj_link_discovery.h"
#include "dj_link_browse.h"
#include "esp_err.h"

/* After NVS/common P4 startup. Default-off worker owns all network I/O and model.
 * Only the JC1060 target links this component. No Web/USB/audio callbacks here. */
esp_err_t dj_link_service_init(void);
/* Engineering calibration: measured output latency, bound to this exact sink
 * and rate. Initially unmeasured; no hardware values are invented/persisted. */
bool dj_link_service_set_sink_latency(uint8_t sink,uint32_t sample_rate,
    uint32_t latency_us,bool measured);
/* Nonblocking copy; false if unavailable or a publication is underway. */
bool dj_link_service_snapshot(dj_link_discovery_t *out);
/* Small nonblocking status for Settings, including observer/error reasons. */
void dj_link_service_format_status(char *out, size_t cap);
/* Enqueue from LVGL; zero means queue unavailable/full. No socket work here. */
uint64_t dj_link_service_browse(uint8_t peer, uint64_t epoch, uint8_t slot,
    dj_link_db_menu_t menu, uint32_t menu_id);
void dj_link_service_cancel_browse(void);
/* Nonblocking owned page copy. A busy publication returns false. */
bool dj_link_service_page(uint64_t request_id, uint32_t first, dj_link_browse_status_t *status,
    dj_link_peer_track_t rows[DJ_LINK_BROWSE_PAGE_ROWS]);
bool dj_link_service_take_load(dj_link_incoming_load_t *out);
bool dj_link_service_source(uint8_t peer,uint64_t epoch,dj_link_peer_t *out);
/* Worker only. Single dbserver owner; request 0 obtains a UTF-8 path, other
 * requests obtain bounded analysis/artwork blobs. Cancellation withdraws the
 * borrowed destination under the owner lock before returning. */
esp_err_t dj_link_service_read_asset(uint8_t peer,uint64_t epoch,uint8_t slot,
    uint32_t id,uint16_t request,void *dst,size_t cap,size_t *len,
    bool (*current)(void *),void *ctx);
