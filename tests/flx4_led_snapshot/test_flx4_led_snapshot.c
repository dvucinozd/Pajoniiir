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
    led_id_t led[128];
    uint8_t state[128];
    uint8_t deck[128];
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

    assert(log.count == 98);
    assert(log.led[0] == LED_CUE && log.deck[0] == 0 && log.state[0] == 0);
    assert(log.led[1] == LED_PLAY && log.deck[1] == 0 && log.state[1] == 0);
    assert(log.led[2] == LED_PFL && log.deck[2] == 0 && log.state[2] == 0);
    assert(log.led[3] == LED_SYNC && log.deck[3] == 0 && log.state[3] == 0);
    assert(log.led[4] == LED_PAD_MODE_HOT_CUE && log.deck[4] == 0 && log.state[4] == 1);
    assert(log.led[5] == LED_PAD_MODE_KEYBOARD && log.deck[5] == 0 && log.state[5] == 0);
    assert(log.led[12] == LED_LOOP_IN && log.deck[12] == 0 && log.state[12] == 0);
    assert(log.led[13] == LED_LOOP_OUT && log.deck[13] == 0 && log.state[13] == 0);
    assert(log.led[38] == LED_SMART_CFX && log.deck[38] == 0 && log.state[38] == 0);
    assert(log.led[39] == LED_SMART_FADER && log.deck[39] == 0 && log.state[39] == 0);
    assert(log.led[40] == LED_BEAT_FX_ON && log.deck[40] == 0 && log.state[40] == 0);
    assert(log.led[41] == LED_MASTER_CUE && log.deck[41] == 0 && log.state[41] == 0);
    assert(log.led[50] == LED_CENSOR && log.deck[50] == 0 && log.state[50] == 0);
    assert(log.led[51] == LED_CUE && log.deck[51] == 1 && log.state[51] == 0);
    assert(log.led[52] == LED_PLAY && log.deck[52] == 1 && log.state[52] == 0);
    assert(log.led[53] == LED_PFL && log.deck[53] == 1 && log.state[53] == 0);
    assert(log.led[54] == LED_SYNC && log.deck[54] == 1 && log.state[54] == 0);
    assert(log.led[55] == LED_PAD_MODE_HOT_CUE && log.deck[55] == 1 && log.state[55] == 1);
    assert(log.led[97] == LED_CENSOR && log.deck[97] == 1 && log.state[97] == 0);
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
    assert(log.count == 98);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 98);
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
    assert(log.count == 98);

    input.play[0] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 99);
    assert(log.led[98] == LED_PLAY);
    assert(log.deck[98] == 0);
    assert(log.state[98] == 1);
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
    assert(log.count == 98);

    input.sync[0] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 99);
    assert(log.led[98] == LED_SYNC);
    assert(log.deck[98] == 0);
    assert(log.state[98] == 1);
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
    assert(log.count == 98);

    input.loop_active[1] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 100);
    assert(log.led[98] == LED_LOOP_IN);
    assert(log.deck[98] == 1);
    assert(log.state[98] == 1);
    assert(log.led[99] == LED_LOOP_OUT);
    assert(log.deck[99] == 1);
    assert(log.state[99] == 1);
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
    assert(log.count == 98);

    input.loop_in_marker[0] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 99);
    assert(log.led[98] == LED_LOOP_IN);
    assert(log.deck[98] == 0);
    assert(log.state[98] == 1);
}

static void test_changed_beat_fx_on_sends_only_global_led(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 98);

    input.beat_fx_on = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 99);
    assert(log.led[98] == LED_BEAT_FX_ON);
    assert(log.deck[98] == CTRL_DECK_1);
    assert(log.state[98] == 1);

    input.beat_fx_on = 0;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 100);
    assert(log.led[99] == LED_BEAT_FX_ON);
    assert(log.deck[99] == CTRL_DECK_1);
    assert(log.state[99] == 0);
}

static void test_changed_master_cue_sends_only_global_led(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 98);

    input.master_cue = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 99);
    assert(log.led[98] == LED_MASTER_CUE);
    assert(log.deck[98] == CTRL_DECK_1);
    assert(log.state[98] == 1);
}

