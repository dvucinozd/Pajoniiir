#pragma once
typedef struct {
    const char *project_name;
    const char *version;
    const char *idf_ver;
} esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);
