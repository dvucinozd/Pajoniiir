#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define S3_DEBUG_AP_SSID "PajoNiiiR-S3-DEBUG"
#define S3_DEBUG_AP_IP "192.168.4.1"
#define S3_DEBUG_LOG_RING_LINES 16
#define S3_DEBUG_LOG_LINE_MAX 256

typedef enum {
    S3_DEBUG_AP_STATUS_OFF = 0,
    S3_DEBUG_AP_STATUS_STARTING = 1,
    S3_DEBUG_AP_STATUS_ON = 2,
    S3_DEBUG_AP_STATUS_ERROR = 3,
} s3_debug_ap_status_t;

typedef void (*s3_debug_ap_status_cb_t)(s3_debug_ap_status_t status);

typedef struct {
    char lines[S3_DEBUG_LOG_RING_LINES][S3_DEBUG_LOG_LINE_MAX];
    uint32_t next_seq;
    uint8_t next_index;
    uint8_t count;
} s3_debug_log_ring_t;

void s3_debug_log_ring_init(s3_debug_log_ring_t *ring);
void s3_debug_log_ring_append(s3_debug_log_ring_t *ring, const char *text);
size_t s3_debug_log_ring_snapshot(const s3_debug_log_ring_t *ring,
                                  char *out,
                                  size_t out_size,
                                  uint32_t after_seq);

esp_err_t s3_debug_ap_init(void);
esp_err_t s3_debug_ap_set_status_callback(s3_debug_ap_status_cb_t cb);
esp_err_t s3_debug_ap_request(bool enable);
s3_debug_ap_status_t s3_debug_ap_status(void);
