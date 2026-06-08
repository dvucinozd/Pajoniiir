#include "cdj_link_server.h"

#include "cdj_link_protocol.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "library.h"
#include "media_io_gate.h"
#include "wifi_link.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "cdj_link_srv";
static const size_t HTTP_STREAM_CHUNK_BYTES = 32u * 1024u;
#define HTTP_STREAM_QUEUE_DEPTH 4u
static httpd_handle_t s_httpd;
static uint8_t *s_library_blob;
static size_t s_library_blob_len;
static uint32_t s_track_count;
static TaskHandle_t s_beacon_task;

static void beacon_task(void *arg);

static void free_snapshot(void)
{
    free(s_library_blob);
    s_library_blob = NULL;
    s_library_blob_len = 0;
    s_track_count = 0;
}

static void copy_field(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) return;
    dst[0] = '\0';
    if (!src) return;
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static bool find_track(uint32_t key, library_track_t *out)
{
    if (!out) return false;
    int n = library_count();
    for (int i = 0; i < n; i++) {
        library_track_t track;
        if (library_get(i, &track) != ESP_OK) continue;
        if (cdj_link_track_key(track.track_id, track.path) == key) {
            *out = track;
            return true;
        }
    }
    return false;
}

static bool audio_path_for_track(const library_track_t *track, char *out, size_t out_len)
{
    if (!track || !out || out_len == 0 || track->path[0] == '\0') {
        return false;
    }
    snprintf(out, out_len, "/usb%s", track->path);
    return true;
}

static bool dat_path_for_track(const library_track_t *track, char *out, size_t out_len)
{
    if (!track || !out || out_len == 0 || track->anlz_path[0] == '\0') {
        return false;
    }
    if (track->anlz_path[0] == '/') {
        snprintf(out, out_len, "/usb%s", track->anlz_path);
    } else {
        snprintf(out, out_len, "%s", track->anlz_path);
    }
    return true;
}

static bool ext_path_for_dat(const char *dat_path, char *out, size_t out_len)
{
    if (!dat_path || !out || out_len == 0) {
        return false;
    }
    copy_field(out, out_len, dat_path);
    char *dot = strrchr(out, '.');
    if (!dot) {
        return false;
    }
    snprintf(dot, out_len - (size_t)(dot - out), ".EXT");
    return true;
}

static bool file_size_u32(const char *path, uint32_t *out_size)
{
    struct stat st;
    if (!path || !out_size || stat(path, &st) != 0 || st.st_size < 0 || st.st_size > UINT32_MAX) {
        return false;
    }
    *out_size = (uint32_t)st.st_size;
    return true;
}

static void send_busy(httpd_req_t *req)
{
    httpd_resp_set_status(req, "503 BUSY");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "BUSY");
}

static esp_err_t send_fixed_headers(httpd_req_t *req, const char *content_type, uint32_t content_len)
{
    char hdr[160];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %u\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     content_type, (unsigned)content_len);
    if (n <= 0 || n >= (int)sizeof(hdr)) {
        return ESP_FAIL;
    }
    return httpd_send(req, hdr, (size_t)n) == n ? ESP_OK : ESP_FAIL;
}

typedef struct {
    uint8_t *data;
    size_t len;
    bool eof;
    esp_err_t rc;
} stream_chunk_t;

typedef struct {
    char path[LIBRARY_PATH_MAX + 8];
    FILE *fp;
    QueueHandle_t free_q;
    QueueHandle_t ready_q;
    SemaphoreHandle_t started;
    SemaphoreHandle_t done;
    volatile bool cancel;
    uint32_t expected_size;
    int64_t read_us;
    size_t read_total;
    esp_err_t rc;
} stream_reader_ctx_t;

