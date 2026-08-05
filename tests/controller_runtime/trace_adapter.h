/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
} trace_midi_t;

typedef struct {
    uint8_t type;
    uint8_t id;
    int16_t value;
} trace_event_t;

void p4_trace_reset(void);
bool p4_trace_feed(const trace_midi_t *midi, trace_event_t *event);
size_t p4_trace_snapshot(trace_event_t *events, size_t capacity);

void s3_trace_reset(void);
bool s3_trace_feed(const trace_midi_t *midi, trace_event_t *event);
size_t s3_trace_snapshot(trace_event_t *events, size_t capacity);
