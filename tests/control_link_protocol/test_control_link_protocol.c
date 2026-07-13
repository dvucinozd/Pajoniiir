#include "../../firmware/main-deck-p4/components/control_link/include/control_link.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int p4_btn_eject(void);
int p4_btn_track_prev(void);
int p4_btn_track_next(void);
int p4_btn_search_back(void);
int p4_btn_search_fwd(void);
int p4_btn_cue(void);
int p4_btn_play(void);
int p4_btn_master_tempo(void);
int p4_btn_load(void);
int p4_btn_count(void);
int p4_ctrl_ev_jog(void);
int p4_ctrl_ev_browse(void);
int p4_ctrl_ev_state(void);
int p4_ctrl_type_state(void);
int p4_ctrl_id_flx4_connection(void);
int p4_ctrl_id_smart_cfx(void);
int p4_ctrl_id_smart_fader(void);
int p4_ctrl_id_smart_cfx_shift(void);
int p4_ctrl_id_smart_fader_shift(void);
int p4_ctrl_id_beat_fx_select_next(void);
int p4_ctrl_id_beat_fx_clear(void);
int p4_ctrl_id_beat_fx_beat_dec_shift(void);
int p4_ctrl_id_beat_fx_beat_inc_shift(void);
int p4_ctrl_id_s3_debug_ap(void);
int p4_ctrl_s3_debug_ap_off(void);
int p4_ctrl_s3_debug_ap_starting(void);
int p4_ctrl_s3_debug_ap_on(void);
int p4_ctrl_s3_debug_ap_error(void);
int p4_ctrl_id_deck1_shift(void);
int p4_ctrl_id_deck2_to_start(void);
int p4_ctrl_id_deck1_sync(void);
int p4_ctrl_id_deck2_tempo_range(void);
int p4_ctrl_id_deck2_pad_action(void);
int p4_ctrl_id_ch1_trim(void);
int p4_ctrl_id_headphone_mix(void);
int p4_ctrl_id_headphone_level(void);
int p4_ctrl_id_master_volume(void);
int p4_ctrl_id_master_cue(void);
int p4_ctrl_id_deck1_jog_search(void);
int p4_ctrl_id_deck2_jog_search_touch(void);
int p4_ctrl_id_browse_shift_delta(void);
int p4_ctrl_id_browse_shift_press(void);
int p4_ctrl_id_shift_load_deck1(void);
int p4_ctrl_id_shift_load_deck2(void);
int p4_ctrl_id_deck1_ext_action(void);
int p4_ctrl_id_deck2_ext_action(void);
int p4_ctrl_deck_ext_action_censor(void);
int p4_ctrl_deck_ext_action_sync_master(void);
int p4_ctrl_deck_ext_action_reloop_stop(void);
int p4_ctrl_deck_ext_action_loop_adjust_in(void);
int p4_ctrl_deck_ext_action_loop_adjust_out(void);
int p4_ctrl_deck_ext_action_quantize(void);
int p4_led_vu_meter(void);
int p4_led_pad_mode_hot_cue(void);
int p4_led_pad_mode_key_shift(void);
int p4_led_sync(void);
int p4_led_loop_in(void);
int p4_led_loop_out(void);
int p4_led_master_cue(void);
int p4_led_beat_jump_pad_1(void);
int p4_led_beat_jump_pad_8(void);
int p4_led_beat_jump_shift_helper_7(void);
int p4_led_beat_jump_shift_helper_8(void);
int p4_ctrl_flx4_disconnected(void);
int p4_ctrl_flx4_connected(void);

