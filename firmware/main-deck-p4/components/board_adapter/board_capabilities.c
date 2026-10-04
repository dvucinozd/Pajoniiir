#include "board_capabilities.h"
#ifndef BOARD_CAPABILITIES_PC_TEST
#include "sdkconfig.h"
#endif

static const board_capabilities_t boards[] = {
    {BOARD_JC4880, "JC4880P443C_I_W", "main-deck-p4", 800, 480, 480, 800,
     270, 0, 1, 0, false, true, true, false},
    {BOARD_JC1060, "JC1060P470C_I_W_Y", "main-deck-jc1060", 1024, 600, 1024, 600,
     0, 1, 0, 0, true, false, false, true},
};
const board_capabilities_t *board_capabilities_for(board_id_t id)
{
    return id == BOARD_JC4880 || id == BOARD_JC1060 ? &boards[id] : 0;
}
const board_capabilities_t *board_capabilities_get(void)
{
#ifdef CONFIG_PAJONIIIR_BOARD_JC1060
    return &boards[BOARD_JC1060];
#else
    return &boards[BOARD_JC4880];
#endif
}
