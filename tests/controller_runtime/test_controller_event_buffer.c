#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "controller_event_buffer.h"
#include "control_link.h"

static unsigned s_checks;
#define CHECK(expr) do { \
    s_checks++; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static flx4_control_event_t event(uint8_t type, uint8_t id, int16_t value)
{
    return (flx4_control_event_t) {
        .type = type,
        .id = id,
        .value = value,
    };
}

static void fill_after_first(controller_event_buffer_t *buffer)
{
    for (size_t i = 1u; i < CONTROLLER_EVENT_BUFFER_CAPACITY; ++i) {
        const flx4_control_event_t command =
            event(CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PLAY, (int16_t)(i & 1u));
        CHECK(controller_event_buffer_push(buffer, &command));
    }
    CHECK(controller_event_buffer_count(buffer) ==
          CONTROLLER_EVENT_BUFFER_CAPACITY);
}

int main(void)
{
    controller_event_buffer_t buffer;
    controller_event_buffer_init(&buffer);

    flx4_control_event_t first =
        event(CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PLAY, 1);
    flx4_control_event_t second =
        event(CTRL_TYPE_BUTTON, CTRL_ID_DECK1_CUE, 1);
    CHECK(controller_event_buffer_push(&buffer, &first));
    CHECK(controller_event_buffer_push(&buffer, &second));
    flx4_control_event_t out;
    CHECK(controller_event_buffer_pop(&buffer, &out));
    CHECK(out.id == CTRL_ID_DECK1_PLAY);
    CHECK(controller_event_buffer_pop(&buffer, &out));
    CHECK(out.id == CTRL_ID_DECK1_CUE);

    controller_event_buffer_init(&buffer);
    first = event(CTRL_TYPE_PITCH, CTRL_ID_CH1_VOLUME, 100);
    CHECK(controller_event_buffer_push(&buffer, &first));
    fill_after_first(&buffer);
    const flx4_control_event_t newest =
        event(CTRL_TYPE_PITCH, CTRL_ID_CH1_VOLUME, 12000);
    CHECK(controller_event_buffer_push(&buffer, &newest));
    CHECK(buffer.coalesced == 1u);
    CHECK(buffer.dropped == 0u);
    CHECK(controller_event_buffer_pop(&buffer, &out));
    CHECK(out.id == CTRL_ID_CH1_VOLUME);
    CHECK(out.value == 12000);

    controller_event_buffer_init(&buffer);
    first = event(CTRL_TYPE_ENCODER, CTRL_ID_DECK1_JOG_BEND, 30);
    CHECK(controller_event_buffer_push(&buffer, &first));
    fill_after_first(&buffer);
    const flx4_control_event_t delta =
        event(CTRL_TYPE_ENCODER, CTRL_ID_DECK1_JOG_BEND, 20);
    CHECK(controller_event_buffer_push(&buffer, &delta));
    CHECK(controller_event_buffer_pop(&buffer, &out));
    CHECK(out.value == 50);

    controller_event_buffer_init(&buffer);
    first = event(CTRL_TYPE_ENCODER, CTRL_ID_DECK1_JOG_BEND,
                  (int16_t)(INT16_MAX - 2));
    CHECK(controller_event_buffer_push(&buffer, &first));
    fill_after_first(&buffer);
    const flx4_control_event_t positive_overflow =
        event(CTRL_TYPE_ENCODER, CTRL_ID_DECK1_JOG_BEND, 50);
    CHECK(controller_event_buffer_push(&buffer, &positive_overflow));
    CHECK(controller_event_buffer_pop(&buffer, &out));
    CHECK(out.value == INT16_MAX);
    CHECK(controller_event_accumulate_delta((int16_t)(INT16_MIN + 2), -50) ==
          INT16_MIN);

    controller_event_buffer_init(&buffer);
    first = event(CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PLAY, 1);
    CHECK(controller_event_buffer_push(&buffer, &first));
    fill_after_first(&buffer);
    const flx4_control_event_t discrete =
        event(CTRL_TYPE_BUTTON, CTRL_ID_DECK1_CUE, 1);
    CHECK(!controller_event_buffer_push(&buffer, &discrete));
    CHECK(buffer.dropped == 1u);

    const flx4_control_event_t browse =
        event(CTRL_TYPE_ENCODER, CTRL_ID_BROWSE_DELTA, 1);
    CHECK(!controller_event_is_high_rate(&browse));
    CHECK(controller_event_is_high_rate(&newest));
    CHECK(controller_event_is_relative_jog(&delta));

    printf("PASS bounded P4 controller event buffer\n");
    printf("CHECKS=%u\n", s_checks);
    return 0;
}
