#include "remote_cache.h"

#include "cdj_link_client.h"
#include "esp_check.h"
#include "esp_log.h"
#include "remote_cache_retry.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ff.h"

static const char *TAG = "remote_cache";
static const char *CACHE_ROOT = "/sd/cdjlink";
static uint8_t s_progress;
static char s_status[40] = "IDLE";

static void set_status(const char *status, uint8_t progress)
{
    snprintf(s_status, sizeof(s_status), "%s", status ? status : "IDLE");
    s_progress = progress;
}

uint8_t remote_cache_progress(void)
{
    return s_progress;
}

const char *remote_cache_status(void)
{
    return s_status;
}

static bool file_has_size(const char *path, uint32_t expected)
{
    struct stat st;
    return path && stat(path, &st) == 0 && st.st_size >= 0 && (uint32_t)st.st_size == expected;
}

static esp_err_t sd_available(void)
{
    struct stat st;
    return stat("/sd", &st) == 0 && S_ISDIR(st.st_mode) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t sd_has_free_space(uint64_t required_bytes)
{
    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    if (f_getfree("/sd", &free_clusters, &fs) != FR_OK || !fs) {
        return ESP_ERR_NOT_FOUND;
    }
    uint32_t sector_size = 512u;
#if FF_MAX_SS != FF_MIN_SS
    sector_size = fs->ssize;
#endif
    uint64_t free_bytes = (uint64_t)free_clusters * (uint64_t)fs->csize * (uint64_t)sector_size;
    return free_bytes >= required_bytes ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t mkdir_if_missing(const char *path)
{
    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "mkdir %s: %s", path, strerror(errno));
    return ESP_FAIL;
}

static esp_err_t ensure_dirs(const char *peer_id, uint32_t track_key, remote_cache_entry_t *entry)
{
    ESP_RETURN_ON_ERROR(mkdir_if_missing(CACHE_ROOT), TAG, "cache root");

    char peer_dir[96];
    snprintf(peer_dir, sizeof(peer_dir), "%s/%s", CACHE_ROOT, peer_id);
    ESP_RETURN_ON_ERROR(mkdir_if_missing(peer_dir), TAG, "peer dir");

    snprintf(entry->dir_path, sizeof(entry->dir_path), "%s/%lu", peer_dir, (unsigned long)track_key);
    ESP_RETURN_ON_ERROR(mkdir_if_missing(entry->dir_path), TAG, "track dir");

    snprintf(entry->manifest_path, sizeof(entry->manifest_path), "%s/manifest.bin", entry->dir_path);
    snprintf(entry->audio_path, sizeof(entry->audio_path), "%s/audio.mp3", entry->dir_path);
    snprintf(entry->dat_path, sizeof(entry->dat_path), "%s/ANLZ0000.DAT", entry->dir_path);
    snprintf(entry->ext_path, sizeof(entry->ext_path), "%s/ANLZ0000.EXT", entry->dir_path);
    return ESP_OK;
}

static bool path_is_dot_entry(const char *name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static esp_err_t cache_walk_stats(const char *path, remote_cache_stats_t *stats, uint8_t depth)
{
    DIR *dir = opendir(path);
    if (!dir) {
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (path_is_dot_entry(entry->d_name)) {
            continue;
        }

        char child[224];
        int written = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (written < 0 || written >= (int)sizeof(child)) {
            closedir(dir);
            return ESP_ERR_INVALID_SIZE;
        }

        struct stat st;
        if (stat(child, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (depth == 1) {
                stats->tracks++;
            }
            esp_err_t rc = cache_walk_stats(child, stats, depth + 1);
            if (rc != ESP_OK && rc != ESP_ERR_NOT_FOUND) {
                closedir(dir);
                return rc;
            }
        } else if (S_ISREG(st.st_mode)) {
            stats->files++;
            if (st.st_size > 0) {
                stats->bytes += (uint64_t)st.st_size;
            }
        }
    }

    closedir(dir);
    return ESP_OK;
}

static esp_err_t cache_remove_tree(const char *path, bool remove_root)
{
    if (strncmp(path, CACHE_ROOT, strlen(CACHE_ROOT)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        return errno == ENOENT ? ESP_OK : ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (path_is_dot_entry(entry->d_name)) {
            continue;
        }

        char child[224];
        int written = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (written < 0 || written >= (int)sizeof(child)) {
            closedir(dir);
            return ESP_ERR_INVALID_SIZE;
        }

        struct stat st;
        if (stat(child, &st) != 0) {
            continue;
        }

        esp_err_t rc = ESP_OK;
        if (S_ISDIR(st.st_mode)) {
            rc = cache_remove_tree(child, true);
        } else {
            rc = unlink(child) == 0 || errno == ENOENT ? ESP_OK : ESP_FAIL;
        }
        if (rc != ESP_OK) {
            closedir(dir);
            return rc;
        }
    }

    closedir(dir);
    if (remove_root && rmdir(path) != 0 && errno != ENOENT) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t remote_cache_get_stats(remote_cache_stats_t *out_stats)
{
    if (!out_stats) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_stats, 0, sizeof(*out_stats));
    if (sd_available() != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t rc = cache_walk_stats(CACHE_ROOT, out_stats, 0);
    return rc == ESP_ERR_NOT_FOUND ? ESP_OK : rc;
}

esp_err_t remote_cache_clear(void)
{
    if (sd_available() != ESP_OK) {
        set_status("SD CACHE REQUIRED", 0);
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t rc = cache_remove_tree(CACHE_ROOT, false);
    if (rc == ESP_OK) {
        set_status("CACHE CLEARED", 0);
    } else {
        set_status("CACHE ERR", 0);
    }
    return rc;
}

static esp_err_t write_manifest(const char *path, const cdj_link_track_manifest_t *manifest)
{
    uint8_t buf[64];
    size_t len = 0;
    if (cdj_link_manifest_encode(buf, sizeof(buf), manifest, &len) != CDJ_LINK_OK) {
        return ESP_FAIL;
    }
    char part[224];
    snprintf(part, sizeof(part), "%s.part", path);
    FILE *fp = fopen(part, "wb");
    if (!fp) {
        return ESP_FAIL;
    }
    bool ok = fwrite(buf, 1, len, fp) == len;
    fclose(fp);
    if (!ok) {
        unlink(part);
        return ESP_FAIL;
    }
    unlink(path);
    return rename(part, path) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t download_if_missing(uint32_t track_key,
                                     const char *asset,
                                     const char *path,
                                     uint32_t expected_size,
                                     uint8_t progress)
{
    if (file_has_size(path, expected_size)) {
        return ESP_OK;
    }

    char part[224];
    snprintf(part, sizeof(part), "%s.part", path);
    unlink(part);
    set_status(asset, progress);
    uint32_t got = 0;
    esp_err_t rc = ESP_OK;
    for (uint8_t attempt = 1; attempt <= REMOTE_CACHE_BUSY_RETRY_LIMIT; attempt++) {
        got = 0;
        rc = cdj_link_client_download_asset(track_key, asset, part, expected_size, &got);
        if (!remote_cache_should_retry(rc, attempt)) {
            break;
        }
        set_status("HOST BUSY", progress);
        ESP_LOGW(TAG, "%s busy, retry %u/%u", asset, (unsigned)attempt,
                 (unsigned)REMOTE_CACHE_BUSY_RETRY_LIMIT);
        vTaskDelay(pdMS_TO_TICKS(remote_cache_retry_delay_ms(attempt)));
        set_status(asset, progress);
    }
    if (rc != ESP_OK || got != expected_size) {
        unlink(part);
        return rc == ESP_OK ? ESP_ERR_INVALID_SIZE : rc;
    }
    unlink(path);
    if (rename(part, path) != 0) {
        unlink(part);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t remote_cache_prepare(uint32_t track_key, remote_cache_entry_t *out_entry)
{
    if (!out_entry || track_key == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_entry, 0, sizeof(*out_entry));
    set_status("MANIFEST", 5);

    cdj_link_peer_t peer;
    if (!cdj_link_client_get_peer(&peer)) {
        set_status("JOIN OFFLINE", 0);
        return ESP_ERR_NOT_FOUND;
    }
    if (sd_available() != ESP_OK) {
        set_status("SD CACHE REQUIRED", 0);
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t dir_rc = ensure_dirs(peer.peer_id, track_key, out_entry);
    if (dir_rc != ESP_OK) {
        set_status("SD CACHE REQUIRED", 0);
        return dir_rc;
    }

    esp_err_t rc = ESP_OK;
    for (uint8_t attempt = 1; attempt <= REMOTE_CACHE_BUSY_RETRY_LIMIT; attempt++) {
        rc = cdj_link_client_fetch_manifest(track_key, &out_entry->manifest);
        if (!remote_cache_should_retry(rc, attempt)) {
            break;
        }
        set_status("HOST BUSY", 5);
        ESP_LOGW(TAG, "manifest busy, retry %u/%u", (unsigned)attempt,
                 (unsigned)REMOTE_CACHE_BUSY_RETRY_LIMIT);
        vTaskDelay(pdMS_TO_TICKS(remote_cache_retry_delay_ms(attempt)));
        set_status("MANIFEST", 5);
    }
    if (rc != ESP_OK) {
        set_status(rc == ESP_ERR_INVALID_STATE ? "HOST BUSY" : "MANIFEST ERR", 0);
        return rc;
    }

    uint64_t required = 1024u * 1024u;
    if (!file_has_size(out_entry->dat_path, out_entry->manifest.dat_size)) {
        required += out_entry->manifest.dat_size;
    }
    if (out_entry->manifest.has_ext &&
        !file_has_size(out_entry->ext_path, out_entry->manifest.ext_size)) {
        required += out_entry->manifest.ext_size;
    }
    if (!file_has_size(out_entry->audio_path, out_entry->manifest.audio_size)) {
        required += out_entry->manifest.audio_size;
    }
    esp_err_t space_rc = sd_has_free_space(required);
    if (space_rc == ESP_ERR_NOT_FOUND) {
        set_status("SD CACHE REQUIRED", 0);
        return space_rc;
    }
    if (space_rc != ESP_OK) {
        set_status("SD CACHE FULL", 0);
        return space_rc;
    }
    if (write_manifest(out_entry->manifest_path, &out_entry->manifest) != ESP_OK) {
        set_status("CACHE ERR", 0);
        return ESP_FAIL;
    }

    rc = download_if_missing(track_key, "ANLZ0000.DAT", out_entry->dat_path,
                             out_entry->manifest.dat_size, 20);
    if (rc != ESP_OK) {
        set_status(rc == ESP_ERR_INVALID_STATE ? "HOST BUSY" : "DAT ERR", 0);
        return rc;
    }

    if (out_entry->manifest.has_ext) {
        rc = download_if_missing(track_key, "ANLZ0000.EXT", out_entry->ext_path,
                                 out_entry->manifest.ext_size, 40);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "EXT cache failed, continuing without high waveform: %s", esp_err_to_name(rc));
            out_entry->manifest.has_ext = 0;
            out_entry->manifest.ext_size = 0;
        }
    }

    rc = download_if_missing(track_key, "audio.mp3", out_entry->audio_path,
                             out_entry->manifest.audio_size, 65);
    if (rc != ESP_OK) {
        set_status(rc == ESP_ERR_INVALID_STATE ? "HOST BUSY" : "AUDIO ERR", 0);
        return rc;
    }

    set_status("CACHE READY", 100);
    ESP_LOGI(TAG, "track %lu cached in %s", (unsigned long)track_key, out_entry->dir_path);
    return ESP_OK;
}