static void stream_reader_task(void *arg)
{
    stream_reader_ctx_t *ctx = (stream_reader_ctx_t *)arg;
    ctx->rc = ESP_OK;

    if (!media_io_gate_try_begin(250)) {
        ctx->rc = ESP_ERR_TIMEOUT;
        xSemaphoreGive(ctx->started);
        xSemaphoreGive(ctx->done);
        vTaskDelete(NULL);
        return;
    }
    if (!file_size_u32(ctx->path, &ctx->expected_size)) {
        ctx->rc = ESP_ERR_NOT_FOUND;
        media_io_gate_end();
        xSemaphoreGive(ctx->started);
        xSemaphoreGive(ctx->done);
        vTaskDelete(NULL);
        return;
    }
    ctx->fp = fopen(ctx->path, "rb");
    if (!ctx->fp) {
        ctx->rc = ESP_ERR_NOT_FOUND;
        ESP_LOGW(TAG, "open %s: %s", ctx->path, strerror(errno));
        media_io_gate_end();
        xSemaphoreGive(ctx->started);
        xSemaphoreGive(ctx->done);
        vTaskDelete(NULL);
        return;
    }
    xSemaphoreGive(ctx->started);

    while (!ctx->cancel) {
        stream_chunk_t *chunk = NULL;
        if (xQueueReceive(ctx->free_q, &chunk, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        if (!chunk || ctx->cancel) {
            break;
        }

        int64_t t0 = esp_timer_get_time();
        size_t n = fread(chunk->data, 1, HTTP_STREAM_CHUNK_BYTES, ctx->fp);
        ctx->read_us += esp_timer_get_time() - t0;
        ctx->read_total += n;

        chunk->len = n;
        chunk->eof = feof(ctx->fp);
        chunk->rc = ESP_OK;
        if (n == 0 && ferror(ctx->fp)) {
            chunk->eof = true;
            chunk->rc = ESP_FAIL;
            ctx->rc = ESP_FAIL;
        }

        if (xQueueSend(ctx->ready_q, &chunk, portMAX_DELAY) != pdTRUE) {
            ctx->rc = ESP_FAIL;
            break;
        }
        if (chunk->eof || chunk->rc != ESP_OK) {
            break;
        }
    }

    fclose(ctx->fp);
    media_io_gate_end();
    xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

static esp_err_t send_file(httpd_req_t *req, const char *path, const char *content_type)
{
    stream_chunk_t chunks[HTTP_STREAM_QUEUE_DEPTH] = {0};
    QueueHandle_t free_q = xQueueCreate(HTTP_STREAM_QUEUE_DEPTH, sizeof(stream_chunk_t *));
    QueueHandle_t ready_q = xQueueCreate(HTTP_STREAM_QUEUE_DEPTH + 1, sizeof(stream_chunk_t *));
    SemaphoreHandle_t started = xSemaphoreCreateBinary();
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    bool buffers_ok = free_q && ready_q && started && done;
    for (size_t i = 0; buffers_ok && i < HTTP_STREAM_QUEUE_DEPTH; i++) {
        chunks[i].data = heap_caps_malloc(HTTP_STREAM_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!chunks[i].data) {
            chunks[i].data = malloc(HTTP_STREAM_CHUNK_BYTES);
        }
        if (!chunks[i].data) {
            buffers_ok = false;
            break;
        }
        stream_chunk_t *chunk = &chunks[i];
        if (xQueueSend(free_q, &chunk, 0) != pdTRUE) {
            buffers_ok = false;
            break;
        }
    }
    if (!buffers_ok) {
        for (size_t i = 0; i < HTTP_STREAM_QUEUE_DEPTH; i++) {
            free(chunks[i].data);
        }
        if (free_q) vQueueDelete(free_q);
        if (ready_q) vQueueDelete(ready_q);
        if (started) vSemaphoreDelete(started);
        if (done) vSemaphoreDelete(done);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NO MEM");
        return ESP_OK;
    }

    stream_reader_ctx_t reader = {
        .free_q = free_q,
        .ready_q = ready_q,
        .started = started,
        .done = done,
        .rc = ESP_OK,
    };
    copy_field(reader.path, sizeof(reader.path), path);

    esp_err_t rc = ESP_OK;
    size_t total = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t send_us = 0;
    int64_t t0 = 0;

    TaskHandle_t reader_task = NULL;
    if (xTaskCreate(stream_reader_task, "cdj_stream_rd", 8192, &reader, 4, &reader_task) != pdPASS) {
        rc = ESP_ERR_NO_MEM;
    }
    if (rc == ESP_OK) {
        if (xSemaphoreTake(started, pdMS_TO_TICKS(5000)) == pdTRUE) {
            rc = reader.rc;
        } else {
            reader.cancel = true;
            rc = ESP_ERR_TIMEOUT;
        }
    }
    if (rc == ESP_ERR_TIMEOUT) {
        send_busy(req);
    } else if (rc == ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "NOT FOUND");
    } else if (rc != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "STREAM START FAILED");
    } else {
        t0 = esp_timer_get_time();
        if (send_fixed_headers(req, content_type, reader.expected_size) != ESP_OK) {
            rc = ESP_FAIL;
        }
        send_us += esp_timer_get_time() - t0;
    }

    while (rc == ESP_OK) {
        stream_chunk_t *chunk = NULL;
        if (xQueueReceive(ready_q, &chunk, pdMS_TO_TICKS(5000)) != pdTRUE || !chunk) {
            rc = ESP_ERR_TIMEOUT;
            reader.cancel = true;
            break;
        }
        if (chunk->rc != ESP_OK) {
            rc = chunk->rc;
        }
        if (rc == ESP_OK && chunk->len > 0) {
            t0 = esp_timer_get_time();
            if (httpd_send(req, (const char *)chunk->data, chunk->len) != (int)chunk->len) {
                rc = ESP_FAIL;
            }
            send_us += esp_timer_get_time() - t0;
            total += chunk->len;
        }
        bool eof = chunk->eof;
        if (!eof) {
            xQueueSend(free_q, &chunk, portMAX_DELAY);
        }
        if (eof || rc != ESP_OK) {
            reader.cancel = true;
            break;
        }
    }
    if (rc != ESP_OK) {
        reader.cancel = true;
    }
    if (reader_task) {
        xSemaphoreTake(done, pdMS_TO_TICKS(5000));
    }
    if (rc == ESP_OK && reader.rc != ESP_OK) {
        rc = reader.rc;
    }
    ESP_LOGI(TAG, "file stream %s: %u/%u bytes total=%lld us read=%lld us send=%lld us rc=%s",
             path, (unsigned)total, (unsigned)reader.expected_size,
             (long long)(esp_timer_get_time() - start_us),
             (long long)reader.read_us, (long long)send_us, esp_err_to_name(rc));
    for (size_t i = 0; i < HTTP_STREAM_QUEUE_DEPTH; i++) {
        free(chunks[i].data);
    }
    vQueueDelete(free_q);
    vQueueDelete(ready_q);
    vSemaphoreDelete(started);
    vSemaphoreDelete(done);
    return ESP_OK;
}

static uint32_t parse_track_key(const char *uri, const char **suffix)
{
    const char *prefix = "/v1/track/";
    size_t prefix_len = strlen(prefix);
    if (strncmp(uri, prefix, prefix_len) != 0) {
        return 0;
    }
    char *end = NULL;
    unsigned long key = strtoul(uri + prefix_len, &end, 10);
    if (!end || *end != '/' || key == 0 || key > UINT32_MAX) {
        return 0;
    }
    if (suffix) {
        *suffix = end + 1;
    }
    return (uint32_t)key;
}

static esp_err_t hello_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "CDJLINK1\n");
    return ESP_OK;
}

