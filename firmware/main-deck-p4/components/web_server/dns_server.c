#include "web_server.h"
#include "dns_reply.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

#define DNS_PORT 53

static const char *TAG = "dns_server";
static int s_dns_socket = -1;
static TaskHandle_t s_dns_task_handle = NULL;

static void dns_server_task(void *pvParameters)
{
    uint8_t rx_buffer[512];
    uint8_t tx_buffer[512];
    const uint8_t captive_ip[4] = {192, 168, 4, 1};
    struct sockaddr_in source_addr;

    ESP_LOGI(TAG, "DNS server započinje slušanje na portu %d...", DNS_PORT);

    while (1) {
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(s_dns_socket, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            if (errno == EBADF) {
                // Socket je zatvoren, izađi mirno
                break;
            }
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        size_t tx_len = dns_build_captive_reply(rx_buffer,
                                                (size_t)len,
                                                tx_buffer,
                                                sizeof(tx_buffer),
                                                captive_ip);
        if (tx_len == 0) {
            continue;
        }

        sendto(s_dns_socket, tx_buffer, tx_len, 0, (struct sockaddr *)&source_addr, socklen);
    }

    ESP_LOGI(TAG, "DNS server task završen.");
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(void)
{
    if (s_dns_socket != -1) {
        return ESP_OK; // Već pokrenut
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DNS_PORT);

    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_socket < 0) {
        ESP_LOGE(TAG, "Ne mogu kreirati socket: errno %d", errno);
        return ESP_FAIL;
    }

    int err = bind(s_dns_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Ne mogu bindati socket: errno %d", errno);
        close(s_dns_socket);
        s_dns_socket = -1;
        return ESP_FAIL;
    }

    if (xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 3, &s_dns_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Ne mogu stvoriti DNS task");
        close(s_dns_socket);
        s_dns_socket = -1;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void dns_server_stop(void)
{
    if (s_dns_socket != -1) {
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    s_dns_task_handle = NULL;
}
