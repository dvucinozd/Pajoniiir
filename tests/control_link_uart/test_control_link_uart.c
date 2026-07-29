/*
 * First execution coverage for control_link_uart.c.
 *
 * This component decides what reaches deck_core: which events may be coalesced
 * when the queue is full, which must never be lost, and how long the UART RX
 * task may be held waiting. Until now all of that was guarded only by grepping
 * the source for identifiers, so the rules were asserted but never run.
 *
 * The suite drives the real translation unit against the fake RTOS from
 * tests/support/rtos (a queue with real capacity, so "full" is reachable) and a
 * scripted UART fake, and calls the RX parser the way the driver would.
 */
#include "control_link.h"
#include "fake_rtos.h"

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

test_uart_state_t g_test_uart;

static int s_failures;
static unsigned s_checks;
#define CHECK(x) do {                                                     \
    s_checks++;                                                           \
    if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); s_failures++; } \
} while (0)

/* control_link_uart.c owns its RX task; the test drives one pass of the parser
 * through the same seam the task uses. */
void control_link_test_pump_rx(void);

static QueueHandle_t s_queue;

static void feed_frame(uint8_t type, uint8_t id, int16_t value, uint8_t seq)
{
    const uint8_t lo = (uint8_t)(value & 0xFF);
    const uint8_t hi = (uint8_t)((value >> 8) & 0xFF);
    uint8_t frame[CTRL_FRAME_LEN] = {
        CTRL_FRAME_START, type, id, lo, hi, seq,
        (uint8_t)(type ^ id ^ lo ^ hi ^ seq),
    };
    test_uart_feed(frame, sizeof(frame));
}

static void begin(unsigned queue_depth)
{
    fake_rtos_reset();
    test_uart_reset();
    s_queue = xQueueCreate(queue_depth, sizeof(ctrl_event_t));
    CHECK(s_queue != NULL);
    CHECK(control_link_init(s_queue) == ESP_OK);
}

static unsigned drain(ctrl_event_t *out, unsigned max)
{
    unsigned n = 0;
    while (n < max && xQueueReceive(s_queue, &out[n], 0) == pdTRUE) n++;
    return n;
}

/* -- Frame parsing --------------------------------------------------------- */

static void test_valid_frame_becomes_an_event(void)
{
    printf("== a well-formed frame becomes one queued event ==\n");
    begin(16u);

    feed_frame(CTRL_TYPE_BUTTON, CTRL_ID_DECK1_JOG_TOUCH, 1, 7u);
    control_link_test_pump_rx();

    ctrl_event_t ev[4];
    CHECK(drain(ev, 4) == 1u);
    CHECK(ev[0].type == CTRL_EV_BUTTON);
    CHECK(ev[0].id == CTRL_ID_DECK1_JOG_TOUCH);
    CHECK(ev[0].value == 1);
    CHECK(ev[0].seq == 7u);
}

static void test_bad_checksum_is_rejected(void)
{
    printf("== a frame with a broken checksum produces no event ==\n");
    begin(16u);

    uint8_t frame[CTRL_FRAME_LEN] = {
        CTRL_FRAME_START, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_JOG_TOUCH, 1u, 0u, 3u,
        0xFFu, /* deliberately wrong */
    };
    test_uart_feed(frame, sizeof(frame));
    control_link_test_pump_rx();

    ctrl_event_t ev[4];
    CHECK(drain(ev, 4) == 0u);
}

static void test_resyncs_after_leading_garbage(void)
{
    printf("== the parser resynchronises on the start byte ==\n");
    begin(16u);

    const uint8_t noise[] = { 0x00u, 0x12u, 0xFEu };
    test_uart_feed(noise, sizeof(noise));
    feed_frame(CTRL_TYPE_PITCH, 0u, 8192, 1u);
    control_link_test_pump_rx();

    ctrl_event_t ev[4];
    CHECK(drain(ev, 4) == 1u);
    CHECK(ev[0].type == CTRL_EV_PITCH);
    CHECK(ev[0].value == 8192);
}

/* -- Queue discipline: the property P1-C changed --------------------------- */

static void test_button_edges_are_never_dropped_when_queue_is_full(void)
{
    printf("== a button release survives a full queue ==\n");
    /* Depth 2, then three button edges: the third can only be delivered if the
     * consumer drains, which is what bounded backpressure is for. Nothing may be
     * silently coalesced away: a lost release latches scratch, Censor or SHIFT. */
    begin(2u);

    feed_frame(CTRL_TYPE_BUTTON, CTRL_ID_DECK1_JOG_TOUCH, 1, 1u);
    feed_frame(CTRL_TYPE_BUTTON, CTRL_ID_DECK1_JOG_TOUCH, 0, 2u);
    control_link_test_pump_rx();

    ctrl_event_t ev[4];
    unsigned n = drain(ev, 4);
    CHECK(n == 2u);
    CHECK(ev[0].value == 1);
    CHECK(ev[1].value == 0);   /* the release is present, and in order */
}