int s3_btn_eject(void);
int s3_btn_track_prev(void);
int s3_btn_track_next(void);
int s3_btn_search_back(void);
int s3_btn_search_fwd(void);
int s3_btn_cue(void);
int s3_btn_play(void);
int s3_btn_master_tempo(void);
int s3_btn_load(void);
int s3_btn_count(void);
int s3_panel_ev_jog(void);
int s3_panel_ev_browse(void);
int s3_control_link_uart_tx_gpio(void);
int s3_control_link_uart_rx_gpio(void);
int s3_ctrl_id_deck1_play(void);
int s3_ctrl_id_deck2_cue(void);
int s3_ctrl_id_deck1_tempo(void);
int s3_ctrl_id_ch1_volume(void);
int s3_ctrl_id_crossfader(void);
int s3_ctrl_id_browse_delta(void);
int s3_ctrl_id_load_deck2(void);
int s3_ctrl_id_shift_load_deck1(void);
int s3_ctrl_id_shift_load_deck2(void);
int s3_ctrl_id_browse_press(void);
int s3_ctrl_type_state(void);
int s3_ctrl_id_flx4_connection(void);
int s3_ctrl_id_smart_cfx(void);
int s3_ctrl_id_smart_fader(void);
int s3_ctrl_id_smart_cfx_shift(void);
int s3_ctrl_id_smart_fader_shift(void);
int s3_ctrl_id_beat_fx_select_next(void);
int s3_ctrl_id_beat_fx_clear(void);
int s3_ctrl_id_beat_fx_beat_dec_shift(void);
int s3_ctrl_id_beat_fx_beat_inc_shift(void);
int s3_ctrl_id_s3_debug_ap(void);
int s3_ctrl_s3_debug_ap_off(void);
int s3_ctrl_s3_debug_ap_starting(void);
int s3_ctrl_s3_debug_ap_on(void);
int s3_ctrl_s3_debug_ap_error(void);
int s3_ctrl_id_deck1_shift(void);
int s3_ctrl_id_deck2_to_start(void);
int s3_ctrl_id_deck1_sync(void);
int s3_ctrl_id_deck2_tempo_range(void);
int s3_ctrl_id_deck2_pad_action(void);
int s3_ctrl_id_ch1_trim(void);
int s3_ctrl_id_headphone_mix(void);
int s3_ctrl_id_headphone_level(void);
int s3_ctrl_id_master_volume(void);
int s3_ctrl_id_master_cue(void);
int s3_ctrl_id_deck1_jog_search(void);
int s3_ctrl_id_deck2_jog_search_touch(void);
int s3_ctrl_id_browse_shift_delta(void);
int s3_ctrl_id_browse_shift_press(void);
int s3_ctrl_id_deck1_ext_action(void);
int s3_ctrl_id_deck2_ext_action(void);
int s3_ctrl_deck_ext_action_censor(void);
int s3_ctrl_deck_ext_action_sync_master(void);
int s3_ctrl_deck_ext_action_reloop_stop(void);
int s3_ctrl_deck_ext_action_loop_adjust_in(void);
int s3_ctrl_deck_ext_action_loop_adjust_out(void);
int s3_ctrl_deck_ext_action_quantize(void);
int s3_led_vu_meter(void);
int s3_led_pad_mode_hot_cue(void);
int s3_led_pad_mode_key_shift(void);
int s3_led_sync(void);
int s3_led_loop_in(void);
int s3_led_loop_out(void);
int s3_led_master_cue(void);
int s3_led_beat_jump_pad_1(void);
int s3_led_beat_jump_pad_8(void);
int s3_led_beat_jump_shift_helper_7(void);
int s3_led_beat_jump_shift_helper_8(void);
int s3_ctrl_flx4_disconnected(void);
int s3_ctrl_flx4_connected(void);

static void build_frame(uint8_t frame[CTRL_FRAME_LEN],
                        uint8_t type, uint8_t id, int16_t value, uint8_t seq)
{
    uint16_t v = (uint16_t)value;
    frame[0] = CTRL_FRAME_START;
    frame[1] = type;
    frame[2] = id;
    frame[3] = (uint8_t)(v & 0xFF);
    frame[4] = (uint8_t)((v >> 8) & 0xFF);
    frame[5] = seq;
    frame[6] = frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5];
}

static bool decode_p4_frame(const uint8_t frame[CTRL_FRAME_LEN], ctrl_event_t *ev)
{
    uint8_t chk = frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5];
    if (frame[0] != CTRL_FRAME_START || chk != frame[6]) {
        return false;
    }

    ev->id = frame[2];
    ev->value = (int16_t)((uint16_t)frame[3] | ((uint16_t)frame[4] << 8));
    ev->seq = frame[5];
    ev->deck = control_link_id_deck(frame[2]);
    ev->control = control_link_id_control(frame[2]);

    switch (frame[1]) {
    case CTRL_TYPE_BUTTON:
        ev->type = CTRL_EV_BUTTON;
        return true;
    case CTRL_TYPE_ENCODER:
        if (ev->id == 0 || control_link_id_is_deck_jog(ev->id)) {
            ev->type = CTRL_EV_JOG;
            return true;
        }
        if (ev->id == 1 ||
            ev->id == CTRL_ID_BROWSE_DELTA ||
            ev->id == CTRL_ID_BROWSE_SHIFT_DELTA) {
            ev->type = CTRL_EV_BROWSE;
            return true;
        }
        return false;
    case CTRL_TYPE_PITCH:
        ev->type = CTRL_EV_PITCH;
        return true;
    case CTRL_TYPE_HEARTBEAT:
        ev->type = CTRL_EV_HEARTBEAT;
        return true;
    case CTRL_TYPE_STATE:
        ev->type = CTRL_EV_STATE;
        return true;
    default:
        return false;
    }
}