static void test_changed_censor_sends_only_changed_deck_led(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 98);

    input.censor_active[1] = 1;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 99);
    assert(log.led[98] == LED_CENSOR);
    assert(log.deck[98] == CTRL_DECK_2);
    assert(log.state[98] == 1);

    input.censor_active[1] = 0;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 100);
    assert(log.led[99] == LED_CENSOR);
    assert(log.deck[99] == CTRL_DECK_2);
    assert(log.state[99] == 0);
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
    input.loop_end_ms[0] = 10600;
    input.beat_loop_pad_active[0] = 1;
    input.beat_loop_active_pad[0] = 5;

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
    input.beat_loop_pad_active[0] = 1;
    input.beat_loop_active_pad[0] = 5;

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

static void test_hot_cue_mode_lights_existing_hot_cue_slots(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_BEAT_LOOP;
    input.hot_cue_exists_mask[0] = (1u << 0) | (1u << 4);
    input.hot_cue_exists_mask[1] = (1u << 2);

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, true,
                                      capture_send, &log) == ESP_OK);

    bool saw_deck1_pad1_on = false;
    bool saw_deck1_pad5_on = false;
    bool saw_other_deck1_pad_on = false;
    bool saw_deck2_hot_cue_on = false;
    for (int i = 0; i < log.count; i++) {
        if (log.led[i] < LED_HOT_CUE_PAD_1 ||
            log.led[i] > LED_HOT_CUE_PAD_8) {
            continue;
        }
        if (log.deck[i] == CTRL_DECK_1) {
            if (log.led[i] == LED_HOT_CUE_PAD_1 && log.state[i] == 1) {
                saw_deck1_pad1_on = true;
            } else if (log.led[i] == LED_HOT_CUE_PAD_5 && log.state[i] == 1) {
                saw_deck1_pad5_on = true;
            } else if (log.state[i] != 0) {
                saw_other_deck1_pad_on = true;
            }
        } else if (log.deck[i] == CTRL_DECK_2 && log.state[i] != 0) {
            saw_deck2_hot_cue_on = true;
        }
    }

    assert(saw_deck1_pad1_on);
    assert(saw_deck1_pad5_on);
    assert(!saw_other_deck1_pad_on);
    assert(!saw_deck2_hot_cue_on);
}

static void test_pad_fx1_mode_lights_active_pad_led_only_while_pressed(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_PAD_FX1;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_fx_active[0] = 1;
    input.pad_fx_active_mode[0] = CTRL_PAD_MODE_PAD_FX1;
    input.pad_fx_active_pad[0] = 2;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, true,
                                      capture_send, &log) == ESP_OK);

    bool saw_pad_3_on = false;
    bool saw_other_pad_on = false;
    for (int i = 0; i < log.count; i++) {
        if (log.deck[i] != CTRL_DECK_1 ||
            log.led[i] < LED_PAD_FX1_PAD_1 ||
            log.led[i] > LED_PAD_FX1_PAD_8) {
            continue;
        }
        if (log.led[i] == LED_PAD_FX1_PAD_3 && log.state[i] == 1) {
            saw_pad_3_on = true;
        } else if (log.state[i] != 0) {
            saw_other_pad_on = true;
        }
    }
    assert(saw_pad_3_on);
    assert(!saw_other_pad_on);

    memset(&log, 0, sizeof(log));
    input.pad_fx_active[0] = 0;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 1);
    assert(log.led[0] == LED_PAD_FX1_PAD_3);
    assert(log.deck[0] == CTRL_DECK_1);
    assert(log.state[0] == 0);
}

static void test_pad_fx2_mode_lights_active_pad_led(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_PAD_FX2;
    input.pad_fx_active[1] = 1;
    input.pad_fx_active_mode[1] = CTRL_PAD_MODE_PAD_FX2;
    input.pad_fx_active_pad[1] = 3;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, true,
                                      capture_send, &log) == ESP_OK);

    bool saw_pad_4_on = false;
    bool saw_other_pad_on = false;
    for (int i = 0; i < log.count; i++) {
        if (log.deck[i] != CTRL_DECK_2 ||
            log.led[i] < LED_PAD_FX2_PAD_1 ||
            log.led[i] > LED_PAD_FX2_PAD_8) {
            continue;
        }
        if (log.led[i] == LED_PAD_FX2_PAD_4 && log.state[i] == 1) {
            saw_pad_4_on = true;
        } else if (log.state[i] != 0) {
            saw_other_pad_on = true;
        }
    }
    assert(saw_pad_4_on);
    assert(!saw_other_pad_on);
}

