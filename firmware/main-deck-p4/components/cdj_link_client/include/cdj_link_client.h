#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cdj_link_protocol.h"
#include "esp_err.h"

typedef struct {
    bool valid;
    char peer_id[CDJ_LINK_PEER_ID_LEN];
    char name[CDJ_LINK_NAME_LEN];
    char host[16];
    uint16_t port;
    uint32_t track_count;
    int64_t last_seen_us;
} cdj_link_peer_t;

esp_err_t cdj_link_client_start(void);
bool cdj_link_client_get_peer(cdj_link_peer_t *out_peer);
esp_err_t cdj_link_client_fetch_library(uint8_t **out_blob, size_t *out_len);
esp_err_t cdj_link_client_fetch_manifest(uint32_t track_key, cdj_link_track_manifest_t *out_manifest);
esp_err_t cdj_link_client_download_asset(uint32_t track_key,
                                         const char *asset_name,
                                         const char *dest_path,
                                         uint32_t expected_size,
                                         uint32_t *out_bytes);