static void test_button_ids_match_and_existing_ids_stay_stable(void)
{
    assert(s3_btn_eject() == p4_btn_eject());
    assert(s3_btn_track_prev() == p4_btn_track_prev());
    assert(s3_btn_track_next() == p4_btn_track_next());
    assert(s3_btn_search_back() == p4_btn_search_back());
    assert(s3_btn_search_fwd() == p4_btn_search_fwd());
    assert(s3_btn_cue() == p4_btn_cue());
    assert(s3_btn_play() == p4_btn_play());
    assert(s3_btn_master_tempo() == p4_btn_master_tempo());

    assert(p4_btn_play() == 6);
    assert(p4_btn_master_tempo() == 12);
    assert(s3_btn_load() == p4_btn_load());
    assert(p4_btn_load() == 13);
    assert(s3_btn_count() == p4_btn_count());
    assert(p4_btn_count() == 14);
}

static void test_s3_xiao_control_link_uart_pin_defaults(void)
{
    assert(s3_control_link_uart_tx_gpio() == 5);
    assert(s3_control_link_uart_rx_gpio() == 6);
}

static void test_button_load_decodes(void)
{
    uint8_t frame[CTRL_FRAME_LEN];
    ctrl_event_t ev;
    build_frame(frame, CTRL_TYPE_BUTTON, (uint8_t)p4_btn_load(), 1, 7);

    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_BUTTON);
    assert(ev.id == p4_btn_load());
    assert(ev.value == 1);
    assert(ev.seq == 7);
}

static void test_encoder_ids_route_to_jog_and_browse(void)
{
    uint8_t frame[CTRL_FRAME_LEN];
    ctrl_event_t ev;

    assert(s3_panel_ev_jog() != s3_panel_ev_browse());
    assert(p4_ctrl_ev_jog() != p4_ctrl_ev_browse());

    build_frame(frame, CTRL_TYPE_ENCODER, 0, -3, 8);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_JOG);
    assert(ev.value == -3);

    build_frame(frame, CTRL_TYPE_ENCODER, 1, 2, 9);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_BROWSE);
    assert(ev.value == 2);

    build_frame(frame, CTRL_TYPE_ENCODER, CTRL_ID_BROWSE_DELTA, -2, 10);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_BROWSE);
    assert(ev.id == CTRL_ID_BROWSE_DELTA);
    assert(ev.value == -2);

    build_frame(frame, CTRL_TYPE_ENCODER, CTRL_ID_BROWSE_SHIFT_DELTA, 4, 12);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_BROWSE);
    assert(ev.id == CTRL_ID_BROWSE_SHIFT_DELTA);
    assert(ev.value == 4);

    build_frame(frame, CTRL_TYPE_ENCODER, 2, 1, 11);
    assert(!decode_p4_frame(frame, &ev));
}

static void test_firmware_decodes_deck_aware_flx4_ids(void)
{
    uint8_t frame[CTRL_FRAME_LEN];
    ctrl_event_t ev;

    build_frame(frame, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_PLAY, 1, 20);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_BUTTON);
    assert(ev.id == CTRL_ID_DECK1_PLAY);
    assert(ev.deck == CTRL_DECK_1);
    assert(ev.control == CTRL_DECK_CTL_PLAY);

    build_frame(frame, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_CUE, 1, 21);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_BUTTON);
    assert(ev.id == CTRL_ID_DECK2_CUE);
    assert(ev.deck == CTRL_DECK_2);
    assert(ev.control == CTRL_DECK_CTL_CUE);

    build_frame(frame, CTRL_TYPE_ENCODER, CTRL_ID_DECK2_JOG_SCRATCH, -4, 22);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_JOG);
    assert(ev.deck == CTRL_DECK_2);
    assert(ev.control == CTRL_DECK_CTL_JOG_SCRATCH);

    build_frame(frame, CTRL_TYPE_ENCODER, CTRL_ID_DECK1_JOG_SEARCH, 3, 23);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_JOG);
    assert(ev.deck == CTRL_DECK_1);
    assert(ev.control == CTRL_DECK_CTL_JOG_SEARCH);

    build_frame(frame, CTRL_TYPE_PITCH, CTRL_ID_DECK1_TEMPO, 9000, 24);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_PITCH);
    assert(ev.deck == CTRL_DECK_1);
    assert(ev.control == CTRL_DECK_CTL_TEMPO);

    build_frame(frame, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_SHIFT, 1, 25);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_BUTTON);
    assert(ev.deck == CTRL_DECK_1);
    assert(ev.control == CTRL_DECK_CTL_SHIFT);

    build_frame(frame, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_TO_START, 1, 26);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_BUTTON);
    assert(ev.deck == CTRL_DECK_2);
    assert(ev.control == CTRL_DECK_CTL_TO_START);

    build_frame(frame, CTRL_TYPE_BUTTON, CTRL_ID_DECK2_PAD_ACTION,
                CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_JUMP, 6, true, true), 27);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_BUTTON);
    assert(ev.deck == CTRL_DECK_2);
    assert(ev.control == CTRL_DECK_CTL_PAD_ACTION);
    assert(CTRL_PAD_ACTION_PAD(ev.value) == 6);
    assert(CTRL_PAD_ACTION_MODE(ev.value) == CTRL_PAD_MODE_BEAT_JUMP);
    assert(CTRL_PAD_ACTION_SHIFTED(ev.value));
    assert(CTRL_PAD_ACTION_PRESSED(ev.value));

    build_frame(frame, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_EXT_ACTION,
                CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_CENSOR, true), 28);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_BUTTON);
    assert(ev.deck == CTRL_DECK_1);
    assert(ev.control == CTRL_DECK_CTL_EXT_ACTION);
    assert(CTRL_DECK_EXT_ACTION(ev.value) == CTRL_DECK_EXT_ACTION_CENSOR);
    assert(CTRL_DECK_EXT_PRESSED(ev.value));
}

