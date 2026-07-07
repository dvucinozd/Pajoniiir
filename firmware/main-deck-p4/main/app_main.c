#include "control_link.h"
#include "deck_core.h"
#include "bsp_jc4880.h"
#include "library.h"
#include "audio_engine.h"
#include "ui.h"
#include "ui_settings.h"
#include "usb_storage.h"
#include "app_settings.h"
#include "media_io_gate.h"
#include "wifi_link.h"
#include "web_server.h"
#include "sd_diag_log.h"
#include "sdkconfig.h"
#if CONFIG_MONITOR_PCM_LINK_ENABLED
#include "monitor_pcm_link.h"
#endif
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "main";

void p4_tcm_heap_guard_keep(void);

static void on_s3_debug_ap_toggle(bool enable)
{
    control_link_send_state(CTRL_ID_S3_DEBUG_AP, enable ? 1 : 0);
}

// Called from the USB storage task when the Rekordbox drive mounts/unmounts.
static void on_usb_storage_event(bool mounted)
{
    if (mounted) {
        esp_err_t rc = library_init();   // open export.pdb, build the track index
        if (rc == ESP_OK) {
            ESP_LOGW(TAG, "USB media library loaded: %d tracks", library_count());
        } else {
            ESP_LOGW(TAG, "library_init after USB mount: %s", esp_err_to_name(rc));
        }
        ui_trigger_library_refresh();            // safely schedule table repopulation in the LVGL task context
    } else {
        ESP_LOGW(TAG, "USB drive removed");
        esp_err_t stop_rc = audio_engine_stop_all();
        if (stop_rc != ESP_OK) {
            ESP_LOGE(TAG, "audio_engine_stop on USB removal: %s", esp_err_to_name(stop_rc));
        }
        library_clear();
        ui_notify_usb_removed();
        ui_trigger_library_refresh();
    }
}

void app_main(void)
{
    p4_tcm_heap_guard_keep();

    ESP_LOGI(TAG, "DDJ-FFL4 P4 main deck firmware starting");
    ESP_LOGI(TAG, "Board: JC4880P443C_I_W (ESP32-P4)");
    // Log the reason for the most recent reset. A spontaneous reboot during use
    // that reports BROWNOUT points at power (bus-powered USB drive current
    // spike); PANIC/WDT points at firmware. Visible at the default WARN level.
    ESP_LOGW(TAG, "reset reason: %d", (int)esp_reset_reason());

#if CONFIG_MONITOR_PCM_LINK_ENABLED
    // Acquire the monitor link I2S unit before the heavy subsystems come up:
    // on rev v1.3 (eco2) silicon, claiming a fresh I2S unit late in boot with
    // DSI/PSRAM/USB traffic active has hard-frozen the HP bus.
    ESP_ERROR_CHECK(monitor_pcm_link_start_transport());
#if CONFIG_MONITOR_PCM_LINK_BENCH_TONE
    ESP_LOGW(TAG, "monitor PCM bench profile active; skipping full P4 app startup");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
#endif

    // ── Persistent settings (NVS) ────────────────────────────────────────────
    app_settings_init();   // also initialises NVS; falls back to defaults
    ESP_ERROR_CHECK(media_io_gate_init());

    // ── Core subsystems ──────────────────────────────────────────────────────
    // deck_core creates the event queue; control_link pushes frames onto it.
    QueueHandle_t ctrl_queue;
    ESP_ERROR_CHECK(deck_core_init(&ctrl_queue));
    deck_core_set_s3_debug_ap_status_cb(ui_settings_set_s3_debug_ap_status);
    ESP_ERROR_CHECK(control_link_init(ctrl_queue));
    control_link_send_state(CTRL_ID_S3_DEBUG_AP, 0);

    // ── Board support (stubs until hardware arrives) ─────────────────────────
    ESP_ERROR_CHECK(bsp_display_init());
    ESP_ERROR_CHECK(bsp_touch_init());
    ESP_ERROR_CHECK(bsp_audio_init());
    ESP_ERROR_CHECK(bsp_sd_init());
    sd_diag_log_init();

    // Apply the saved monitor speaker route + backlight brightness.
    bsp_audio_set_output(app_settings_get().audio_out ? BSP_AUDIO_OUT_RCA
                                                      : BSP_AUDIO_OUT_SPEAKER);
    bsp_display_set_backlight(app_settings_get().backlight_pct);

    // ── Web UI / status API transport ───────────────────────────────────────
    // Wi-Fi remote (ESP-Hosted SoftAP + web UI) is user-controlled from the
    // Settings tab and defaults to OFF. wifi_link_init() only prepares state;
    // the AP + web server come up asynchronously when the saved setting is on
    // (or when the user flips the Settings switch), so boot is never blocked on
    // the SDIO/C6 bring-up.
    esp_err_t wifi_rc = wifi_link_init();
    if (wifi_rc != ESP_OK) {
        ESP_LOGW(TAG, "wifi_link_init: %s", esp_err_to_name(wifi_rc));
    }
    // Let the Settings tab toggle the Wi-Fi remote without the UI depending on
    // the wifi_link/web_server components directly.
    ui_settings_set_wifi_toggle_cb(wifi_link_request_enable);
    ui_settings_set_s3_debug_ap_toggle_cb(on_s3_debug_ap_toggle);
    if (app_settings_get().wifi_remote) {
        ESP_LOGI(TAG, "Wi-Fi remote enabled in settings — starting web UI AP");
        wifi_link_request_enable(true);
    }

    // ── Media and audio ──────────────────────────────────────────────────────
    // library_init() returns NOT_FOUND when USB is not mounted — that is
    // normal at startup; the library will be re-initialised when USB connects.
    esp_err_t lib_rc = library_init();
    if (lib_rc != ESP_OK) {
        ESP_LOGW(TAG, "library_init: %s (USB not mounted yet — OK)", esp_err_to_name(lib_rc));
    }

    ESP_ERROR_CHECK(audio_engine_init());
    app_settings_t settings = app_settings_get();
    audio_engine_set_cue_mode(settings.cue_mode);
    audio_engine_set_master_trim(ui_settings_master_trim_gain(settings.master_trim_preset));

    // ── UI ───────────────────────────────────────────────────────────────────
    ESP_ERROR_CHECK(ui_init());

    // ── USB host (Rekordbox media drive) ─────────────────────────────────────
    // Starts the host + MSC stack; when a drive is plugged into the HS USB-C
    // port it mounts at /usb and on_usb_storage_event() loads the library.
    ESP_ERROR_CHECK(usb_storage_init(on_usb_storage_event));

    ESP_LOGI(TAG, "all subsystems ready — deck active, waiting for S3 events");
}
