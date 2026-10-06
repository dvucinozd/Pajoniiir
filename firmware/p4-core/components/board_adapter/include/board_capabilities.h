#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { BOARD_JC4880, BOARD_JC1060 } board_id_t;
typedef struct {
    board_id_t id;
    const char *name;
    const char *project;
    uint16_t display_width, display_height;
    uint16_t panel_width, panel_height;
    uint16_t panel_rotation;
    uint8_t storage_root, controller_root, fs_phy_index;
    bool ethernet, wifi, pcm5102a, sd_internal_bounce;
} board_capabilities_t;

/* Pure immutable descriptions; root roles never depend on enumeration order. */
const board_capabilities_t *board_capabilities_for(board_id_t id);
const board_capabilities_t *board_capabilities_get(void);