static void test_s3_and_p4_deck_aware_ids_match(void)
{
    assert(CTRL_ID_DECK1_PLAY == 0x10);
    assert(CTRL_ID_DECK1_PAD_ACTION == 0x25);
    assert(CTRL_ID_DECK2_PLAY == 0x30);
    assert(CTRL_ID_DECK2_PAD_ACTION == 0x45);
    assert(CTRL_ID_CH1_VOLUME == 0x50);
    assert(CTRL_ID_BROWSE_DELTA == 0x60);

    assert(s3_ctrl_id_deck1_play() == CTRL_ID_DECK1_PLAY);
    assert(s3_ctrl_id_deck2_cue() == CTRL_ID_DECK2_CUE);
    assert(s3_ctrl_id_deck1_tempo() == CTRL_ID_DECK1_TEMPO);
    assert(s3_ctrl_id_ch1_volume() == CTRL_ID_CH1_VOLUME);
    assert(s3_ctrl_id_crossfader() == CTRL_ID_CROSSFADER);
    assert(s3_ctrl_id_browse_delta() == CTRL_ID_BROWSE_DELTA);
    assert(s3_ctrl_id_load_deck2() == CTRL_ID_LOAD_DECK2);
    assert(s3_ctrl_id_browse_press() == CTRL_ID_BROWSE_PRESS);
    assert(s3_ctrl_id_deck1_shift() == p4_ctrl_id_deck1_shift());
    assert(s3_ctrl_id_deck1_shift() == CTRL_ID_DECK1_SHIFT);
    assert(s3_ctrl_id_deck2_to_start() == p4_ctrl_id_deck2_to_start());
    assert(s3_ctrl_id_deck2_to_start() == CTRL_ID_DECK2_TO_START);
    assert(s3_ctrl_id_deck1_sync() == p4_ctrl_id_deck1_sync());
    assert(s3_ctrl_id_deck1_sync() == CTRL_ID_DECK1_SYNC);
    assert(s3_ctrl_id_deck2_tempo_range() == p4_ctrl_id_deck2_tempo_range());
    assert(s3_ctrl_id_deck2_tempo_range() == CTRL_ID_DECK2_TEMPO_RANGE);
    assert(s3_ctrl_id_deck2_pad_action() == p4_ctrl_id_deck2_pad_action());
    assert(s3_ctrl_id_deck2_pad_action() == CTRL_ID_DECK2_PAD_ACTION);
    assert(s3_ctrl_id_ch1_trim() == p4_ctrl_id_ch1_trim());
    assert(s3_ctrl_id_ch1_trim() == CTRL_ID_CH1_TRIM);
    assert(s3_ctrl_id_headphone_mix() == p4_ctrl_id_headphone_mix());
    assert(s3_ctrl_id_headphone_mix() == CTRL_ID_HEADPHONE_MIX);
    assert(s3_ctrl_id_headphone_level() == p4_ctrl_id_headphone_level());
    assert(s3_ctrl_id_headphone_level() == CTRL_ID_HEADPHONE_LEVEL);
    assert(s3_ctrl_id_headphone_level() != s3_ctrl_id_smart_cfx());
    assert(s3_ctrl_id_headphone_level() == 0x7D);
    assert(s3_ctrl_id_master_volume() == p4_ctrl_id_master_volume());
    assert(s3_ctrl_id_master_volume() == CTRL_ID_MASTER_VOLUME);
    assert(s3_ctrl_id_master_cue() == p4_ctrl_id_master_cue());
    assert(s3_ctrl_id_master_cue() == CTRL_ID_MASTER_CUE);
    assert(s3_ctrl_id_deck1_jog_search() == p4_ctrl_id_deck1_jog_search());
    assert(s3_ctrl_id_deck1_jog_search() == CTRL_ID_DECK1_JOG_SEARCH);
    assert(s3_ctrl_id_deck2_jog_search_touch() == p4_ctrl_id_deck2_jog_search_touch());
    assert(s3_ctrl_id_deck2_jog_search_touch() == CTRL_ID_DECK2_JOG_SEARCH_TOUCH);
    assert(s3_ctrl_id_browse_shift_delta() == p4_ctrl_id_browse_shift_delta());
    assert(s3_ctrl_id_browse_shift_delta() == CTRL_ID_BROWSE_SHIFT_DELTA);
    assert(s3_ctrl_id_browse_shift_press() == p4_ctrl_id_browse_shift_press());
    assert(s3_ctrl_id_browse_shift_press() == CTRL_ID_BROWSE_SHIFT_PRESS);
    assert(s3_ctrl_id_shift_load_deck1() == p4_ctrl_id_shift_load_deck1());
    assert(s3_ctrl_id_shift_load_deck1() == CTRL_ID_SHIFT_LOAD_DECK1);
    assert(s3_ctrl_id_shift_load_deck1() == 0x66);
    assert(s3_ctrl_id_shift_load_deck2() == p4_ctrl_id_shift_load_deck2());
    assert(s3_ctrl_id_shift_load_deck2() == CTRL_ID_SHIFT_LOAD_DECK2);
    assert(s3_ctrl_id_shift_load_deck2() == 0x67);
    assert(s3_ctrl_id_deck1_ext_action() == p4_ctrl_id_deck1_ext_action());
    assert(s3_ctrl_id_deck1_ext_action() == CTRL_ID_DECK1_EXT_ACTION);
    assert(s3_ctrl_id_deck2_ext_action() == p4_ctrl_id_deck2_ext_action());
    assert(s3_ctrl_id_deck2_ext_action() == CTRL_ID_DECK2_EXT_ACTION);
    assert(s3_ctrl_deck_ext_action_censor() == p4_ctrl_deck_ext_action_censor());
    assert(s3_ctrl_deck_ext_action_sync_master() == p4_ctrl_deck_ext_action_sync_master());
    assert(s3_ctrl_deck_ext_action_reloop_stop() == p4_ctrl_deck_ext_action_reloop_stop());
    assert(s3_ctrl_deck_ext_action_loop_adjust_in() == p4_ctrl_deck_ext_action_loop_adjust_in());
    assert(s3_ctrl_deck_ext_action_loop_adjust_out() == p4_ctrl_deck_ext_action_loop_adjust_out());
    assert(s3_ctrl_deck_ext_action_quantize() == p4_ctrl_deck_ext_action_quantize());
    assert(CTRL_DECK_EXT_ACTION(CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_QUANTIZE, true)) ==
           CTRL_DECK_EXT_ACTION_QUANTIZE);
    assert(CTRL_DECK_EXT_PRESSED(CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_QUANTIZE, true)));
    assert(!CTRL_DECK_EXT_PRESSED(CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_QUANTIZE, false)));
    assert(s3_led_vu_meter() == p4_led_vu_meter());
    assert(p4_led_vu_meter() == LED_VU_METER);
    assert(s3_led_pad_mode_hot_cue() == p4_led_pad_mode_hot_cue());
    assert(p4_led_pad_mode_hot_cue() == LED_PAD_MODE_HOT_CUE);
    assert(s3_led_pad_mode_key_shift() == p4_led_pad_mode_key_shift());
    assert(p4_led_pad_mode_key_shift() == LED_PAD_MODE_KEY_SHIFT);
    assert(s3_led_sync() == p4_led_sync());
    assert(p4_led_sync() == LED_SYNC);
    assert(s3_led_loop_in() == p4_led_loop_in());
    assert(p4_led_loop_in() == LED_LOOP_IN);
    assert(s3_led_loop_out() == p4_led_loop_out());
    assert(p4_led_loop_out() == LED_LOOP_OUT);
    assert(s3_led_master_cue() == p4_led_master_cue());
    assert(p4_led_master_cue() == LED_MASTER_CUE);
    assert(s3_led_beat_jump_pad_1() == p4_led_beat_jump_pad_1());
    assert(s3_led_beat_jump_pad_8() == p4_led_beat_jump_pad_8());
    assert(p4_led_beat_jump_pad_1() == LED_BEAT_JUMP_PAD_1);
    assert(p4_led_beat_jump_pad_8() == LED_BEAT_JUMP_PAD_8);
    assert(p4_led_beat_jump_pad_8() == p4_led_beat_jump_pad_1() + 7);
    assert(s3_led_beat_jump_shift_helper_7() == p4_led_beat_jump_shift_helper_7());
    assert(s3_led_beat_jump_shift_helper_8() == p4_led_beat_jump_shift_helper_8());
    assert(p4_led_beat_jump_shift_helper_7() == LED_BEAT_JUMP_SHIFT_HELPER_7);
    assert(p4_led_beat_jump_shift_helper_8() == LED_BEAT_JUMP_SHIFT_HELPER_8);
    assert(p4_led_beat_jump_shift_helper_7() == p4_led_beat_jump_pad_8() + 1);
    assert(p4_led_beat_jump_shift_helper_8() == p4_led_beat_jump_shift_helper_7() + 1);
}

