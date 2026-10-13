/* JC8012P4A1 entrypoint: common P4 startup/core. No Ethernet on this board; the network is
 * Wi-Fi through the ESP32-C6 (ESP-Hosted), started by the shared wifi_link component from
 * the persisted setting (default ON, AP+STA, WPA2). SCAFFOLD: not compiled, not run. */
#include "p4_app.h"
#include "board_adapter.h"
#include "esp_log.h"
void app_main(void)
{
    /* NS4150 speaker amplifier enable (GPIO20) must be low before network/NVS work. */
    ESP_ERROR_CHECK(bsp_audio_force_safe_boot_state());
    pajoniiir_common_app_main();
}
