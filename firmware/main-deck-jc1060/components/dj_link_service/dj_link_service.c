#include "dj_link_service.h"
#include "dj_link_udp.h"
#include "board_ethernet.h"
#include "app_settings.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "dj_link";
static TaskHandle_t s_task;
static SemaphoreHandle_t s_snapshot_lock;
static dj_link_discovery_t *s_model, *s_snapshot;
static portMUX_TYPE s_summary_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    dj_link_phase_t phase;
    uint8_t numbers[2], peers;
    bool ready, socket_error;
} s_summary;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time()/1000); }
static void publish(bool socket_error)
{
    uint8_t peers = 0;
    for (unsigned i = 0; i < DJ_LINK_PEERS; ++i) peers += s_model->peers[i].present;
    portENTER_CRITICAL(&s_summary_lock);
    s_summary.phase = s_model->phase; s_summary.peers = peers;
    s_summary.numbers[0] = dj_link_discovery_number(s_model, 0);
    s_summary.numbers[1] = dj_link_discovery_number(s_model, 1);
    s_summary.ready = true; s_summary.socket_error = socket_error;
    portEXIT_CRITICAL(&s_summary_lock);
    if (xSemaphoreTake(s_snapshot_lock, 0) == pdTRUE) {
        *s_snapshot = *s_model;
        xSemaphoreGive(s_snapshot_lock);
    }
}
bool dj_link_service_snapshot(dj_link_discovery_t *out)
{
    if (!out || !s_snapshot_lock || xSemaphoreTake(s_snapshot_lock, 0) != pdTRUE) return false;
    *out = *s_snapshot; xSemaphoreGive(s_snapshot_lock); return true;
}
void dj_link_service_format_status(char *out, size_t cap)
{
    if (!out || !cap) return;
    portENTER_CRITICAL(&s_summary_lock);
    const dj_link_phase_t phase = s_summary.phase;
    const uint8_t a = s_summary.numbers[0], b = s_summary.numbers[1], peers = s_summary.peers;
    const bool ready = s_summary.ready, error = s_summary.socket_error;
    portEXIT_CRITICAL(&s_summary_lock);
    if (!app_settings_get().dj_link) snprintf(out, cap, "DJ LINK: OFF");
    else if (!ready || error) snprintf(out, cap, "DJ LINK: SERVICE ERROR");
    else if (phase == DJ_LINK_WAIT_IP) snprintf(out, cap, "DJ LINK: WAIT ETHERNET IP");
    else if (phase == DJ_LINK_OBSERVER) snprintf(out, cap, "DJ LINK: OBSERVER / NO PAIR");
    else if (phase == DJ_LINK_ACTIVE) snprintf(out, cap, "DJ LINK: #%u/#%u (%u peers)", a, b, peers);
    else snprintf(out, cap, "DJ LINK: CLAIMING");
}
static void worker(void *unused)
{
    (void)unused;
    const uint16_t ports[3] = {50000,50001,50002};
    dj_link_udp_t transport = {.fd={-1,-1,-1}};
    uint32_t opened_ip = 0, opened_mask = 0, retry_at = 0;
    bool socket_error = false;
    dj_link_datagram_t packet;
    uint8_t tx[DJLINK_MAX_PACKET];
    for (;;) {
        esp_netif_t *netif = board_ethernet_netif();
        uint32_t now = now_ms();
        uint8_t mac[6] = {0}; esp_netif_ip_info_t info = {0};
        bool enabled = app_settings_get().dj_link != 0;
        bool available = enabled && netif && esp_netif_is_netif_up(netif) &&
            esp_netif_get_mac(netif, mac) == ESP_OK &&
            esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr && info.netmask.addr;
        uint32_t ip = available ? ntohl(info.ip.addr) : 0;
        uint32_t mask = available ? ntohl(info.netmask.addr) : 0;
        if (!available || (opened_ip && (opened_ip != ip || opened_mask != mask))) {
            dj_link_udp_close(&transport); opened_ip = opened_mask = 0;
            dj_link_discovery_configure(s_model, enabled, 0, mac, now);
            socket_error = false; retry_at = now;
        }
        if (available && !opened_ip && (int32_t)(now-retry_at) >= 0) {
            char name[IFNAMSIZ] = {0};
            /* This exact Ethernet netif owns every inbound/outbound socket.
             * No default route, AP, Wi-Fi netif or INADDR_ANY fallback binding. */
            if (esp_netif_get_netif_impl_name(netif, name) == ESP_OK &&
                dj_link_udp_open(&transport, name, ip, mask, ports)) {
                opened_ip = ip; opened_mask = mask; socket_error = false;
                dj_link_discovery_configure(s_model, true, ip, mac, now);
                ESP_LOGI(TAG, "Ethernet transport %s opened; claiming two players", name);
            } else {
                socket_error = true; retry_at = now + 2000;
                dj_link_discovery_configure(s_model, true, 0, mac, now);
                ESP_LOGW(TAG, "Ethernet transport refused: errno=%d", errno);
            }
        }
        if (opened_ip) {
            for (unsigned i = 0; i < 12; ++i) {
                int received = dj_link_udp_receive(&transport, &packet, i == 0 ? 40 : 0);
                if (received < 0) { socket_error = true; break; }
                if (!received) break;
                (void)dj_link_discovery_ingest(s_model, packet.port, packet.bytes,
                    packet.len, packet.source_ip, now_ms());
            }
            now = now_ms();
            for (unsigned i = 0; i < 2 && !socket_error; ++i) {
                unsigned deck;
                int len = dj_link_discovery_poll(s_model, now, tx, sizeof(tx), &deck);
                if (len > 0 && !dj_link_udp_broadcast(&transport, tx, (size_t)len)) socket_error = true;
            }
            if (socket_error) {
                dj_link_udp_close(&transport); opened_ip = opened_mask = 0; retry_at = now+2000;
                dj_link_discovery_configure(s_model, enabled, 0, mac, now);
            }
        }
        dj_link_discovery_tick(s_model, now_ms()); publish(socket_error);
        vTaskDelay(pdMS_TO_TICKS(opened_ip ? 1 : 40));
    }
}
esp_err_t dj_link_service_init(void)
{
    if (s_task) return ESP_OK;
    if (!board_ethernet_netif()) return ESP_ERR_INVALID_STATE;
    s_model = heap_caps_calloc(1, sizeof(*s_model), MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s_snapshot = heap_caps_calloc(1, sizeof(*s_snapshot), MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s_snapshot_lock = xSemaphoreCreateMutex();
    if (!s_model || !s_snapshot || !s_snapshot_lock) goto fail;
    dj_link_discovery_init(s_model); *s_snapshot = *s_model;
    if (xTaskCreatePinnedToCoreWithCaps(worker, "dj_link_eth", 6144, NULL, 2, &s_task, 0,
        MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) != pdPASS) goto fail;
    return ESP_OK;
fail:
    if (s_snapshot_lock) vSemaphoreDelete(s_snapshot_lock);
    heap_caps_free(s_model); heap_caps_free(s_snapshot);
    s_snapshot_lock = NULL; s_model = s_snapshot = NULL;
    return ESP_ERR_NO_MEM;
}