static void test_s3_and_p4_flx4_connection_state_ids_match(void)
{
    assert(s3_ctrl_type_state() == p4_ctrl_type_state());
    assert(s3_ctrl_type_state() == CTRL_TYPE_STATE);
    assert(p4_ctrl_ev_state() == CTRL_EV_STATE);
    assert(s3_ctrl_id_flx4_connection() == p4_ctrl_id_flx4_connection());
    assert(s3_ctrl_id_flx4_connection() == CTRL_ID_FLX4_CONNECTION);
    assert(s3_ctrl_flx4_disconnected() == p4_ctrl_flx4_disconnected());
    assert(s3_ctrl_flx4_disconnected() == CTRL_FLX4_DISCONNECTED);
    assert(s3_ctrl_flx4_connected() == p4_ctrl_flx4_connected());
    assert(s3_ctrl_flx4_connected() == CTRL_FLX4_CONNECTED);
    assert(s3_ctrl_id_smart_cfx() == p4_ctrl_id_smart_cfx());
    assert(s3_ctrl_id_smart_cfx() == CTRL_ID_SMART_CFX);
    assert(s3_ctrl_id_smart_fader() == p4_ctrl_id_smart_fader());
    assert(s3_ctrl_id_smart_fader() == CTRL_ID_SMART_FADER);
    assert(s3_ctrl_id_smart_cfx_shift() == p4_ctrl_id_smart_cfx_shift());
    assert(s3_ctrl_id_smart_cfx_shift() == CTRL_ID_SMART_CFX_SHIFT);
    assert(s3_ctrl_id_smart_cfx_shift() == 0x7E);
    assert(s3_ctrl_id_smart_fader_shift() == p4_ctrl_id_smart_fader_shift());
    assert(s3_ctrl_id_smart_fader_shift() == CTRL_ID_SMART_FADER_SHIFT);
    assert(s3_ctrl_id_smart_fader_shift() == 0x7F);
    assert(s3_ctrl_id_smart_cfx_shift() != s3_ctrl_id_smart_cfx());
    assert(s3_ctrl_id_smart_fader_shift() != s3_ctrl_id_smart_fader());
    assert(s3_ctrl_id_beat_fx_select_next() == p4_ctrl_id_beat_fx_select_next());
    assert(s3_ctrl_id_beat_fx_select_next() == CTRL_ID_BEAT_FX_SELECT_NEXT);
    assert(s3_ctrl_id_beat_fx_select_next() == 0x73);
    assert(s3_ctrl_id_beat_fx_clear() == p4_ctrl_id_beat_fx_clear());
    assert(s3_ctrl_id_beat_fx_clear() == CTRL_ID_BEAT_FX_CLEAR);
    assert(s3_ctrl_id_beat_fx_clear() == 0x7A);
    assert(s3_ctrl_id_beat_fx_beat_dec_shift() == p4_ctrl_id_beat_fx_beat_dec_shift());
    assert(s3_ctrl_id_beat_fx_beat_dec_shift() == CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT);
    assert(s3_ctrl_id_beat_fx_beat_dec_shift() == 0x83);
    assert(s3_ctrl_id_beat_fx_beat_inc_shift() == p4_ctrl_id_beat_fx_beat_inc_shift());
    assert(s3_ctrl_id_beat_fx_beat_inc_shift() == CTRL_ID_BEAT_FX_BEAT_INC_SHIFT);
    assert(s3_ctrl_id_beat_fx_beat_inc_shift() == 0x84);
    assert(s3_ctrl_id_beat_fx_beat_dec_shift() != s3_ctrl_id_beat_fx_select_next());
    assert(s3_ctrl_id_beat_fx_beat_inc_shift() != s3_ctrl_id_beat_fx_clear());
    assert(s3_ctrl_id_beat_fx_beat_dec_shift() != s3_ctrl_id_beat_fx_beat_inc_shift());
    assert(s3_ctrl_id_s3_debug_ap() == p4_ctrl_id_s3_debug_ap());
    assert(s3_ctrl_id_s3_debug_ap() == CTRL_ID_S3_DEBUG_AP);
    assert(s3_ctrl_id_s3_debug_ap() == 0x85);
    assert(s3_ctrl_s3_debug_ap_off() == p4_ctrl_s3_debug_ap_off());
    assert(s3_ctrl_s3_debug_ap_starting() == p4_ctrl_s3_debug_ap_starting());
    assert(s3_ctrl_s3_debug_ap_on() == p4_ctrl_s3_debug_ap_on());
    assert(s3_ctrl_s3_debug_ap_error() == p4_ctrl_s3_debug_ap_error());
    assert(p4_ctrl_s3_debug_ap_off() == CTRL_S3_DEBUG_AP_OFF);
    assert(p4_ctrl_s3_debug_ap_starting() == CTRL_S3_DEBUG_AP_STARTING);
    assert(p4_ctrl_s3_debug_ap_on() == CTRL_S3_DEBUG_AP_ON);
    assert(p4_ctrl_s3_debug_ap_error() == CTRL_S3_DEBUG_AP_ERROR);
    assert(p4_ctrl_s3_debug_ap_off() == 0);
    assert(p4_ctrl_s3_debug_ap_starting() == 1);
    assert(p4_ctrl_s3_debug_ap_on() == 2);
    assert(p4_ctrl_s3_debug_ap_error() == 3);

    uint8_t frame[CTRL_FRAME_LEN];
    ctrl_event_t ev;
    build_frame(frame, CTRL_TYPE_STATE, CTRL_ID_FLX4_CONNECTION, CTRL_FLX4_CONNECTED, 31);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_STATE);
    assert(ev.id == CTRL_ID_FLX4_CONNECTION);
    assert(ev.value == CTRL_FLX4_CONNECTED);
}

