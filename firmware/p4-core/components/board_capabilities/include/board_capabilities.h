#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { BOARD_JC4880, BOARD_JC1060, BOARD_M3 } board_id_t;
typedef struct {
    board_id_t id;
    const char *name;
    const char *project;
    uint16_t display_width, display_height;
    uint16_t panel_width, panel_height;
    uint16_t panel_rotation;
    uint8_t storage_root, controller_root, fs_phy_index;
    bool ethernet, wifi, pcm5102a, sd_internal_bounce;
    uint8_t scanout_bytes_per_pixel;
    bool waveform_top_to_bottom, waveform_first, hosted_release_on_stop;
    uint32_t fixed_output_sample_rate;
    const char *softap_ssid;
    bool keylock_dense_correlation;
    bool audio_antialias;
    bool wifi_wpa3_transition;
    bool wifi_apsta;
    bool overview_artwork_in_title;
} board_capabilities_t;

/* Pure immutable descriptions; root roles never depend on enumeration order. */
const board_capabilities_t *board_capabilities_for(board_id_t id);
const board_capabilities_t *board_capabilities_get(void);
const char *board_id_name(board_id_t id);
