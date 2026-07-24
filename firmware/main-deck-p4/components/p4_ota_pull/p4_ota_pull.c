#include "p4_ota_pull.h"
#include "p4_ota_pull_config.h"

#include "app_settings.h"
#include "wifi_link.h"

#include "esp_app_desc.h"
#include "ota_manifest.h"
#include "p4_ota.h"
#include "p4_ota_policy.h"
#include <stdlib.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "ota_pull";

/* The channel document is small by design. A cap rather than a growing buffer
 * means a server that streams megabytes - or an SPA catch-all that answers 200
 * with a landing page, which this deck's own update host did until it was
 * fixed - is refused instead of exhausting the heap. */
#define CHANNEL_DOC_MAX 4096u

/* Long enough for a slow uplink to answer, short enough that the deck does not
 * sit off its own AP waiting for a server that never will. */
#define HTTP_TIMEOUT_MS 15000
#define STA_TIMEOUT_MS  20000u

static p4_ota_pull_status_t s_status;
/* Bundle path from the last check that found something. */
static char s_install_url[P4_OTA_PULL_URL_MAX + 1u];
static volatile bool s_running;

static void note(p4_ota_pull_state_t state, esp_err_t err, const char *detail)
{
    s_status.state = state;
    s_status.last_error = err;
    snprintf(s_status.detail, sizeof(s_status.detail), "%s", detail ? detail : "");
}

/* Fetch <base>/latest.json into `buf`. Returns the byte count, or a negative
 * esp_err_t. */
static int fetch_channel_doc(const char *base_url, char *buf, size_t cap)
{
    char url[APP_SETTINGS_OTA_URL_CAP + 16u];
    size_t n = strnlen(base_url, APP_SETTINGS_OTA_URL_CAP);
    /* Tolerate a configured URL with or without a trailing slash rather than
     * making the operator guess which one this wants. */
    bool slash = n > 0u && base_url[n - 1u] == '/';
    snprintf(url, sizeof(url), "%s%slatest.json", base_url, slash ? "" : "/");

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -ESP_ERR_NO_MEM;

    int result;
    esp_err_t rc = esp_http_client_open(client, 0);
    if (rc != ESP_OK) {
        result = -rc;
        goto done;
    }
    int64_t len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        /* A real 404 here is the useful answer "nothing published", which is
         * only distinguishable because the server serves files rather than a
         * catch-all. */
        ESP_LOGW(TAG, "%s answered HTTP %d", url, status);
        result = -ESP_ERR_NOT_FOUND;
        goto done;
    }
    if (len > (int64_t)cap) {
        ESP_LOGW(TAG, "channel document is %lld bytes, cap is %u",
                 (long long)len, (unsigned)cap);
        result = -ESP_ERR_INVALID_SIZE;
        goto done;
    }

    int total = 0;
    while ((size_t)total < cap) {
        int got = esp_http_client_read(client, buf + total, (int)(cap - (size_t)total));
        if (got < 0) { result = -ESP_FAIL; goto done; }
        if (got == 0) break;
        total += got;
    }
    /* Chunked responses report no length up front, so the cap has to be
     * enforced on what actually arrived as well. */
    if ((size_t)total >= cap) {
        result = -ESP_ERR_INVALID_SIZE;
        goto done;
    }
    result = total;

done:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

static void check_task(void *arg)
{
    (void)arg;
    static char doc[CHANNEL_DOC_MAX];

    char ssid[APP_SETTINGS_OTA_SSID_CAP] = {0};
    char pass[APP_SETTINGS_OTA_PASS_CAP] = {0};
    char url[APP_SETTINGS_OTA_URL_CAP] = {0};
    app_settings_ota_get_ssid(ssid, sizeof(ssid));
    app_settings_ota_get_url(url, sizeof(url));
    app_settings_ota_copy_password(pass, sizeof(pass));

    /* Let the HTTP handler finish and its 202 reach the client before the
     * transition starts. Without this the first thing this task does is stop
     * the web server the handler is still executing inside: the reply never
     * goes out, the caller sees a dead connection, and the operator is left
     * unable to tell "started" from "crashed". */
    vTaskDelay(pdMS_TO_TICKS(500));

    note(P4_OTA_PULL_CHECKING, ESP_OK, "joining service network");
    esp_err_t rc = wifi_link_switch_to_sta(ssid, pass, STA_TIMEOUT_MS);
    memset(pass, 0, sizeof(pass));   /* done with it; do not leave it on the stack */

    if (rc != ESP_OK) {
        note(P4_OTA_PULL_FAILED, rc,
             rc == ESP_ERR_TIMEOUT ? "no address from network"
                                   : "could not join network");
    } else {
        note(P4_OTA_PULL_CHECKING, ESP_OK, "reading update channel");
        int got = fetch_channel_doc(url, doc, sizeof(doc));
        if (got < 0) {
            esp_err_t herr = (esp_err_t)(-got);
            note(P4_OTA_PULL_FAILED, herr,
                 herr == ESP_ERR_NOT_FOUND   ? "no update published"
               : herr == ESP_ERR_INVALID_SIZE ? "channel document too large"
                                              : "could not reach update server");
        } else {
            p4_ota_pull_manifest_t m;
            p4_ota_pull_manifest_result_t pr =
                p4_ota_pull_manifest_parse(doc, (size_t)got, &m);
            if (pr != P4_OTA_PULL_MANIFEST_OK) {
                note(P4_OTA_PULL_FAILED, ESP_ERR_INVALID_RESPONSE,
                     p4_ota_pull_manifest_result_name(pr));
            } else {
                const esp_app_desc_t *me = esp_app_get_description();
                snprintf(s_status.available_release,
                         sizeof(s_status.available_release), "%s", m.release);
                s_status.available_size = m.size;
                if (p4_ota_pull_manifest_differs(&m, me->version)) {
                    snprintf(s_install_url, sizeof(s_install_url), "%s", m.url);
                    note(P4_OTA_PULL_AVAILABLE, ESP_OK, m.release);
                } else {
                    note(P4_OTA_PULL_UP_TO_DATE, ESP_OK, "already running this build");
                }
            }
        }
    }

    /* Unconditional. Whatever happened above, being reachable again matters
     * more than the result of the check. */
    esp_err_t back = wifi_link_restore_ap();
    if (back != ESP_OK) {
        note(P4_OTA_PULL_FAILED, back, "AP DID NOT COME BACK");
    }

    s_running = false;
    vTaskDelete(NULL);
}