int s3_ctrl_bulk_frame_start(void);
int s3_ctrl_bulk_max_payload(void);
int s3_ctrl_bulk_header_len(void);
int s3_ctrl_bulk_crc_len(void);
int s3_ctrl_bulk_type_controller_descriptor(void);
int s3_ctrl_desc_payload_len(void);
int s3_ctrl_desc_product_max(void);
int s3_ctrl_desc_cap_midi_in(void);
int s3_ctrl_desc_cap_midi_out(void);
int s3_ctrl_desc_cap_usb_audio(void);
int p4_ctrl_bulk_frame_start(void);
int p4_ctrl_bulk_max_payload(void);
int p4_ctrl_bulk_header_len(void);
int p4_ctrl_bulk_crc_len(void);
int p4_ctrl_bulk_type_controller_descriptor(void);
int p4_ctrl_desc_payload_len(void);
int p4_ctrl_desc_product_max(void);
int p4_ctrl_desc_cap_midi_in(void);
int p4_ctrl_desc_cap_midi_out(void);
int p4_ctrl_desc_cap_usb_audio(void);

int s3_ctrl_bulk_type_profile_begin(void);
int s3_ctrl_bulk_type_profile_chunk(void);
int s3_ctrl_bulk_type_profile_end(void);
int s3_ctrl_bulk_type_profile_ack(void);
int s3_ctrl_bulk_type_profile_nack(void);
int s3_ctrl_bulk_type_profile_activate(void);
int s3_ctrl_bulk_type_profile_status(void);
int s3_ctrl_bulk_type_profile_clear(void);
int s3_ctrl_bulk_type_firmware_report(void);
int s3_ctrl_fw_report_len(void);
int s3_ctrl_fw_slot_ota_0(void);
int s3_ctrl_fw_state_valid(void);
int s3_ctrl_profile_begin_len(void);
int s3_ctrl_profile_chunk_max(void);
int s3_ctrl_profile_nack_crc(void);
int s3_ctrl_profile_state_active(void);
int p4_ctrl_bulk_type_profile_begin(void);
int p4_ctrl_bulk_type_profile_chunk(void);
int p4_ctrl_bulk_type_profile_end(void);
int p4_ctrl_bulk_type_profile_ack(void);
int p4_ctrl_bulk_type_profile_nack(void);
int p4_ctrl_bulk_type_profile_activate(void);
int p4_ctrl_bulk_type_profile_status(void);
int p4_ctrl_bulk_type_profile_clear(void);
int p4_ctrl_bulk_type_firmware_report(void);
int p4_ctrl_fw_report_len(void);
int p4_ctrl_fw_slot_ota_0(void);
int p4_ctrl_fw_state_valid(void);
int p4_ctrl_profile_begin_len(void);
int p4_ctrl_profile_chunk_max(void);
int p4_ctrl_profile_nack_crc(void);
int p4_ctrl_profile_state_active(void);

