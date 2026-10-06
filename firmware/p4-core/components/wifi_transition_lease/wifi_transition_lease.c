#include "wifi_transition_lease.h"

#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_lease_mux = portMUX_INITIALIZER_UNLOCKED;
static wifi_transition_owner_t s_owner = WIFI_TRANSITION_OWNER_NONE;

esp_err_t wifi_transition_lease_acquire(wifi_transition_owner_t owner)
{
    if (owner == WIFI_TRANSITION_OWNER_NONE) return ESP_ERR_INVALID_ARG;

    esp_err_t rc = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_lease_mux);
    if (s_owner == WIFI_TRANSITION_OWNER_NONE) {
        s_owner = owner;
        rc = ESP_OK;
    }
    portEXIT_CRITICAL(&s_lease_mux);
    return rc;
}

void wifi_transition_lease_release(wifi_transition_owner_t owner)
{
    portENTER_CRITICAL(&s_lease_mux);
    if (s_owner == owner) s_owner = WIFI_TRANSITION_OWNER_NONE;
    portEXIT_CRITICAL(&s_lease_mux);
}

wifi_transition_owner_t wifi_transition_lease_owner(void)
{
    wifi_transition_owner_t owner;
    portENTER_CRITICAL(&s_lease_mux);
    owner = s_owner;
    portEXIT_CRITICAL(&s_lease_mux);
    return owner;
}