/* ── Download and install ─────────────────────────────────────────────────── */

/* Streamed in chunks rather than buffered whole: the bundle is over 2 MB and
 * there is nowhere sensible to put it before it is verified. */
#define DL_CHUNK 4096u

/* Returns ESP_OK only when the whole signed bundle has been written and
 * finished. The manifest is parsed and its signature verified from the first
 * DDJ_OTA_HEADER_SIZE bytes, before p4_ota_begin touches the flash - the same
 * order the push path uses, and the reason arriving over TLS grants the bundle
 * no additional trust. */
static esp_err_t download_and_install(const char *base_url, const char *rel_url,
                                      uint32_t expect_size)
{
    char url[APP_SETTINGS_OTA_URL_CAP + P4_OTA_PULL_URL_MAX + 4u];
    size_t n = strnlen(base_url, APP_SETTINGS_OTA_URL_CAP);
    bool slash = n > 0u && base_url[n - 1u] == '/';
    snprintf(url, sizeof(url), "%s%s%s", base_url, slash ? "" : "/", rel_url);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_ERR_NO_MEM;

    uint8_t *buf = malloc(DL_CHUNK);
    if (!buf) { esp_http_client_cleanup(client); return ESP_ERR_NO_MEM; }

    esp_err_t rc = esp_http_client_open(client, 0);
    if (rc != ESP_OK) goto done;

    int64_t len = esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200) {
        rc = ESP_ERR_NOT_FOUND;
        goto done;
    }
    /* The channel document already told us the size. A mismatch means the two
     * disagree, and installing either would be guessing which is right. */
    if (len != (int64_t)expect_size) {
        ESP_LOGE(TAG, "server offers %lld bytes, channel says %u",
                 (long long)len, (unsigned)expect_size);
        rc = ESP_ERR_INVALID_SIZE;
        goto done;
    }

    /* Header first, whole, before anything is written. */
    uint8_t header[DDJ_OTA_HEADER_SIZE];
    size_t have = 0;
    while (have < sizeof(header)) {
        int got = esp_http_client_read(client, (char *)header + have,
                                       (int)(sizeof(header) - have));
        if (got <= 0) { rc = ESP_ERR_INVALID_RESPONSE; goto done; }
        have += (size_t)got;
    }

    ddj_ota_manifest_t manifest;
    ddj_ota_manifest_result_t mrc = ddj_ota_manifest_parse(
        header, sizeof(header), DDJ_OTA_TARGET_P4, P4_OTA_ESP32P4_CHIP_ID,
        "main-deck-p4", P4_OTA_MAX_IMAGE_SIZE, &manifest);
    if (mrc != DDJ_OTA_MANIFEST_OK) {
        ESP_LOGE(TAG, "manifest rejected: %s", ddj_ota_manifest_result_name(mrc));
        rc = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    if (!ddj_ota_manifest_verify_signature(header, sizeof(header))) {
        ESP_LOGE(TAG, "manifest signature is not ours");
        rc = ESP_ERR_INVALID_MAC;
        goto done;
    }
    if (expect_size != DDJ_OTA_HEADER_SIZE + manifest.image_size) {
        rc = ESP_ERR_INVALID_SIZE;
        goto done;
    }

    rc = p4_ota_begin(&manifest);
    if (rc != ESP_OK) goto done;

    size_t written = 0;
    while (written < manifest.image_size) {
        size_t want = manifest.image_size - written;
        if (want > DL_CHUNK) want = DL_CHUNK;
        int got = esp_http_client_read(client, (char *)buf, (int)want);
        if (got <= 0) {
            p4_ota_abort("download truncated");
            rc = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
        rc = p4_ota_write(buf, (size_t)got);
        if (rc != ESP_OK) {
            p4_ota_abort("flash write failed");
            goto done;
        }
        written += (size_t)got;
        s_status.downloaded = (uint32_t)written;
    }

    /* Verifies the image SHA-256 against the signed manifest and activates the
     * slot. Anything wrong here and nothing is booted. */
    rc = p4_ota_finish();

done:
    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return rc;
}

static void install_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));   /* let the 202 out; see check_task */

    char ssid[APP_SETTINGS_OTA_SSID_CAP] = {0};
    char pass[APP_SETTINGS_OTA_PASS_CAP] = {0};
    char url[APP_SETTINGS_OTA_URL_CAP] = {0};
    app_settings_ota_get_ssid(ssid, sizeof(ssid));
    app_settings_ota_get_url(url, sizeof(url));
    app_settings_ota_copy_password(pass, sizeof(pass));

    note(P4_OTA_PULL_DOWNLOADING, ESP_OK, "joining service network");
    esp_err_t rc = wifi_link_switch_to_sta(ssid, pass, STA_TIMEOUT_MS);
    memset(pass, 0, sizeof(pass));

    if (rc != ESP_OK) {
        note(P4_OTA_PULL_FAILED, rc, "could not join network");
    } else {
        note(P4_OTA_PULL_DOWNLOADING, ESP_OK, "downloading");
        rc = download_and_install(url, s_install_url, s_status.available_size);
        if (rc == ESP_OK) {
            note(P4_OTA_PULL_READY_TO_REBOOT, ESP_OK, "verified, restarting");
        } else if (rc == ESP_ERR_INVALID_MAC) {
            note(P4_OTA_PULL_FAILED, rc, "signature is not ours - NOT installed");
        } else if (rc == ESP_ERR_INVALID_SIZE) {
            note(P4_OTA_PULL_FAILED, rc, "size does not match the manifest");
        } else if (rc == ESP_ERR_NOT_FOUND) {
            note(P4_OTA_PULL_FAILED, rc, "bundle not on the server");
        } else {
            note(P4_OTA_PULL_FAILED, rc, "download or flash failed");
        }
    }

    /* Restore the AP even when about to reboot: if the restart is prevented or
     * the new image rolls back, the deck must still be reachable. */
    esp_err_t back = wifi_link_restore_ap();
    if (back != ESP_OK && rc == ESP_OK) {
        ESP_LOGW(TAG, "AP did not restore, but the update is staged");
    }

    s_running = false;
    if (rc == ESP_OK) {
        /* Long enough for the status to be read once before the deck goes. */
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }
    vTaskDelete(NULL);
}

