#include "p4_app.h"
#include "sdkconfig.h"
#if CONFIG_PAJONIIIR_DJ_LINK_SERVICE
#include "dj_link_service.h"
#include "esp_netif.h"
#include "esp_log.h"
/* Engineering network provider: never use the SoftAP/default route. */
static esp_netif_t *board_link_netif(void)
{
    return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
}
#endif
void app_main(void)
{
    pajoniiir_common_app_main();
#if CONFIG_PAJONIIIR_DJ_LINK_SERVICE
    esp_err_t rc = dj_link_service_init(board_link_netif);
    if (rc != ESP_OK) ESP_LOGE("board_link", "DJ Link unavailable: %s", esp_err_to_name(rc));
#endif
}