static esp_err_t library_handler(httpd_req_t *req)
{
    if (!s_library_blob || s_library_blob_len == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "NO LIBRARY");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_send(req, (const char *)s_library_blob, s_library_blob_len);
    return ESP_OK;
}

static esp_err_t track_handler(httpd_req_t *req)
{
    const char *suffix = NULL;
    uint32_t key = parse_track_key(req->uri, &suffix);
    if (key == 0 || !suffix) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "BAD TRACK");
        return ESP_OK;
    }

    library_track_t track;
    if (!find_track(key, &track)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "TRACK NOT FOUND");
        return ESP_OK;
    }

    char audio_path[LIBRARY_PATH_MAX + 8];
    char dat_path[LIBRARY_PATH_MAX + 8];
    char ext_path[LIBRARY_PATH_MAX + 8];
    audio_path_for_track(&track, audio_path, sizeof(audio_path));
    dat_path_for_track(&track, dat_path, sizeof(dat_path));
    ext_path_for_dat(dat_path, ext_path, sizeof(ext_path));

    if (strcmp(suffix, "audio.mp3") == 0) {
        return send_file(req, audio_path, "audio/mpeg");
    }
    if (strcmp(suffix, "ANLZ0000.DAT") == 0) {
        return send_file(req, dat_path, "application/octet-stream");
    }
    if (strcmp(suffix, "ANLZ0000.EXT") == 0) {
        return send_file(req, ext_path, "application/octet-stream");
    }
    if (strcmp(suffix, "manifest.bin") == 0) {
        if (!media_io_gate_try_begin(250)) {
            send_busy(req);
            return ESP_OK;
        }
        cdj_link_track_manifest_t manifest = {
            .track_key = key,
        };
        bool ok = file_size_u32(audio_path, &manifest.audio_size) &&
                  file_size_u32(dat_path, &manifest.dat_size);
        if (file_size_u32(ext_path, &manifest.ext_size)) {
            manifest.has_ext = 1;
        }
        media_io_gate_end();
        if (!ok) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "ASSET NOT FOUND");
            return ESP_OK;
        }
        uint8_t buf[64];
        size_t written = 0;
        if (cdj_link_manifest_encode(buf, sizeof(buf), &manifest, &written) != CDJ_LINK_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "BAD MANIFEST");
            return ESP_OK;
        }
        httpd_resp_set_type(req, "application/octet-stream");
        httpd_resp_send(req, (const char *)buf, written);
        return ESP_OK;
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "NOT FOUND");
    return ESP_OK;
}

