#include "cdj_link_client.h"

#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "cdj_link_cli";
static cdj_link_peer_t s_peer;
static SemaphoreHandle_t s_peer_lock;
static TaskHandle_t s_discovery_task;

static esp_err_t ensure_lock(void)
{
    if (!s_peer_lock) {
        s_peer_lock = xSemaphoreCreateMutex();
        if (!s_peer_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static void copy_fixed_str(char *dst, size_t dst_len, const char *src, size_t src_len)
{
    if (!dst || dst_len == 0) {
        return;
    }
    size_t n = 0;
    while (n < src_len && src[n] != '\0') {
        n++;
    }
    if (n >= dst_len) {
        n = dst_len - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void update_peer(const cdj_link_discovery_packet_t *packet, const char *host)
{
    if (ensure_lock() != ESP_OK) {
        return;
    }
    xSemaphoreTake(s_peer_lock, portMAX_DELAY);
    memset(&s_peer, 0, sizeof(s_peer));
    s_peer.valid = true;
    copy_fixed_str(s_peer.peer_id, sizeof(s_peer.peer_id), packet->peer_id, sizeof(packet->peer_id));
    copy_fixed_str(s_peer.name, sizeof(s_peer.name), packet->name, sizeof(packet->name));
    snprintf(s_peer.host, sizeof(s_peer.host), "%s", host);
    s_peer.port = packet->port;
    s_peer.track_count = packet->track_count;
    s_peer.last_seen_us = esp_timer_get_time();
    xSemaphoreGive(s_peer_lock);
}

static void discovery_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGW(TAG, "discovery socket failed");
        s_discovery_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(CDJ_LINK_DISCOVERY_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        ESP_LOGW(TAG, "discovery bind failed: %s", strerror(errno));
        close(sock);
        s_discovery_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        cdj_link_discovery_packet_t packet;
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        int n = recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr *)&src, &src_len);
        if (n != (int)sizeof(packet)) {
            continue;
        }
        if (cdj_link_discovery_validate(&packet) != CDJ_LINK_OK) {
            continue;
        }
        char host[16];
        inet_ntoa_r(src.sin_addr, host, sizeof(host));
        update_peer(&packet, host);
    }
}

esp_err_t cdj_link_client_start(void)
{
    ESP_RETURN_ON_ERROR(ensure_lock(), TAG, "peer lock");
    if (!s_discovery_task) {
        if (xTaskCreate(discovery_task, "cdj_disc", 4096, NULL, 3, &s_discovery_task) != pdPASS) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

bool cdj_link_client_get_peer(cdj_link_peer_t *out_peer)
{
    if (!out_peer || ensure_lock() != ESP_OK) {
        return false;
    }
    xSemaphoreTake(s_peer_lock, portMAX_DELAY);
    *out_peer = s_peer;
    bool valid = s_peer.valid && (esp_timer_get_time() - s_peer.last_seen_us) < 5000000LL;
    xSemaphoreGive(s_peer_lock);
    return valid;
}

static esp_err_t build_url(char *url, size_t url_len, const char *suffix)
{
    cdj_link_peer_t peer;
    if (!cdj_link_client_get_peer(&peer)) {
        return ESP_ERR_NOT_FOUND;
    }
    snprintf(url, url_len, "http://%s:%u%s", peer.host, (unsigned)peer.port, suffix);
    return ESP_OK;
}

static esp_err_t http_open_read(const char *url, esp_http_client_handle_t *out_client, int *out_len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 10000,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t rc = esp_http_client_open(client, 0);
    if (rc != ESP_OK) {
        esp_http_client_cleanup(client);
        return rc;
    }
    int len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status == 503) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_STATE;
    }
    if (status != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FOUND;
    }
    *out_client = client;
    *out_len = len;
    return ESP_OK;
}

esp_err_t cdj_link_client_fetch_library(uint8_t **out_blob, size_t *out_len)
{
    if (!out_blob || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_blob = NULL;
    *out_len = 0;

    char url[128];
    ESP_RETURN_ON_ERROR(build_url(url, sizeof(url), "/v1/library.bin"), TAG, "library url");

    esp_http_client_handle_t client;
    int len = 0;
    ESP_RETURN_ON_ERROR(http_open_read(url, &client, &len), TAG, "open library");
    if (len <= 0 || len > 512 * 1024) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *buf = malloc((size_t)len);
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int off = 0;
    while (off < len) {
        int n = esp_http_client_read(client, (char *)buf + off, len - off);
        if (n <= 0) {
            free(buf);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        off += n;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    cdj_link_library_view_t view;
    if (cdj_link_library_decode(buf, (size_t)len, &view) != CDJ_LINK_OK) {
        free(buf);
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out_blob = buf;
    *out_len = (size_t)len;
    ESP_LOGI(TAG, "remote library fetched: %u tracks", (unsigned)view.count);
    return ESP_OK;
}

esp_err_t cdj_link_client_fetch_manifest(uint32_t track_key, cdj_link_track_manifest_t *out_manifest)
{
    if (!out_manifest || track_key == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char suffix[80];
    snprintf(suffix, sizeof(suffix), "/v1/track/%lu/manifest.bin", (unsigned long)track_key);
    char url[128];
    ESP_RETURN_ON_ERROR(build_url(url, sizeof(url), suffix), TAG, "manifest url");

    esp_http_client_handle_t client;
    int len = 0;
    ESP_RETURN_ON_ERROR(http_open_read(url, &client, &len), TAG, "open manifest");
    uint8_t buf[64];
    int off = 0;
    while (off < len && off < (int)sizeof(buf)) {
        int n = esp_http_client_read(client, (char *)buf + off, len - off);
        if (n <= 0) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        off += n;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return cdj_link_manifest_decode(buf, (size_t)off, out_manifest) == CDJ_LINK_OK
               ? ESP_OK
               : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t cdj_link_client_download_asset(uint32_t track_key,
                                         const char *asset_name,
                                         const char *dest_path,
                                         uint32_t expected_size,
                                         uint32_t *out_bytes)
{
    if (track_key == 0 || !asset_name || !dest_path || expected_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_bytes) {
        *out_bytes = 0;
    }

    char suffix[96];
    snprintf(suffix, sizeof(suffix), "/v1/track/%lu/%s", (unsigned long)track_key, asset_name);
    char url[160];
    ESP_RETURN_ON_ERROR(build_url(url, sizeof(url), suffix), TAG, "asset url");

    esp_http_client_handle_t client;
    int len = 0;
    ESP_RETURN_ON_ERROR(http_open_read(url, &client, &len), TAG, "open asset");
    if (len > 0 && (uint32_t)len != expected_size) {
        ESP_LOGW(TAG, "%s length header %d != expected %lu", asset_name, len, (unsigned long)expected_size);
    }

    FILE *fp = fopen(dest_path, "wb");
    if (!fp) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    uint8_t *buf = malloc(16 * 1024);
    if (!buf) {
        fclose(fp);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    uint32_t total = 0;
    while (total < expected_size) {
        int want = (int)(expected_size - total);
        if (want > 16 * 1024) {
            want = 16 * 1024;
        }
        int n = esp_http_client_read(client, (char *)buf, want);
        if (n <= 0) {
            free(buf);
            fclose(fp);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (fwrite(buf, 1, (size_t)n, fp) != (size_t)n) {
            free(buf);
            fclose(fp);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        total += (uint32_t)n;
    }

    free(buf);
    fclose(fp);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (out_bytes) {
        *out_bytes = total;
    }
    return total == expected_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}