static void test_s3_and_p4_bulk_frame_constants_match(void)
{
    assert(s3_ctrl_bulk_frame_start() == p4_ctrl_bulk_frame_start());
    assert(s3_ctrl_bulk_frame_start() == 0xA6);
    assert(s3_ctrl_bulk_frame_start() != CTRL_FRAME_START);
    assert(s3_ctrl_bulk_max_payload() == p4_ctrl_bulk_max_payload());
    assert(s3_ctrl_bulk_max_payload() == 128);
    assert(s3_ctrl_bulk_header_len() == p4_ctrl_bulk_header_len());
    assert(s3_ctrl_bulk_crc_len() == p4_ctrl_bulk_crc_len());
    assert(s3_ctrl_bulk_type_controller_descriptor() ==
           p4_ctrl_bulk_type_controller_descriptor());
    assert(s3_ctrl_bulk_type_controller_descriptor() == 0x01);
    assert(s3_ctrl_desc_payload_len() == p4_ctrl_desc_payload_len());
    assert(s3_ctrl_desc_payload_len() ==
           6 + s3_ctrl_desc_product_max());
    assert(s3_ctrl_desc_product_max() == p4_ctrl_desc_product_max());
    assert(s3_ctrl_desc_cap_midi_in() == p4_ctrl_desc_cap_midi_in());
    assert(s3_ctrl_desc_cap_midi_out() == p4_ctrl_desc_cap_midi_out());
    assert(s3_ctrl_desc_cap_usb_audio() == p4_ctrl_desc_cap_usb_audio());
    assert(s3_ctrl_desc_payload_len() <= s3_ctrl_bulk_max_payload());

    /* Profile transfer frame types + payload sizing must agree byte-for-byte. */
    assert(s3_ctrl_bulk_type_profile_begin() == p4_ctrl_bulk_type_profile_begin());
    assert(s3_ctrl_bulk_type_profile_begin() == 0x02);
    assert(s3_ctrl_bulk_type_profile_chunk() == p4_ctrl_bulk_type_profile_chunk());
    assert(s3_ctrl_bulk_type_profile_end() == p4_ctrl_bulk_type_profile_end());
    assert(s3_ctrl_bulk_type_profile_ack() == p4_ctrl_bulk_type_profile_ack());
    assert(s3_ctrl_bulk_type_profile_nack() == p4_ctrl_bulk_type_profile_nack());
    assert(s3_ctrl_bulk_type_profile_activate() == p4_ctrl_bulk_type_profile_activate());
    assert(s3_ctrl_bulk_type_profile_status() == p4_ctrl_bulk_type_profile_status());
    assert(s3_ctrl_bulk_type_profile_clear() == p4_ctrl_bulk_type_profile_clear());
    assert(s3_ctrl_bulk_type_profile_clear() == 0x09);
    assert(s3_ctrl_bulk_type_firmware_report() == p4_ctrl_bulk_type_firmware_report());
    assert(s3_ctrl_bulk_type_firmware_report() == 0x0A);
    assert(s3_ctrl_fw_report_len() == p4_ctrl_fw_report_len());
    assert(s3_ctrl_fw_report_len() == 34);
    assert(s3_ctrl_fw_slot_ota_0() == p4_ctrl_fw_slot_ota_0());
    assert(s3_ctrl_fw_state_valid() == p4_ctrl_fw_state_valid());
    assert(s3_ctrl_profile_begin_len() == p4_ctrl_profile_begin_len());
    assert(s3_ctrl_profile_begin_len() == 12);
    assert(s3_ctrl_profile_chunk_max() == p4_ctrl_profile_chunk_max());
    assert(s3_ctrl_profile_chunk_max() == s3_ctrl_bulk_max_payload() - 4);
    assert(s3_ctrl_profile_nack_crc() == p4_ctrl_profile_nack_crc());
    assert(s3_ctrl_profile_state_active() == p4_ctrl_profile_state_active());
}

static void test_led_command_values_and_bad_checksum(void)
{
    uint8_t frame[CTRL_FRAME_LEN];
    ctrl_event_t ev;

    build_frame(frame, CTRL_TYPE_LED, LED_PLAY, 2, 11);
    assert(frame[1] == CTRL_TYPE_LED);
    assert(frame[2] == LED_PLAY);
    assert(frame[3] == 2);

    build_frame(frame, CTRL_TYPE_BUTTON, BTN_PLAY, 1, 12);
    frame[6] ^= 0x55;
    assert(!decode_p4_frame(frame, &ev));
}

int main(void)
{
    test_button_ids_match_and_existing_ids_stay_stable();
    test_s3_xiao_control_link_uart_pin_defaults();
    test_button_load_decodes();
    test_encoder_ids_route_to_jog_and_browse();
    test_firmware_decodes_deck_aware_flx4_ids();
    test_s3_and_p4_deck_aware_ids_match();
    test_s3_and_p4_flx4_connection_state_ids_match();
    test_s3_and_p4_bulk_frame_constants_match();
    test_led_command_values_and_bad_checksum();
    puts("control_link_protocol tests passed");
    return 0;
}
