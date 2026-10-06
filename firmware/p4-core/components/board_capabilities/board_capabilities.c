#include "board_capabilities.h"
#ifndef BOARD_CAPABILITIES_PC_TEST
#include "sdkconfig.h"
#endif

static const board_capabilities_t boards[] = {
    {BOARD_JC4880, "JC4880P443C_I_W", "main-deck-p4", 800, 480, 480, 800,
     270, 0, 1, 0, false, true, true, false, 2, false, false, false, 0, "Pajoniiir", false, false, true, false, false},
    {BOARD_JC1060, "JC1060P470C_I_W_Y", "main-deck-jc1060", 1024, 600, 1024, 600,
     0, 1, 0, 0, true, false, false, true, 2, false, false, false, 0, "Pajoniiir", false, false, true, false, false},
    {BOARD_M3, "JC-ESP32P4-M3-DEV", "main-deck-m3", 800, 480, 800, 480,
     0, 0, 1, 0, false, true, true, false, 3, true, true, true, 48000, "Pajoniiir-M3", true, true, false, true, true},
};
const board_capabilities_t *board_capabilities_for(board_id_t id)
{
    return id == BOARD_JC4880 || id == BOARD_JC1060 || id == BOARD_M3 ? &boards[id] : 0;
}
const board_capabilities_t *board_capabilities_get(void)
{
#if defined(CONFIG_PAJONIIIR_BOARD_JC1060) && defined(CONFIG_PAJONIIIR_BOARD_M3)
#error "Exactly one P4 board must be selected"
#elif defined(CONFIG_PAJONIIIR_BOARD_JC1060) || defined(UI_SIM_BOARD_JC1060)
    return &boards[BOARD_JC1060];
#elif defined(CONFIG_PAJONIIIR_BOARD_M3)
    return &boards[BOARD_M3];
#else
    return &boards[BOARD_JC4880];
#endif
}
const char *board_id_name(board_id_t id)
{
    switch (id) {
    case BOARD_JC4880: return "jc4880";
    case BOARD_JC1060: return "jc1060";
    case BOARD_M3: return "m3";
    default: return "unknown";
    }
}
