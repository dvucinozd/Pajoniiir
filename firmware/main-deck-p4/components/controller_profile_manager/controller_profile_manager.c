#include "controller_profile_manager.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Pure helpers (host-testable) ──────────────────────────────────────────── */

uint32_t controller_profile_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

esp_err_t controller_profile_meta_parse(const uint8_t *data, size_t len,
                                        controller_profile_meta_t *meta)
{
    if (!data || !meta) {
        return ESP_ERR_INVALID_ARG;
    }

    meta->valid = false;
    meta->vid = 0;
    meta->pid = 0;
    meta->input_count = 0;
    meta->output_count = 0;
    meta->size = 0;

    if (len < CPM_HEADER_SIZE || len > CPM_MAX_PROFILE_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (memcmp(data, CPM_MAGIC, 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rd_u16(data + 4) != CPM_VERSION || rd_u16(data + 6) != CPM_HEADER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t profile_size = rd_u32(data + 8);
    if (profile_size != len) {
        return ESP_ERR_INVALID_ARG;
    }
    if (controller_profile_crc32(data + 16, len - 16) != rd_u32(data + 12)) {
        return ESP_ERR_INVALID_ARG;
    }

    meta->vid = rd_u16(data + 16);
    meta->pid = rd_u16(data + 18);
    meta->input_count = rd_u16(data + 24);
    meta->output_count = rd_u16(data + 26);
    meta->size = profile_size;
    meta->valid = true;
    return ESP_OK;
}

/* Read <dir>/profile.s3bin (bounded) and header-validate it into *meta. */
static bool load_profile_meta(const char *dir_path, const char *name,
                              controller_profile_meta_t *meta)
{
    char path[CPM_PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s/profile.s3bin", dir_path, name);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return false; /* not a profile directory */
    }

    uint8_t *buf = (uint8_t *)malloc(CPM_MAX_PROFILE_SIZE + 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    size_t len = fread(buf, 1, CPM_MAX_PROFILE_SIZE + 1, f);
    fclose(f);

    memset(meta, 0, sizeof(*meta));
    /* Directory names longer than the id field are truncated deliberately. */
    snprintf(meta->id, sizeof(meta->id), "%.*s", (int)sizeof(meta->id) - 1, name);
    memcpy(meta->path, path, sizeof(meta->path));
    meta->path[sizeof(meta->path) - 1] = '\0';
    /* A too-large file fails validation inside meta_parse (len bound). */
    (void)controller_profile_meta_parse(buf, len, meta);
    free(buf);
    return true; /* recorded (possibly with valid=false so UI can report it) */
}

esp_err_t controller_profile_scan_dir(const char *root,
                                      controller_profile_registry_t *reg)
{
    if (!root || !reg) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(reg, 0, sizeof(*reg));
    reg->active_index = -1;

    DIR *dir = opendir(root);
    if (!dir) {
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && reg->count < CPM_MAX_PROFILES) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        controller_profile_meta_t meta;
        if (load_profile_meta(root, entry->d_name, &meta)) {
            reg->profiles[reg->count++] = meta;
        }
    }
    closedir(dir);
    return ESP_OK;
}

int controller_profile_registry_match(const controller_profile_registry_t *reg,
                                      uint16_t vid, uint16_t pid)
{
    if (!reg) {
        return -1;
    }
    for (uint8_t i = 0; i < reg->count; i++) {
        if (reg->profiles[i].valid &&
            reg->profiles[i].vid == vid && reg->profiles[i].pid == pid) {
            return (int)i;
        }
    }
    return -1;
}

int controller_profile_registry_on_descriptor(controller_profile_registry_t *reg,
                                              uint16_t vid, uint16_t pid)
{
    if (!reg) {
        return -1;
    }
    reg->controller_present = true;
    reg->connected_vid = vid;
    reg->connected_pid = pid;
    int idx = controller_profile_registry_match(reg, vid, pid);
    reg->active_index = (int8_t)idx;
    return idx;
}

/* ── Firmware glue (ESP-IDF only) ──────────────────────────────────────────── */

#ifndef CONTROLLER_PROFILE_MANAGER_PC_TEST

#include "esp_log.h"
#include "sdkconfig.h"

#ifndef CONFIG_CONTROLLER_PROFILE_SD_PATH
#define CONFIG_CONTROLLER_PROFILE_SD_PATH "/sd/controllers"
#endif

static const char *TAG = "ctrl_profile";

static controller_profile_registry_t s_registry;

esp_err_t controller_profile_manager_init(void)
{
    memset(&s_registry, 0, sizeof(s_registry));
    s_registry.active_index = -1;
    return ESP_OK;
}

esp_err_t controller_profile_manager_scan_storage(void)
{
    esp_err_t rc = controller_profile_scan_dir(CONFIG_CONTROLLER_PROFILE_SD_PATH,
                                               &s_registry);
    if (rc == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "no %s directory (SD missing or no profiles)",
                 CONFIG_CONTROLLER_PROFILE_SD_PATH);
        return rc;
    }
    if (rc != ESP_OK) {
        return rc;
    }

    ESP_LOGI(TAG, "%u controller profile(s) in %s",
             (unsigned)s_registry.count, CONFIG_CONTROLLER_PROFILE_SD_PATH);
    for (uint8_t i = 0; i < s_registry.count; i++) {
        const controller_profile_meta_t *m = &s_registry.profiles[i];
        if (m->valid) {
            ESP_LOGI(TAG,
                     "  [%u] %s VID=0x%04X PID=0x%04X inputs=%u outputs=%u (%u B)",
                     (unsigned)i, m->id, m->vid, m->pid,
                     (unsigned)m->input_count, (unsigned)m->output_count,
                     (unsigned)m->size);
        } else {
            ESP_LOGW(TAG, "  [%u] %s INVALID (bad header/CRC) at %s",
                     (unsigned)i, m->id, m->path);
        }
    }
    return ESP_OK;
}

const controller_profile_registry_t *controller_profile_manager_get_registry(void)
{
    return &s_registry;
}

int controller_profile_manager_on_descriptor(uint16_t vid, uint16_t pid)
{
    int idx = controller_profile_registry_on_descriptor(&s_registry, vid, pid);
    if (idx >= 0) {
        ESP_LOGI(TAG, "controller VID=0x%04X PID=0x%04X -> profile '%s'",
                 vid, pid, s_registry.profiles[idx].id);
    } else {
        ESP_LOGW(TAG, "controller VID=0x%04X PID=0x%04X has no profile", vid, pid);
    }
    return idx;
}

int controller_profile_manager_on_descriptor_report(uint16_t vid, uint16_t pid,
                                                    uint16_t caps,
                                                    const char *product)
{
    s_registry.connected_caps = caps;
    memset(s_registry.connected_product, 0, sizeof(s_registry.connected_product));
    if (product) {
        snprintf(s_registry.connected_product, sizeof(s_registry.connected_product),
                 "%.*s", CPM_PRODUCT_MAX, product);
    }
    ESP_LOGI(TAG, "connected controller '%s' caps=0x%04X",
             s_registry.connected_product, caps);
    return controller_profile_manager_on_descriptor(vid, pid);
}

#endif /* CONTROLLER_PROFILE_MANAGER_PC_TEST */
