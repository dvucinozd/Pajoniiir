/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flx4_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONTROLLER_EVENT_BUFFER_CAPACITY 64u

typedef struct {
    flx4_control_event_t events[CONTROLLER_EVENT_BUFFER_CAPACITY];
    size_t head;
    size_t count;
    uint32_t coalesced;
    uint32_t dropped;
} controller_event_buffer_t;

void controller_event_buffer_init(controller_event_buffer_t *buffer);
bool controller_event_buffer_push(controller_event_buffer_t *buffer,
                                  const flx4_control_event_t *event);
bool controller_event_buffer_pop(controller_event_buffer_t *buffer,
                                 flx4_control_event_t *event_out);
size_t controller_event_buffer_count(const controller_event_buffer_t *buffer);
bool controller_event_is_high_rate(const flx4_control_event_t *event);
bool controller_event_is_relative_jog(const flx4_control_event_t *event);
int16_t controller_event_accumulate_delta(int16_t current, int16_t delta);

#ifdef __cplusplus
}
#endif
