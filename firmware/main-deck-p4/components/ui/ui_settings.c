#include "ui_settings.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

bool ui_settings_should_poll(uint32_t now_ms,
                             uint32_t last_poll_ms,
                             bool force,
                             uint32_t interval_ms)
{
    return force || last_poll_ms == 0 || (uint32_t)(now_ms - last_poll_ms) >= interval_ms;
}

#ifndef UI_SETTINGS_HOST_TEST

#include "ui_theme.h"

#ifndef WIN32
#include "bsp_jc4880.h"
#include "cdj_link_client.h"
#include "esp_timer.h"
#include "remote_cache.h"
#include "wifi_link.h"
#endif

typedef struct {
    bool valid;
    char text[80];
} ui_settings_text_cache_t;

typedef struct {
    bool valid;
    uint32_t color;
} ui_settings_color_cache_t;

static ui_settings_widgets_t s_widgets;
static ui_settings_color_cache_t s_cache_uart_color;
static ui_settings_color_cache_t s_cache_sd_color;
static int s_cache_uart_state = -1;
static uint32_t s_cache_uart_age_bucket = UINT32_MAX;
static int s_cache_sd_state = -1;
static uint32_t s_cache_sd_free_mib = UINT32_MAX;
static uint32_t s_cache_sd_total_mib = UINT32_MAX;
static uint32_t s_cache_sd_last_poll_ms = 0;
static ui_settings_text_cache_t s_cache_sd_text;
static ui_settings_text_cache_t s_cache_sd_cache_text;
static uint32_t s_cache_sd_cache_last_poll_ms = 0;
static uint32_t s_cache_sd_cache_mib = UINT32_MAX;
static uint32_t s_cache_sd_cache_tracks = UINT32_MAX;
static uint32_t s_cache_sd_cache_files = UINT32_MAX;

static void ui_settings_label_set_text_cached(lv_obj_t *label,
                                              ui_settings_text_cache_t *cache,
                                              const char *text)
{
    if (!label || !cache) {
        return;
    }
    const char *safe_text = text ? text : "";
    if (cache->valid && strncmp(cache->text, safe_text, sizeof(cache->text)) == 0) {
        return;
    }
    lv_label_set_text(label, safe_text);
    snprintf(cache->text, sizeof(cache->text), "%s", safe_text);
    cache->valid = true;
}

static void ui_settings_obj_set_text_color_cached(lv_obj_t *obj,
                                                  ui_settings_color_cache_t *cache,
                                                  lv_color_t color)
{
    if (!obj || !cache) {
        return;
    }
    uint32_t color_u32 = lv_color_to_u32(color);
    if (cache->valid && cache->color == color_u32) {
        return;
    }
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    cache->color = color_u32;
    cache->valid = true;
}

void ui_settings_init(const ui_settings_widgets_t *widgets)
{
    memset(&s_widgets, 0, sizeof(s_widgets));
    if (widgets) {
        s_widgets = *widgets;
    }
    ui_settings_invalidate();
}

void ui_settings_invalidate(void)
{
    s_cache_uart_color.valid = false;
    s_cache_sd_color.valid = false;
    s_cache_uart_state = -1;
    s_cache_uart_age_bucket = UINT32_MAX;
    s_cache_sd_state = -1;
    s_cache_sd_free_mib = UINT32_MAX;
    s_cache_sd_total_mib = UINT32_MAX;
    s_cache_sd_last_poll_ms = 0;
    s_cache_sd_text.valid = false;
    s_cache_sd_cache_text.valid = false;
    s_cache_sd_cache_last_poll_ms = 0;
    s_cache_sd_cache_mib = UINT32_MAX;
    s_cache_sd_cache_tracks = UINT32_MAX;
    s_cache_sd_cache_files = UINT32_MAX;
}

static void ui_settings_format_storage_size(uint64_t bytes, char *out, size_t out_size)
{
    const uint64_t gib = 1024ull * 1024ull * 1024ull;
    const uint64_t mib = 1024ull * 1024ull;
    uint64_t scale = mib;
    const char *unit = "MB";
    if (bytes >= gib) {
        scale = gib;
        unit = "GB";
    }

    uint64_t whole = bytes / scale;
    uint64_t frac = ((bytes % scale) * 10ull) / scale;
    snprintf(out, out_size, "%llu.%llu %s",
             (unsigned long long)whole,
             (unsigned long long)frac,
             unit);
}

