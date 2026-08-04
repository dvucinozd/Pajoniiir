/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_event_buffer.h"

#include <limits.h>
#include <string.h>

#include "control_link.h"

void controller_event_buffer_init(controller_event_buffer_t *buffer)
{
    if (buffer) {
        memset(buffer, 0, sizeof(*buffer));
    }
}

bool controller_event_is_high_rate(const flx4_control_event_t *event)
{
    if (!event) {
        return false;
    }
    if (event->type == CTRL_TYPE_ENCODER &&
        event->id != CTRL_ID_BROWSE_DELTA &&
        event->id != CTRL_ID_BROWSE_SHIFT_DELTA) {
        return true;
    }
    if (event->type != CTRL_TYPE_PITCH) {
        return false;
    }
    switch (event->id) {
    case CTRL_ID_DECK1_TEMPO:
    case CTRL_ID_DECK2_TEMPO:
    case CTRL_ID_CH1_VOLUME:
    case CTRL_ID_CH2_VOLUME:
    case CTRL_ID_CROSSFADER:
    case CTRL_ID_CH1_TRIM:
    case CTRL_ID_CH2_TRIM:
    case CTRL_ID_CH1_EQ_HIGH:
    case CTRL_ID_CH2_EQ_HIGH:
    case CTRL_ID_CH1_EQ_MID:
    case CTRL_ID_CH2_EQ_MID:
    case CTRL_ID_CH1_EQ_LOW:
    case CTRL_ID_CH2_EQ_LOW:
    case CTRL_ID_CH1_FILTER:
    case CTRL_ID_CH2_FILTER:
    case CTRL_ID_MASTER_VOLUME:
    case CTRL_ID_HEADPHONE_MIX:
    case CTRL_ID_HEADPHONE_LEVEL:
    case CTRL_ID_BEAT_FX_DEPTH:
        return true;
    default:
        return false;
    }
}

bool controller_event_is_relative_jog(const flx4_control_event_t *event)
{
    if (!event || event->type != CTRL_TYPE_ENCODER) {
        return false;
    }
    switch (event->id) {
    case CTRL_ID_DECK1_JOG_SCRATCH:
    case CTRL_ID_DECK1_JOG_BEND:
    case CTRL_ID_DECK1_JOG_SEARCH:
    case CTRL_ID_DECK2_JOG_SCRATCH:
    case CTRL_ID_DECK2_JOG_BEND:
    case CTRL_ID_DECK2_JOG_SEARCH:
        return true;
    default:
        return false;
    }
}

int16_t controller_event_accumulate_delta(int16_t current, int16_t delta)
{
    const int32_t sum = (int32_t)current + (int32_t)delta;
    if (sum > INT16_MAX) {
        return INT16_MAX;
    }
    if (sum < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)sum;
}

bool controller_event_buffer_push(controller_event_buffer_t *buffer,
                                  const flx4_control_event_t *event)
{
    if (!buffer || !event) {
        return false;
    }

    if (buffer->count < CONTROLLER_EVENT_BUFFER_CAPACITY) {
        const size_t tail =
            (buffer->head + buffer->count) % CONTROLLER_EVENT_BUFFER_CAPACITY;
        buffer->events[tail] = *event;
        buffer->count++;
        return true;
    }

    if (controller_event_is_high_rate(event)) {
        for (size_t reverse = buffer->count; reverse > 0u; --reverse) {
            const size_t logical = reverse - 1u;
            const size_t index =
                (buffer->head + logical) % CONTROLLER_EVENT_BUFFER_CAPACITY;
            flx4_control_event_t *queued = &buffer->events[index];
            if (queued->type != event->type || queued->id != event->id) {
                continue;
            }
            if (controller_event_is_relative_jog(event)) {
                queued->value = controller_event_accumulate_delta(
                    queued->value, event->value);
            } else {
                *queued = *event;
            }
            buffer->coalesced++;
            return true;
        }
    }

    buffer->dropped++;
    return false;
}

bool controller_event_buffer_pop(controller_event_buffer_t *buffer,
                                 flx4_control_event_t *event_out)
{
    if (!buffer || !event_out || buffer->count == 0u) {
        return false;
    }
    *event_out = buffer->events[buffer->head];
    buffer->head = (buffer->head + 1u) % CONTROLLER_EVENT_BUFFER_CAPACITY;
    buffer->count--;
    return true;
}

size_t controller_event_buffer_count(const controller_event_buffer_t *buffer)
{
    return buffer ? buffer->count : 0u;
}
