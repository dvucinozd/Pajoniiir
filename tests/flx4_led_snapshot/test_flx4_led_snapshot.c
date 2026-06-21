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
    led_id_t led[32];
    uint8_t state[32];
    uint8_t deck[32];
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
    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;

    assert(log.count == 28);
    assert(log.led[0] == LED_CUE && log.deck[0] == 0 && log.state[0] == 0);
    assert(log.led[1] == LED_PLAY && log.deck[1] == 0 && log.state[1] == 0);
    assert(log.led[2] == LED_PFL && log.deck[2] == 0 && log.state[2] == 0);
    assert(log.led[3] == LED_SYNC && log.deck[3] == 0 && log.state[3] == 0);
    assert(log.led[4] == LED_PAD_MODE_HOT_CUE && log.deck[4] == 0 && log.state[4] == 1);
    assert(log.led[5] == LED_PAD_MODE_KEYBOARD && log.deck[5] == 0 && log.state[5] == 0);
    assert(log.led[12] == LED_LOOP_IN && log.deck[12] == 0 && log.state[12] == 0);
    assert(log.led[13] == LED_LOOP_OUT && log.deck[13] == 0 && log.state[13] == 0);
    assert(log.led[14] == LED_CUE && log.deck[14] == 1 && log.state[14] == 0);
    assert(log.led[15] == LED_PLAY && log.deck[15] == 1 && log.state[15] == 0);
    assert(log.led[16] == LED_PFL && log.deck[16] == 1 && log.state[16] == 0);
    assert(log.led[17] == LED_SYNC && log.deck[17] == 1 && log.state[17] == 0);
    assert(log.led[18] == LED_PAD_MODE_HOT_CUE && log.deck[18] == 1 && log.state[18] == 1);
}

static void test_normal_publish_suppresses_unchanged_values(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 28);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 28);
}

static void test_changed_play_sends_only_changed_play_led(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 28);

    input.play[0] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 29);
    assert(log.led[28] == LED_PLAY);
    assert(log.deck[28] == 0);
    assert(log.state[28] == 1);
}

static void test_changed_sync_sends_only_changed_sync_led(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 28);

    input.sync[0] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 29);
    assert(log.led[28] == LED_SYNC);
    assert(log.deck[28] == 0);
    assert(log.state[28] == 1);
}

static void test_changed_loop_state_sends_loop_in_and_out_for_deck(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 28);

    input.loop_active[1] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 30);
    assert(log.led[28] == LED_LOOP_IN);
    assert(log.deck[28] == 1);
    assert(log.state[28] == 1);
    assert(log.led[29] == LED_LOOP_OUT);
    assert(log.deck[29] == 1);
    assert(log.state[29] == 1);
}

static void test_changed_pad_mode_sends_old_off_and_new_on_for_deck(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 28);

    input.pad_mode[1] = CTRL_PAD_MODE_SAMPLER;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 30);
    assert(log.led[28] == LED_PAD_MODE_HOT_CUE);
    assert(log.deck[28] == 1);
    assert(log.state[28] == 0);
    assert(log.led[29] == LED_PAD_MODE_SAMPLER);
    assert(log.deck[29] == 1);
    assert(log.state[29] == 1);
}

static void test_failed_send_retries_on_next_normal_publish(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;
    input.play[0] = 1;
    flx4_led_publisher_init(&publisher);
    log.fail_on_call = 2;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_FAIL);
    assert(log.count == 28);
    assert(log.failed_led == LED_PLAY);
    assert(log.failed_deck == 0);

    log.fail_on_call = 0;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 29);
    assert(log.saw_retry);
}

static void test_forced_reconnect_snapshot_sends_all_values_including_off(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_BEAT_JUMP;
    input.pad_mode[1] = CTRL_PAD_MODE_KEY_SHIFT;
    input.cue[0] = 1;
    input.play[1] = 1;
    input.pfl[1] = 1;
    input.loop_active[1] = 1;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 28);

    memset(&log, 0, sizeof(log));
    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;
    input.sync[0] = 1;
    input.loop_active[1] = 0;
    input.cue[0] = 0;
    input.play[1] = 0;
    input.pfl[1] = 0;
    assert(flx4_led_publisher_publish(&publisher, &input, true,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 28);
    assert(log.state[0] == 0);
    assert(log.led[3] == LED_SYNC && log.state[3] == 1);
    assert(log.state[15] == 0);
    assert(log.state[16] == 0);
    assert(log.led[4] == LED_PAD_MODE_HOT_CUE && log.state[4] == 1);
    assert(log.led[18] == LED_PAD_MODE_HOT_CUE && log.state[18] == 1);
    assert(log.led[26] == LED_LOOP_IN && log.state[26] == 0);
    assert(log.led[27] == LED_LOOP_OUT && log.state[27] == 0);
}

int main(void)
{
    test_initial_forced_snapshot_sends_all_mvp_leds();
    test_normal_publish_suppresses_unchanged_values();
    test_changed_play_sends_only_changed_play_led();
    test_changed_sync_sends_only_changed_sync_led();
    test_changed_loop_state_sends_loop_in_and_out_for_deck();
    test_changed_pad_mode_sends_old_off_and_new_on_for_deck();
    test_failed_send_retries_on_next_normal_publish();
    test_forced_reconnect_snapshot_sends_all_values_including_off();
    puts("flx4_led_snapshot tests passed");
    return 0;
}
