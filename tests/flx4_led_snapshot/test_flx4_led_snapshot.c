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
    led_id_t led[64];
    uint8_t state[64];
    uint8_t deck[64];
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

    assert(log.count == 46);
    assert(log.led[0] == LED_CUE && log.deck[0] == 0 && log.state[0] == 0);
    assert(log.led[1] == LED_PLAY && log.deck[1] == 0 && log.state[1] == 0);
    assert(log.led[2] == LED_PFL && log.deck[2] == 0 && log.state[2] == 0);
    assert(log.led[3] == LED_SYNC && log.deck[3] == 0 && log.state[3] == 0);
    assert(log.led[4] == LED_PAD_MODE_HOT_CUE && log.deck[4] == 0 && log.state[4] == 1);
    assert(log.led[5] == LED_PAD_MODE_KEYBOARD && log.deck[5] == 0 && log.state[5] == 0);
    assert(log.led[12] == LED_LOOP_IN && log.deck[12] == 0 && log.state[12] == 0);
    assert(log.led[13] == LED_LOOP_OUT && log.deck[13] == 0 && log.state[13] == 0);
    assert(log.led[22] == LED_SMART_CFX && log.deck[22] == 0 && log.state[22] == 0);
    assert(log.led[23] == LED_SMART_FADER && log.deck[23] == 0 && log.state[23] == 0);
    assert(log.led[24] == LED_CUE && log.deck[24] == 1 && log.state[24] == 0);
    assert(log.led[25] == LED_PLAY && log.deck[25] == 1 && log.state[25] == 0);
    assert(log.led[26] == LED_PFL && log.deck[26] == 1 && log.state[26] == 0);
    assert(log.led[27] == LED_SYNC && log.deck[27] == 1 && log.state[27] == 0);
    assert(log.led[28] == LED_PAD_MODE_HOT_CUE && log.deck[28] == 1 && log.state[28] == 1);
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
    assert(log.count == 46);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 46);
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
    assert(log.count == 46);

    input.play[0] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 47);
    assert(log.led[46] == LED_PLAY);
    assert(log.deck[46] == 0);
    assert(log.state[46] == 1);
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
    assert(log.count == 46);

    input.sync[0] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 47);
    assert(log.led[46] == LED_SYNC);
    assert(log.deck[46] == 0);
    assert(log.state[46] == 1);
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
    assert(log.count == 46);

    input.loop_active[1] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 48);
    assert(log.led[46] == LED_LOOP_IN);
    assert(log.deck[46] == 1);
    assert(log.state[46] == 1);
    assert(log.led[47] == LED_LOOP_OUT);
    assert(log.deck[47] == 1);
    assert(log.state[47] == 1);
}

static void test_loop_in_marker_lights_loop_in_without_loop_out(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 46);

    input.loop_in_marker[0] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 47);
    assert(log.led[46] == LED_LOOP_IN);
    assert(log.deck[46] == 0);
    assert(log.state[46] == 1);
}

static void test_beat_loop_mode_lights_matching_loop_length_pad(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_BEAT_LOOP;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;
    input.loop_active[0] = 1;
    input.loop_start_ms[0] = 10000;
    input.loop_end_ms[0] = 10500;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, true,
                                      capture_send, &log) == ESP_OK);

    bool saw_pad_6_on = false;
    bool saw_other_pad_on = false;
    for (int i = 0; i < log.count; i++) {
        if (log.deck[i] != CTRL_DECK_1 ||
            log.led[i] < LED_BEAT_LOOP_PAD_1 ||
            log.led[i] > LED_BEAT_LOOP_PAD_8) {
            continue;
        }
        if (log.led[i] == LED_BEAT_LOOP_PAD_6 && log.state[i] == 1) {
            saw_pad_6_on = true;
        } else if (log.state[i] != 0) {
            saw_other_pad_on = true;
        }
    }
    assert(saw_pad_6_on);
    assert(!saw_other_pad_on);
}

static void test_beat_loop_pad_leds_are_off_outside_beat_loop_mode(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;
    input.loop_active[0] = 1;
    input.loop_start_ms[0] = 10000;
    input.loop_end_ms[0] = 10500;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, true,
                                      capture_send, &log) == ESP_OK);

    for (int i = 0; i < log.count; i++) {
        if (log.deck[i] == CTRL_DECK_1 &&
            log.led[i] >= LED_BEAT_LOOP_PAD_1 &&
            log.led[i] <= LED_BEAT_LOOP_PAD_8) {
            assert(log.state[i] == 0);
        }
    }
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
    assert(log.count == 46);

    input.pad_mode[1] = CTRL_PAD_MODE_SAMPLER;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 48);
    assert(log.led[46] == LED_PAD_MODE_HOT_CUE);
    assert(log.deck[46] == 1);
    assert(log.state[46] == 0);
    assert(log.led[47] == LED_PAD_MODE_SAMPLER);
    assert(log.deck[47] == 1);
    assert(log.state[47] == 1);
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
    assert(log.count == 46);
    assert(log.failed_led == LED_PLAY);
    assert(log.failed_deck == 0);

    log.fail_on_call = 0;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 47);
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
    assert(log.count == 46);

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
    assert(log.count == 46);
    assert(log.state[0] == 0);
    assert(log.led[3] == LED_SYNC && log.state[3] == 1);
    assert(log.state[25] == 0);
    assert(log.state[26] == 0);
    assert(log.led[4] == LED_PAD_MODE_HOT_CUE && log.state[4] == 1);
    assert(log.led[28] == LED_PAD_MODE_HOT_CUE && log.state[28] == 1);
    assert(log.led[36] == LED_LOOP_IN && log.state[36] == 0);
    assert(log.led[37] == LED_LOOP_OUT && log.state[37] == 0);
}

int main(void)
{
    test_initial_forced_snapshot_sends_all_mvp_leds();
    test_normal_publish_suppresses_unchanged_values();
    test_changed_play_sends_only_changed_play_led();
    test_changed_sync_sends_only_changed_sync_led();
    test_changed_loop_state_sends_loop_in_and_out_for_deck();
    test_loop_in_marker_lights_loop_in_without_loop_out();
    test_beat_loop_mode_lights_matching_loop_length_pad();
    test_beat_loop_pad_leds_are_off_outside_beat_loop_mode();
    test_changed_pad_mode_sends_old_off_and_new_on_for_deck();
    test_failed_send_retries_on_next_normal_publish();
    test_forced_reconnect_snapshot_sends_all_values_including_off();
    puts("flx4_led_snapshot tests passed");
    return 0;
}
