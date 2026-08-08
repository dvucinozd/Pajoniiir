/* SPDX-License-Identifier: Apache-2.0 */
#include "trace_adapter.h"

#define flx4_map_init s3_flx4_map_init_impl
#define flx4_map_message s3_flx4_map_message_impl
#define flx4_map_emit_snapshot s3_flx4_map_emit_snapshot_impl
#include "../../firmware/control-board-s3/components/flx4_midi_host/flx4_map.c"
#undef flx4_map_emit_snapshot
#undef flx4_map_message
#undef flx4_map_init

static flx4_map_state_t s_state;

void s3_trace_reset(void)
{
    s3_flx4_map_init_impl(&s_state);
}

bool s3_trace_feed(const trace_midi_t *midi, trace_event_t *event)
{
    if (!midi || !event) {
        return false;
    }
    const flx4_midi_message_t message = {
        .cable = 0u,
        .cin = (uint8_t)((midi->status >> 4) & 0x0Fu),
        .len = 3u,
        .status = midi->status,
        .data1 = midi->data1,
        .data2 = midi->data2,
    };
    flx4_control_event_t mapped = {0};
    const bool emitted = s3_flx4_map_message_impl(&s_state, &message, &mapped);
    if (emitted) {
        *event = (trace_event_t) {
            .type = mapped.type,
            .id = mapped.id,
            .value = mapped.value,
        };
    }
    return emitted;
}

typedef struct {
    trace_event_t *events;
    size_t capacity;
    size_t count;
} snapshot_ctx_t;

static bool collect_snapshot(uint8_t type, uint8_t id, int16_t value, void *ctx_ptr)
{
    snapshot_ctx_t *ctx = (snapshot_ctx_t *)ctx_ptr;
    if (ctx->count >= ctx->capacity) {
        return false;
    }
    ctx->events[ctx->count++] = (trace_event_t) {
        .type = type, .id = id, .value = value,
    };
    return true;
}

size_t s3_trace_snapshot(trace_event_t *events, size_t capacity)
{
    snapshot_ctx_t ctx = { .events = events, .capacity = capacity };
    (void)s3_flx4_map_emit_snapshot_impl(&s_state, collect_snapshot, &ctx);
    return ctx.count;
}
