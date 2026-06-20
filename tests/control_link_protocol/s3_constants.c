#include "../../firmware/control-board-s3/components/panel_io/include/panel_io.h"
#include "../../firmware/control-board-s3/components/control_link/include/control_link.h"

int s3_btn_eject(void) { return BTN_EJECT; }
int s3_btn_track_prev(void) { return BTN_TRACK_PREV; }
int s3_btn_track_next(void) { return BTN_TRACK_NEXT; }
int s3_btn_search_back(void) { return BTN_SEARCH_BACK; }
int s3_btn_search_fwd(void) { return BTN_SEARCH_FWD; }
int s3_btn_cue(void) { return BTN_CUE; }
int s3_btn_play(void) { return BTN_PLAY; }
int s3_btn_master_tempo(void) { return BTN_MASTER_TEMPO; }
int s3_btn_load(void) { return BTN_LOAD; }
int s3_btn_count(void) { return BTN_COUNT; }
int s3_panel_ev_jog(void) { return PANEL_EV_JOG; }
int s3_panel_ev_browse(void) { return PANEL_EV_BROWSE; }
int s3_ctrl_id_deck1_play(void) { return CTRL_ID_DECK1_PLAY; }
int s3_ctrl_id_deck2_cue(void) { return CTRL_ID_DECK2_CUE; }
int s3_ctrl_id_deck1_tempo(void) { return CTRL_ID_DECK1_TEMPO; }
int s3_ctrl_id_ch1_volume(void) { return CTRL_ID_CH1_VOLUME; }
int s3_ctrl_id_crossfader(void) { return CTRL_ID_CROSSFADER; }
int s3_ctrl_id_browse_delta(void) { return CTRL_ID_BROWSE_DELTA; }
int s3_ctrl_id_load_deck2(void) { return CTRL_ID_LOAD_DECK2; }
int s3_ctrl_id_browse_press(void) { return CTRL_ID_BROWSE_PRESS; }
int s3_ctrl_type_state(void) { return CTRL_TYPE_STATE; }
int s3_ctrl_id_flx4_connection(void) { return CTRL_ID_FLX4_CONNECTION; }
int s3_ctrl_id_smart_cfx(void) { return CTRL_ID_SMART_CFX; }
int s3_ctrl_id_smart_fader(void) { return CTRL_ID_SMART_FADER; }
int s3_ctrl_id_deck1_shift(void) { return CTRL_ID_DECK1_SHIFT; }
int s3_ctrl_id_deck2_to_start(void) { return CTRL_ID_DECK2_TO_START; }
int s3_ctrl_id_deck1_sync(void) { return CTRL_ID_DECK1_SYNC; }
int s3_ctrl_id_deck2_tempo_range(void) { return CTRL_ID_DECK2_TEMPO_RANGE; }
int s3_ctrl_flx4_disconnected(void) { return CTRL_FLX4_DISCONNECTED; }
int s3_ctrl_flx4_connected(void) { return CTRL_FLX4_CONNECTED; }
