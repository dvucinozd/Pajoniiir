#include "s3_debug_ap_netif_stage.h"

esp_err_t s3_debug_ap_netif_ensure(void **published,
                                   const s3_debug_ap_netif_ops_t *ops)
{
    if (!published || !ops || !ops->create || !ops->stop_dhcp ||
        !ops->set_ip || !ops->start_dhcp || !ops->destroy) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*published) return ESP_OK;

    void *candidate = ops->create(ops->ctx);
    if (!candidate) return ESP_ERR_NO_MEM;

    esp_err_t rc = ops->stop_dhcp(candidate, ops->ctx);
    if (rc == ESP_OK) rc = ops->set_ip(candidate, ops->ctx);
    if (rc == ESP_OK) rc = ops->start_dhcp(candidate, ops->ctx);
    if (rc != ESP_OK) {
        ops->destroy(candidate, ops->ctx);
        return rc;
    }

    *published = candidate;
    return ESP_OK;
}