#ifndef WIN32
static void ui_settings_update_uart_status_label(const deck_state_t *state)
{
    if (!s_widgets.uart_status || !state) {
        return;
    }

    int display_state;
    uint32_t age_bucket;
    if (state->control_link_connected) {
        display_state = 1;
        if (state->last_heartbeat_age_ms < 1000u) {
            age_bucket = state->last_heartbeat_age_ms / 100u;
            if (s_cache_uart_state != display_state || s_cache_uart_age_bucket != age_bucket) {
                lv_label_set_text_fmt(s_widgets.uart_status,
                                      "Control Link (S3): Connected (age %lu ms)",
                                      (unsigned long)(age_bucket * 100u));
                s_cache_uart_state = display_state;
                s_cache_uart_age_bucket = age_bucket;
            }
        } else {
            age_bucket = state->last_heartbeat_age_ms / 1000u;
            if (s_cache_uart_state != display_state || s_cache_uart_age_bucket != age_bucket) {
                lv_label_set_text_fmt(s_widgets.uart_status,
                                      "Control Link (S3): Connected (age %lu s)",
                                      (unsigned long)age_bucket);
                s_cache_uart_state = display_state;
                s_cache_uart_age_bucket = age_bucket;
            }
        }
        ui_settings_obj_set_text_color_cached(s_widgets.uart_status, &s_cache_uart_color, COL_GREEN);
        return;
    }

    if (state->last_heartbeat_age_ms == UINT32_MAX) {
        display_state = 0;
        age_bucket = UINT32_MAX;
        if (s_cache_uart_state != display_state || s_cache_uart_age_bucket != age_bucket) {
            lv_label_set_text(s_widgets.uart_status, "Control Link (S3): Offline (no heartbeat)");
            s_cache_uart_state = display_state;
            s_cache_uart_age_bucket = age_bucket;
        }
    } else {
        display_state = 2;
        age_bucket = state->last_heartbeat_age_ms / 1000u;
        if (s_cache_uart_state != display_state || s_cache_uart_age_bucket != age_bucket) {
            lv_label_set_text_fmt(s_widgets.uart_status,
                                  "Control Link (S3): Offline (last %lu s ago)",
                                  (unsigned long)age_bucket);
            s_cache_uart_state = display_state;
            s_cache_uart_age_bucket = age_bucket;
        }
    }
    ui_settings_obj_set_text_color_cached(s_widgets.uart_status, &s_cache_uart_color, COL_RED);
}

static void ui_settings_update_sd_status_label(bool force)
{
    if (!s_widgets.sd_status) {
        return;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ull);
    if (!ui_settings_should_poll(now_ms, s_cache_sd_last_poll_ms, force, 1000u)) {
        return;
    }
    s_cache_sd_last_poll_ms = now_ms;

    bsp_sd_status_t status;
    esp_err_t rc = bsp_sd_get_status(&status);
    if (rc != ESP_OK || !status.mounted) {
        if (s_cache_sd_state != 0) {
            ui_settings_label_set_text_cached(s_widgets.sd_status,
                                              &s_cache_sd_text,
                                              "Offline (/sd unavailable)");
            s_cache_sd_state = 0;
            s_cache_sd_free_mib = UINT32_MAX;
            s_cache_sd_total_mib = UINT32_MAX;
        }
        ui_settings_obj_set_text_color_cached(s_widgets.sd_status, &s_cache_sd_color, COL_RED);
        return;
    }

    uint32_t free_mib = (uint32_t)(status.free_bytes / (1024ull * 1024ull));
    uint32_t total_mib = (uint32_t)(status.total_bytes / (1024ull * 1024ull));
    if (s_cache_sd_state != 1 ||
        s_cache_sd_free_mib != free_mib ||
        s_cache_sd_total_mib != total_mib) {
        char free_buf[16];
        char total_buf[16];
        char text[80];
        ui_settings_format_storage_size(status.free_bytes, free_buf, sizeof(free_buf));
        ui_settings_format_storage_size(status.total_bytes, total_buf, sizeof(total_buf));
        snprintf(text, sizeof(text), "Mounted: %s free / %s", free_buf, total_buf);
        ui_settings_label_set_text_cached(s_widgets.sd_status, &s_cache_sd_text, text);
        s_cache_sd_state = 1;
        s_cache_sd_free_mib = free_mib;
        s_cache_sd_total_mib = total_mib;
    }
    ui_settings_obj_set_text_color_cached(s_widgets.sd_status, &s_cache_sd_color, COL_GREEN);
}

