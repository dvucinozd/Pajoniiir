#include "sd_diag_log.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "sd_diag_log";
static const char *LOG_DIR = "/sd/logs";
static const char *LOG_PATH = "/sd/logs/system.log";
static const char *ROTATED_LOG_PATH = "/sd/logs/system.log.1";
static const long MAX_LOG_BYTES = 256 * 1024;

static bool path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static esp_err_t ensure_log_dir(void)
{
    struct stat st;
    if (stat("/sd", &st) != 0 || !S_ISDIR(st.st_mode)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (mkdir(LOG_DIR, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static void rotate_if_needed(void)
{
    struct stat st;
    if (stat(LOG_PATH, &st) != 0 || st.st_size < MAX_LOG_BYTES) {
        return;
    }
    if (path_exists(ROTATED_LOG_PATH)) {
        unlink(ROTATED_LOG_PATH);
    }
    rename(LOG_PATH, ROTATED_LOG_PATH);
}

esp_err_t sd_diag_log_init(void)
{
    esp_err_t rc = ensure_log_dir();
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "diagnostic log unavailable: %s", esp_err_to_name(rc));
        return rc;
    }
    rotate_if_needed();
    return sd_diag_log_write("boot", "main deck firmware online");
}

esp_err_t sd_diag_log_write(const char *tag, const char *message)
{
    esp_err_t rc = ensure_log_dir();
    if (rc != ESP_OK) {
        return rc;
    }
    rotate_if_needed();

    FILE *fp = fopen(LOG_PATH, "a");
    if (!fp) {
        return ESP_FAIL;
    }

    int64_t uptime_ms = esp_timer_get_time() / 1000;
    fprintf(fp, "%lld ms [%s] %s\n",
            (long long)uptime_ms,
            tag ? tag : "app",
            message ? message : "");
    fclose(fp);
    return ESP_OK;
}