esp_err_t p4_ota_pull_install_start(const char *expected_release)
{
    if (s_running) return ESP_ERR_INVALID_STATE;
    /* Only what a check actually offered, and only if the caller names it back.
     * A stale page must not be able to install something never seen. */
    if (s_status.state != P4_OTA_PULL_AVAILABLE) return ESP_ERR_INVALID_STATE;
    if (!expected_release ||
        strncmp(expected_release, s_status.available_release,
                P4_OTA_PULL_RELEASE_MAX) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_install_url[0] == '\0' || s_status.available_size == 0u) {
        return ESP_ERR_INVALID_STATE;
    }

    s_running = true;
    s_status.downloaded = 0u;
    note(P4_OTA_PULL_DOWNLOADING, ESP_OK, "starting");
    /* 10 KiB: TLS records plus the flash write path run on this task. */
    if (xTaskCreate(install_task, "ota_install", 10240, NULL, 4, NULL) != pdPASS) {
        s_running = false;
        note(P4_OTA_PULL_FAILED, ESP_ERR_NO_MEM, "could not start task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t p4_ota_pull_check_start(void)
{
    if (s_running) return ESP_ERR_INVALID_STATE;

    char ssid[APP_SETTINGS_OTA_SSID_CAP] = {0};
    char url[APP_SETTINGS_OTA_URL_CAP] = {0};
    app_settings_ota_get_ssid(ssid, sizeof(ssid));
    app_settings_ota_get_url(url, sizeof(url));
    if (ssid[0] == '\0' || url[0] == '\0') return ESP_ERR_INVALID_ARG;
    if (p4_ota_cfg_check_url(url) != P4_OTA_CFG_OK) return ESP_ERR_INVALID_ARG;

    s_running = true;
    note(P4_OTA_PULL_CHECKING, ESP_OK, "starting");
    s_status.available_release[0] = '\0';
    s_status.available_size = 0u;
    /* 8 KiB: TLS handshake and the mbedTLS record buffers run on this task. */
    if (xTaskCreate(check_task, "ota_check", 8192, NULL, 4, NULL) != pdPASS) {
        s_running = false;
        note(P4_OTA_PULL_FAILED, ESP_ERR_NO_MEM, "could not start task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

p4_ota_pull_status_t p4_ota_pull_get_status(void)
{
    return s_status;
}
