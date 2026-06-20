#include "flx4_led_snapshot.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int count;
    int fail_on_call;
    bool saw_retry;
    led_id_t failed_led;
    uint8_t failed_deck;
    led_id_t led[16];
    uint8_t state[16];
    uint8_t deck[16];
} send_log_t;

static esp_err_t capture_send(led_id_t led, uint8_t state, uint8_t deck, void *ctx)
{
    send_log_t *log = ctx;
    assert(log->count < (int)(sizeof(log->led) / sizeof(log->led[0])));
    log->led[log->count] = led;
    log->state[log->count] = state;
    log->deck[log->count] = deck;
    log->count++;

    if (log->fail_on_call == log->count) {
        log->failed_led = led;
        log->failed_deck = deck;
        return ESP_FAIL;
    }
    if (log->fail_on_call == 0 &&
        led == log->failed_led &&
        deck == log->failed_deck) {
        log->saw_retry = true;
    }
    return ESP_OK;
}

static void test_initial_forced_snapshot_sends_all_mvp_leds(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, true,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 6);
    assert(log.led[0] == LED_CUE && log.deck[0] == 0 && log.state[0] == 0);
    assert(log.led[1] == LED_PLAY && log.deck[1] == 0 && log.state[1] == 0);
    assert(log.led[2] == LED_PFL && log.deck[2] == 0 && log.state[2] == 0);
    assert(log.led[3] == LED_CUE && log.deck[3] == 1 && log.state[3] == 0);
    assert(log.led[4] == LED_PLAY && log.deck[4] == 1 && log.state[4] == 0);
    assert(log.led[5] == LED_PFL && log.deck[5] == 1 && log.state[5] == 0);
}

static void test_normal_publish_suppresses_unchanged_values(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 6);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 6);
}

static void test_changed_play_sends_only_changed_play_led(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 6);

    input.play[0] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 7);
    assert(log.led[6] == LED_PLAY);
    assert(log.deck[6] == 0);
    assert(log.state[6] == 1);
}

static void test_failed_send_retries_on_next_normal_publish(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.play[0] = 1;
    flx4_led_publisher_init(&publisher);
    log.fail_on_call = 2;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_FAIL);
    assert(log.count == 6);
    assert(log.failed_led == LED_PLAY);
    assert(log.failed_deck == 0);

    log.fail_on_call = 0;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 7);
    assert(log.saw_retry);
}

static void test_forced_reconnect_snapshot_sends_all_values_including_off(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.cue[0] = 1;
    input.play[1] = 1;
    input.pfl[1] = 1;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 6);

    memset(&log, 0, sizeof(log));
    input.cue[0] = 0;
    input.play[1] = 0;
    input.pfl[1] = 0;
    assert(flx4_led_publisher_publish(&publisher, &input, true,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 6);
    assert(log.state[0] == 0);
    assert(log.state[4] == 0);
    assert(log.state[5] == 0);
}

int main(void)
{
    test_initial_forced_snapshot_sends_all_mvp_leds();
    test_normal_publish_suppresses_unchanged_values();
    test_changed_play_sends_only_changed_play_led();
    test_failed_send_retries_on_next_normal_publish();
    test_forced_reconnect_snapshot_sends_all_values_including_off();
    puts("flx4_led_snapshot tests passed");
    return 0;
}