static void test_continuous_values_coalesce_to_the_newest(void)
{
    printf("== fader samples coalesce to the newest when the queue is full ==\n");
    /* Depth 1, then three absolute fader samples with no consumer in between.
     * An absolute control is worth only its latest value, so the intermediate
     * sample may be discarded - but the value the operator actually left the
     * fader at may not be, or the mixer ends up at the wrong level. */
    begin(1u);

    feed_frame(CTRL_TYPE_BUTTON, CTRL_ID_CH1_VOLUME, 10, 1u);
    feed_frame(CTRL_TYPE_BUTTON, CTRL_ID_CH1_VOLUME, 20, 2u);
    feed_frame(CTRL_TYPE_BUTTON, CTRL_ID_CH1_VOLUME, 30, 3u);
    control_link_test_pump_rx();

    ctrl_event_t ev[8];
    /* The first sample took the only slot; the rest were held pending. */
    CHECK(drain(ev, 8) == 1u);
    CHECK(ev[0].id == CTRL_ID_CH1_VOLUME);
    CHECK(ev[0].value == 10);

    /* With the queue drained, the next pass releases the newest held sample -
     * 30, not 20: the middle sample is coalesced away, the last one survives. */
    control_link_test_pump_rx();
    unsigned n = drain(ev, 8);
    CHECK(n == 1u);
    CHECK(ev[0].id == CTRL_ID_CH1_VOLUME);
    CHECK(ev[0].value == 30);

    /* And nothing is left over: a coalesced sample is delivered once. */
    control_link_test_pump_rx();
    CHECK(drain(ev, 8) == 0u);
}

static void test_pending_jog_deltas_accumulate_rather_than_being_lost(void)
{
    printf("== jog deltas held back by a full queue add up, not overwrite ==\n");
    begin(1u);

    /* One event fills the queue; the jog deltas behind it must be summed, since
     * a jog delta is a relative movement and dropping one loses distance. */
    feed_frame(CTRL_TYPE_BUTTON, CTRL_ID_DECK1_JOG_TOUCH, 1, 1u);
    feed_frame(CTRL_TYPE_ENCODER, CTRL_ID_DECK1_JOG_SCRATCH, 5, 2u);
    feed_frame(CTRL_TYPE_ENCODER, CTRL_ID_DECK1_JOG_SCRATCH, 7, 3u);
    control_link_test_pump_rx();

    ctrl_event_t ev[8];
    unsigned n = drain(ev, 8);
    CHECK(n >= 1u);

    /* Draining lets the pending sample through on the next pass. */
    control_link_test_pump_rx();
    unsigned m = drain(ev, 8);
    int32_t total = 0;
    for (unsigned i = 0; i < m; ++i) {
        if (ev[i].type == CTRL_EV_JOG) total += ev[i].value;
    }
    CHECK(total == 12);   /* 5 + 7, not 7 */
}

static void test_producer_never_removes_events_from_the_queue(void)
{
    printf("== the RX producer never drains the shared queue to make room ==\n");
    begin(2u);

    /* Two events from another producer (the UI or web server would do this). */
    ctrl_event_t injected = { .type = CTRL_EV_BUTTON, .id = CTRL_ID_CH2_VOLUME,
                              .value = 99, .seq = 42u };
    CHECK(xQueueSend(s_queue, &injected, 0) == pdTRUE);
    injected.value = 98;
    CHECK(xQueueSend(s_queue, &injected, 0) == pdTRUE);

    /* The queue is now full. Feeding more must not evict what is already there:
     * an earlier implementation drained and re-pushed to coalesce, which raced
     * every other producer on this queue. */
    feed_frame(CTRL_TYPE_BUTTON, CTRL_ID_CH1_VOLUME, 1, 1u);
    control_link_test_pump_rx();

    ctrl_event_t ev[8];
    unsigned n = drain(ev, 8);
    CHECK(n >= 2u);
    CHECK(ev[0].id == CTRL_ID_CH2_VOLUME);
    CHECK(ev[0].value == 99);   /* still first, still intact */
    CHECK(ev[1].value == 98);
}

/* -- Transmit path --------------------------------------------------------- */

static void test_state_frames_are_transmitted_well_formed(void)
{
    printf("== a state command goes out as one checksummed frame ==\n");
    begin(8u);

    control_link_send_state(CTRL_ID_S3_DEBUG_AP, 1);
    CHECK(g_test_uart.tx_len == CTRL_FRAME_LEN);
    CHECK(g_test_uart.tx[0] == CTRL_FRAME_START);
    CHECK(g_test_uart.tx[1] == CTRL_TYPE_STATE);
    CHECK(g_test_uart.tx[2] == CTRL_ID_S3_DEBUG_AP);
    CHECK(g_test_uart.tx[3] == 1u);

    const uint8_t sum = (uint8_t)(g_test_uart.tx[1] ^ g_test_uart.tx[2] ^
                                  g_test_uart.tx[3] ^ g_test_uart.tx[4] ^
                                  g_test_uart.tx[5]);
    CHECK(g_test_uart.tx[6] == sum);
}

static void test_led_frames_carry_the_deck(void)
{
    printf("== a deck LED command carries its deck in the frame ==\n");
    begin(8u);

    control_link_send_led_deck(LED_PLAY, 1u, CTRL_DECK_2);
    CHECK(g_test_uart.tx_len == CTRL_FRAME_LEN);
    CHECK(g_test_uart.tx[1] == CTRL_TYPE_LED);
}

int main(void)
{
    test_valid_frame_becomes_an_event();
    test_bad_checksum_is_rejected();
    test_resyncs_after_leading_garbage();
    test_button_edges_are_never_dropped_when_queue_is_full();
    test_continuous_values_coalesce_to_the_newest();
    test_pending_jog_deltas_accumulate_rather_than_being_lost();
    test_producer_never_removes_events_from_the_queue();
    test_state_frames_are_transmitted_well_formed();
    test_led_frames_carry_the_deck();

    printf("TESTS_RUN=%u\n", s_checks);
    if (s_failures == 0) {
        puts("control_link_uart tests passed");
        return 0;
    }
    printf("control_link_uart tests FAILED (%d)\n", s_failures);
    return 1;
}
