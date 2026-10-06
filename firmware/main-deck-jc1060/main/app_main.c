/* JC1060 entrypoint: common P4 startup/core, board-local Ethernet bring-up. */
#include "p4_app.h"
#include "board_adapter.h"
#include "esp_log.h"
#include "board_ethernet.h"
#include "dj_link_service.h"
void app_main(void)
{
    /* PA must be low before network/NVS work, as on JC4880. */
    ESP_ERROR_CHECK(bsp_audio_force_safe_boot_state());
    esp_err_t rc = board_ethernet_start();
    if (rc != ESP_OK) ESP_LOGE("jc1060", "Ethernet unavailable: %s", esp_err_to_name(rc));
    pajoniiir_common_app_main();
    rc = dj_link_service_init();
    if (rc != ESP_OK) ESP_LOGE("jc1060", "DJ Link service unavailable: %s", esp_err_to_name(rc));
}