esp_err_t cdj_link_server_rebuild_library(void)
{
    int n = library_count();
    if (n < 0) {
        n = 0;
    }
    if (n > (int)CDJ_LINK_MAX_TRACKS) {
        n = (int)CDJ_LINK_MAX_TRACKS;
    }

    cdj_link_track_record_t *records = heap_caps_calloc((size_t)n, sizeof(*records),
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!records && n > 0) {
        records = calloc((size_t)n, sizeof(*records));
    }
    if (!records && n > 0) {
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < n; i++) {
        library_track_t track;
        if (library_get(i, &track) != ESP_OK) continue;
        records[i].track_key = cdj_link_track_key(track.track_id, track.path);
        records[i].rekordbox_track_id = track.track_id;
        records[i].bpm = track.bpm;
        records[i].duration_ms = track.duration_ms;
        copy_field(records[i].title, sizeof(records[i].title), track.title);
        copy_field(records[i].artist, sizeof(records[i].artist), track.artist);
        copy_field(records[i].album, sizeof(records[i].album), track.album);
    }

    size_t blob_len = 0;
    cdj_link_result_t prc = cdj_link_library_size((uint32_t)n, &blob_len);
    if (prc != CDJ_LINK_OK) {
        free(records);
        return ESP_FAIL;
    }
    uint8_t *blob = heap_caps_malloc(blob_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!blob) {
        blob = malloc(blob_len);
    }
    if (!blob) {
        free(records);
        return ESP_ERR_NO_MEM;
    }
    size_t written = 0;
    prc = cdj_link_library_encode(blob, blob_len, records, (uint32_t)n, &written);
    free(records);
    if (prc != CDJ_LINK_OK) {
        free(blob);
        return ESP_FAIL;
    }

    free_snapshot();
    s_library_blob = blob;
    s_library_blob_len = written;
    s_track_count = (uint32_t)n;
    ESP_LOGI(TAG, "library snapshot ready: %lu tracks, %u bytes",
             (unsigned long)s_track_count, (unsigned)s_library_blob_len);
    return ESP_OK;
}

void cdj_link_server_clear_library(void)
{
    free_snapshot();
    ESP_LOGI(TAG, "library snapshot cleared");
}

esp_err_t cdj_link_server_start(void)
{
    if (s_httpd) {
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CDJ_LINK_SERVER_PORT;
    config.ctrl_port = CDJ_LINK_SERVER_PORT + 1;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;

    esp_err_t rc = httpd_start(&s_httpd, &config);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(rc));
        return rc;
    }

    const httpd_uri_t hello_uri = {
        .uri = "/v1/hello.txt",
        .method = HTTP_GET,
        .handler = hello_handler,
    };
    const httpd_uri_t library_uri = {
        .uri = "/v1/library.bin",
        .method = HTTP_GET,
        .handler = library_handler,
    };
    const httpd_uri_t track_uri = {
        .uri = "/v1/track/*",
        .method = HTTP_GET,
        .handler = track_handler,
    };
    httpd_register_uri_handler(s_httpd, &hello_uri);
    httpd_register_uri_handler(s_httpd, &library_uri);
    httpd_register_uri_handler(s_httpd, &track_uri);
    if (!s_beacon_task) {
        if (xTaskCreate(beacon_task, "cdj_beacon", 4096, NULL, 3, &s_beacon_task) != pdPASS) {
            httpd_stop(s_httpd);
            s_httpd = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "HTTP server listening on port %u", CDJ_LINK_SERVER_PORT);
    return ESP_OK;
}

uint32_t cdj_link_server_track_count(void)
{
    return s_track_count;
}

static void beacon_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGW(TAG, "beacon socket failed");
        s_beacon_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(CDJ_LINK_DISCOVERY_PORT),
        .sin_addr.s_addr = inet_addr("255.255.255.255"),
    };

    while (1) {
        cdj_link_discovery_packet_t packet;
        cdj_link_discovery_init(&packet, wifi_link_peer_id(), wifi_link_ssid(),
                                CDJ_LINK_SERVER_PORT, s_track_count);
        sendto(sock, &packet, sizeof(packet), 0, (struct sockaddr *)&dest, sizeof(dest));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
