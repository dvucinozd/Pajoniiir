#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "control_link.h"
#include "esp_err.h"

typedef struct {
    uint8_t cue[2];
    uint8_t play[2];
    uint8_t pfl[2];
    uint8_t sync[2];
    uint8_t pad_mode[2];
    uint8_t loop_in_marker[2];
    uint8_t loop_active[2];
    uint32_t loop_start_ms[2];
    uint32_t loop_end_ms[2];
    uint8_t pad_fx_active[2];
    uint8_t pad_fx_active_mode[2];
    uint8_t pad_fx_active_pad[2];
    uint8_t smart_cfx;
    uint8_t smart_fader;
    uint8_t beat_fx_on;
} flx4_led_snapshot_input_t;

typedef esp_err_t (*flx4_led_send_fn_t)(led_id_t led,
                                       uint8_t state,
                                       uint8_t deck,
                                       void *ctx);

typedef struct {
    uint8_t last[2][41];
    bool valid[2][41];
} flx4_led_publisher_t;

void flx4_led_publisher_init(flx4_led_publisher_t *publisher);

esp_err_t flx4_led_publisher_publish(
    flx4_led_publisher_t *publisher,
    const flx4_led_snapshot_input_t *input,
    bool force,
    flx4_led_send_fn_t send,
    void *ctx);