static void test_changed_pad_mode_sends_old_off_and_new_on_for_supported_deck_mode(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 98);

    input.pad_mode[1] = CTRL_PAD_MODE_BEAT_JUMP;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 100);
    assert(log.led[98] == LED_PAD_MODE_HOT_CUE);
    assert(log.deck[98] == 1);
    assert(log.state[98] == 0);
    assert(log.led[99] == LED_PAD_MODE_BEAT_JUMP);
    assert(log.deck[99] == 1);
    assert(log.state[99] == 1);
}

static void test_out_of_scope_pad_modes_do_not_turn_on_mode_leds(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_HOT_CUE;
    input.pad_mode[1] = CTRL_PAD_MODE_HOT_CUE;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 98);

    input.pad_mode[1] = CTRL_PAD_MODE_SAMPLER;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 99);
    assert(log.led[98] == LED_PAD_MODE_HOT_CUE);
    assert(log.deck[98] == 1);
    assert(log.state[98] == 0);

    memset(&log, 0, sizeof(log));
    input.pad_mode[1] = CTRL_PAD_MODE_KEY_SHIFT;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 0);

    input.pad_mode[0] = CTRL_PAD_MODE_KEYBOARD;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 1);
    assert(log.led[0] == LED_PAD_MODE_HOT_CUE);
    assert(log.deck[0] == 0);
    assert(log.state[0] == 0);
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
    assert(log.count == 98);
    assert(log.failed_led == LED_PLAY);
    assert(log.failed_deck == 0);

    log.fail_on_call = 0;
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 99);
    assert(log.saw_retry);
}

static void test_forced_reconnect_snapshot_sends_all_values_including_off(void)
{
    flx4_led_publisher_t publisher;
    flx4_led_snapshot_input_t input = { 0 };
    send_log_t log = { 0 };

    input.pad_mode[0] = CTRL_PAD_MODE_BEAT_JUMP;
    input.pad_mode[1] = CTRL_PAD_MODE_BEAT_LOOP;
    input.cue[0] = 1;
    input.play[1] = 1;
    input.pfl[1] = 1;
    input.loop_active[1] = 1;

    flx4_led_publisher_init(&publisher);
    assert(flx4_led_publisher_publish(&publisher, &input, false,
                                      capture_send, &log) == ESP_OK);
    assert(log.count == 98);

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
    assert(log.count == 98);
    assert(log.state[0] == 0);
    assert(log.led[3] == LED_SYNC && log.state[3] == 1);
    assert(log.state[52] == 0);
    assert(log.state[53] == 0);
    assert(log.led[4] == LED_PAD_MODE_HOT_CUE && log.state[4] == 1);
    assert(log.led[55] == LED_PAD_MODE_HOT_CUE && log.state[55] == 1);
    assert(log.led[63] == LED_LOOP_IN && log.state[63] == 0);
    assert(log.led[64] == LED_LOOP_OUT && log.state[64] == 0);
}

int main(void)
{
    test_initial_forced_snapshot_sends_all_mvp_leds();
    test_normal_publish_suppresses_unchanged_values();
    test_changed_play_sends_only_changed_play_led();
    test_changed_sync_sends_only_changed_sync_led();
    test_changed_beat_fx_on_sends_only_global_led();
    test_changed_master_cue_sends_only_global_led();
    test_changed_censor_sends_only_changed_deck_led();
    test_changed_loop_state_sends_loop_in_and_out_for_deck();
    test_loop_in_marker_lights_loop_in_without_loop_out();
    test_beat_loop_mode_lights_matching_loop_length_pad();
    test_beat_loop_pad_leds_are_off_outside_beat_loop_mode();
    test_hot_cue_mode_lights_existing_hot_cue_slots();
    test_pad_fx1_mode_lights_active_pad_led_only_while_pressed();
    test_pad_fx2_mode_lights_active_pad_led();
    test_changed_pad_mode_sends_old_off_and_new_on_for_supported_deck_mode();
    test_out_of_scope_pad_modes_do_not_turn_on_mode_leds();
    test_failed_send_retries_on_next_normal_publish();
    test_forced_reconnect_snapshot_sends_all_values_including_off();
    puts("flx4_led_snapshot tests passed");
    return 0;
}
