#include "wifi_debug_log.h"

#include "sdkconfig.h"

#if CONFIG_WIFI_DEBUG_LOG_ENABLED

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define WIFI_DEBUG_LOG_QUEUE_LEN 32
#define WIFI_DEBUG_LOG_LINE_MAX 256
#define WIFI_DEBUG_LOG_TASK_STACK 4096
#define WIFI_DEBUG_LOG_CONNECT_TASK_STACK 4096

typedef struct {
    char text[WIFI_DEBUG_LOG_LINE_MAX];
} wifi_debug_log_line_t;

static const char *TAG = "wifi_debug_log";

static QueueHandle_t s_queue;
static int s_sock = -1;
static struct sockaddr_in s_dest;
static atomic_bool s_udp_ready;
static atomic_bool s_hook_ready;
static vprintf_like_t s_prev_vprintf;

static esp_err_t wifi_debug_log_start_wifi(void);

static esp_err_t wifi_debug_log_open_udp_socket(void)
{
    if (s_sock >= 0) {
        return ESP_OK;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    ESP_RETURN_ON_FALSE(sock >= 0, ESP_FAIL, TAG, "create UDP socket");
    s_sock = sock;
    return ESP_OK;
}

static void wifi_debug_log_task(void *arg)
{
    (void)arg;
    wifi_debug_log_line_t line;
    while (1) {
        if (xQueueReceive(s_queue, &line, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!atomic_load(&s_udp_ready) || s_sock < 0) {
            continue;
        }
        (void)sendto(s_sock, line.text, strlen(line.text), 0,
                     (const struct sockaddr *)&s_dest, sizeof(s_dest));
    }
}

static void wifi_debug_log_connect_task(void *arg)
{
    (void)arg;
    esp_err_t rc = wifi_debug_log_start_wifi();
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi debug log start failed: %s", esp_err_to_name(rc));
    }
    vTaskDelete(NULL);
}

static int wifi_debug_log_vprintf(const char *fmt, va_list args)
{
    va_list uart_args;
    va_copy(uart_args, args);
    int ret = s_prev_vprintf ? s_prev_vprintf(fmt, uart_args) : vprintf(fmt, uart_args);
    va_end(uart_args);

    if (atomic_load(&s_hook_ready) && s_queue) {
        wifi_debug_log_line_t line = { 0 };
        va_list format_args;
        va_copy(format_args, args);
        int len = vsnprintf(line.text, sizeof(line.text), fmt, format_args);
        va_end(format_args);
        if (len > 0) {
            (void)xQueueSend(s_queue, &line, 0);
        }
    }
    return ret;
}

static void wifi_debug_log_event_handler(void *arg,
                                         esp_event_base_t event_base,
                                         int32_t event_id,
                                         void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        (void)esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&s_udp_ready, false);
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
        (void)esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&s_udp_ready, true);
        ESP_LOGI(TAG, "Wi-Fi connected; UDP logs active to %s:%d",
                 CONFIG_WIFI_DEBUG_LOG_HOST, CONFIG_WIFI_DEBUG_LOG_PORT);
    }
}

static esp_err_t wifi_debug_log_nvs_init(void)
{
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        rc = nvs_flash_init();
    }
    return rc;
}

static esp_err_t wifi_debug_log_start_wifi(void)
{
    ESP_RETURN_ON_ERROR(wifi_debug_log_nvs_init(), TAG, "init NVS");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "init netif");
    ESP_RETURN_ON_ERROR(wifi_debug_log_open_udp_socket(), TAG, "open UDP socket");
    esp_err_t rc = esp_event_loop_create_default();
    if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(rc, TAG, "create event loop");
    }
    (void)esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "init Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   wifi_debug_log_event_handler, NULL),
                        TAG, "register Wi-Fi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   wifi_debug_log_event_handler, NULL),
                        TAG, "register IP event handler");

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_WIFI_DEBUG_LOG_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_WIFI_DEBUG_LOG_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set Wi-Fi STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set Wi-Fi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi");
    return ESP_OK;
}

esp_err_t wifi_debug_log_init(void)
{
    if (s_queue) {
        return ESP_OK;
    }
    if (CONFIG_WIFI_DEBUG_LOG_SSID[0] == '\0' || CONFIG_WIFI_DEBUG_LOG_HOST[0] == '\0') {
        return ESP_OK;
    }

    s_queue = xQueueCreate(WIFI_DEBUG_LOG_QUEUE_LEN, sizeof(wifi_debug_log_line_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "create queue");

    memset(&s_dest, 0, sizeof(s_dest));
    s_dest.sin_family = AF_INET;
    s_dest.sin_port = htons(CONFIG_WIFI_DEBUG_LOG_PORT);
    ESP_RETURN_ON_FALSE(inet_pton(AF_INET, CONFIG_WIFI_DEBUG_LOG_HOST, &s_dest.sin_addr) == 1,
                        ESP_ERR_INVALID_ARG, TAG, "parse UDP host");

    ESP_RETURN_ON_FALSE(xTaskCreate(wifi_debug_log_task, "wifi_log",
                                    WIFI_DEBUG_LOG_TASK_STACK, NULL, 2, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "create Wi-Fi log task");

    s_prev_vprintf = esp_log_set_vprintf(wifi_debug_log_vprintf);
    atomic_store(&s_hook_ready, true);

    ESP_RETURN_ON_FALSE(xTaskCreate(wifi_debug_log_connect_task, "wifi_log_conn",
                                    WIFI_DEBUG_LOG_CONNECT_TASK_STACK, NULL, 2, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "create Wi-Fi connect task");
    ESP_LOGI(TAG, "UDP debug log enabled for %s:%d", CONFIG_WIFI_DEBUG_LOG_HOST,
             CONFIG_WIFI_DEBUG_LOG_PORT);
    return ESP_OK;
}

#else

esp_err_t wifi_debug_log_init(void)
{
    return ESP_OK;
}

#endif
