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

    switch (frame[1]) {
    case CTRL_TYPE_BUTTON:
        ev->type = CTRL_EV_BUTTON;
        return true;
    case CTRL_TYPE_ENCODER:
        if (ev->id == 0) {
            ev->type = CTRL_EV_JOG;
            return true;
        }
        if (ev->id == 1) {
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

    build_frame(frame, CTRL_TYPE_ENCODER, 2, 1, 10);
    assert(!decode_p4_frame(frame, &ev));
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
    test_button_load_decodes();
    test_encoder_ids_route_to_jog_and_browse();
    test_led_command_values_and_bad_checksum();
    puts("control_link_protocol tests passed");
    return 0;
}