static void ui_settings_update_sd_cache_status_label(bool force)
{
    if (!s_widgets.sd_cache_status) {
        return;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ull);
    if (!ui_settings_should_poll(now_ms, s_cache_sd_cache_last_poll_ms, force, 1000u)) {
        return;
    }
    s_cache_sd_cache_last_poll_ms = now_ms;

    remote_cache_stats_t stats;
    esp_err_t rc = remote_cache_get_stats(&stats);
    if (rc != ESP_OK) {
        ui_settings_label_set_text_cached(s_widgets.sd_cache_status,
                                          &s_cache_sd_cache_text,
                                          "Cache unavailable");
        s_cache_sd_cache_mib = UINT32_MAX;
        s_cache_sd_cache_tracks = UINT32_MAX;
        s_cache_sd_cache_files = UINT32_MAX;
        return;
    }

    uint32_t mib = (uint32_t)(stats.bytes / (1024ull * 1024ull));
    if (s_cache_sd_cache_mib != mib ||
        s_cache_sd_cache_tracks != stats.tracks ||
        s_cache_sd_cache_files != stats.files) {
        char size_buf[16];
        char text[80];
        ui_settings_format_storage_size(stats.bytes, size_buf, sizeof(size_buf));
        snprintf(text, sizeof(text), "%s, %lu tracks, %lu files",
                 size_buf,
                 (unsigned long)stats.tracks,
                 (unsigned long)stats.files);
        ui_settings_label_set_text_cached(s_widgets.sd_cache_status,
                                          &s_cache_sd_cache_text,
                                          text);
        s_cache_sd_cache_mib = mib;
        s_cache_sd_cache_tracks = stats.tracks;
        s_cache_sd_cache_files = stats.files;
    }
}

static void ui_settings_update_link_status_label(void)
{
    if (!s_widgets.link_status) {
        return;
    }

    wifi_link_status_t st = wifi_link_get_status();
    if (st.mode == WIFI_LINK_MODE_HOST) {
        lv_label_set_text_fmt(s_widgets.link_status,
                              "Link: HOST %s (%u client)",
                              st.ssid[0] ? st.ssid : "CDJ100S",
                              (unsigned)st.ap_clients);
        return;
    }

    if (st.mode == WIFI_LINK_MODE_JOIN) {
        cdj_link_peer_t peer;
        if (cdj_link_client_get_peer(&peer)) {
            lv_label_set_text_fmt(s_widgets.link_status,
                                  "Link: JOINED %s (%lu tracks)",
                                  peer.name[0] ? peer.name : peer.host,
                                  (unsigned long)peer.track_count);
        } else {
            lv_label_set_text(s_widgets.link_status, "Link: JOIN SCANNING");
        }
        return;
    }

    lv_label_set_text(s_widgets.link_status, "Link: OFF");
}
#endif

void ui_settings_update(const ui_frame_context_t *ctx)
{
    if (!ctx || ctx->active_tab != 6) {
        return;
    }
#ifndef WIN32
    ui_settings_update_uart_status_label(&ctx->deck_state[CTRL_DECK_1]);
    ui_settings_update_link_status_label();
    ui_settings_update_sd_status_label(false);
    ui_settings_update_sd_cache_status_label(false);
#endif
}

void ui_settings_refresh_storage(void)
{
#ifndef WIN32
    ui_settings_update_sd_status_label(true);
    ui_settings_update_sd_cache_status_label(true);
#endif
}

void ui_settings_note_link_mode_saved(const char *mode_name)
{
    (void)mode_name;
#ifndef WIN32
    if (s_widgets.link_status) {
        lv_label_set_text(s_widgets.link_status, "Link mode saved; reboot applies Wi-Fi role");
    }
#endif
}

#endif
