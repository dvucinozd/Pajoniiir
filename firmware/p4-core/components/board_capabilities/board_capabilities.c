#include "board_capabilities.h"
#ifndef BOARD_CAPABILITIES_PC_TEST
#include "sdkconfig.h"
#endif

#if defined(CONFIG_BSP_M3_HS_CONTROLLER) && CONFIG_BSP_M3_HS_CONTROLLER
#define M3_USB_ROLES 1, 0
#else
#define M3_USB_ROLES 0, 1
#endif

static const board_capabilities_t boards[] = {
    {BOARD_JC4880, "JC4880P443C_I_W", "main-deck-p4", 800, 480, 480, 800,
     270, 0, 1, 0, false, true, true, false, 2, false, false, false, 0, "Pajoniiir", false, false, true, false, false, 1, false},
    {BOARD_JC1060, "JC1060P470C_I_W_Y", "main-deck-jc1060", 1024, 600, 1024, 600,
     0, 1, 0, 0, true, true, false, true, 2, false, false, false, 0, "Pajoniiir-JC1060", false, false, false, true, false, 1, true},
    {BOARD_M3, "JC-ESP32P4-M3-DEV", "main-deck-m3", 800, 480, 800, 480,
     0, M3_USB_ROLES, 0, false, true, true, false, 3, true, true, true, 48000, "Pajoniiir-M3", true, true, false, true, true, 2, false},
    /* JC8012P4A1 (10.1", panel 800x1280 portrait used as 1280x800). UNVERIFIED on hardware:
     * rotation 270, USB roots (copied from JC1060: controller HS root 0, storage FS root 1
     * behind a self-powered hub), scanout 2 bytes per pixel and the waveform flags are
     * assumptions until the first bring-up. No Ethernet; Wi-Fi on the C6, default ON,
     * AP+STA, WPA2 (operator decision 2026-10-13). */
    {BOARD_JC8012P4A1, "JC8012P4A1", "main-deck-jc8012p4a1", 1280, 800, 800, 1280,
     270, 1, 0, 0, false, true, false, false, 2, false, false, false, 0, "Pajoniiir-JC8012", false, false, false, true, false, 1, true},
};
const board_capabilities_t *board_capabilities_for(board_id_t id)
{
    return id == BOARD_JC4880 || id == BOARD_JC1060 || id == BOARD_M3 || id == BOARD_JC8012P4A1 ? &boards[id] : 0;
}
const board_capabilities_t *board_capabilities_get(void)
{
#if defined(CONFIG_PAJONIIIR_BOARD_JC1060) && defined(CONFIG_PAJONIIIR_BOARD_M3)
#error "Exactly one P4 board must be selected"
#elif defined(CONFIG_PAJONIIIR_BOARD_JC8012P4A1) && \
    (defined(CONFIG_PAJONIIIR_BOARD_JC1060) || defined(CONFIG_PAJONIIIR_BOARD_M3))
#error "Exactly one P4 board must be selected"
#elif defined(CONFIG_PAJONIIIR_BOARD_JC1060) || defined(UI_SIM_BOARD_JC1060)
    return &boards[BOARD_JC1060];
#elif defined(CONFIG_PAJONIIIR_BOARD_M3)
    return &boards[BOARD_M3];
#elif defined(CONFIG_PAJONIIIR_BOARD_JC8012P4A1)
    return &boards[BOARD_JC8012P4A1];
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
    case BOARD_JC8012P4A1: return "jc8012p4a1";
    default: return "unknown";
    }
}
