#include "control_link.h"
#include "control_link_p4_diagnostics.h"
#include "deck_core.h"
#include "bsp_jc4880.h"
#include "library.h"
#include "audio_engine.h"
#if CONFIG_AUDIO_RECORDER_ENABLED
#include "audio_recorder.h"
#endif
#include "ui.h"
#include "ui_settings.h"
#include "usb_storage.h"
#include "app_settings.h"
#include "media_io_gate.h"
#include "wifi_link.h"
#include "p4_ota_pull.h"
#include "web_server.h"
#include "service_log.h"
#include "sdkconfig.h"
#if CONFIG_CONTROLLER_PROFILE_MANAGER
#include "controller_profile_manager.h"
#endif
#if CONFIG_MONITOR_PCM_LINK_ENABLED
#include "monitor_pcm_link.h"
#endif
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "firmware_health.h"
#include "p4_ota.h"

static const char *TAG = "main";

void p4_tcm_heap_guard_keep(void);

static void on_s3_debug_ap_toggle(bool enable)
{
    control_link_send_state(CTRL_ID_S3_DEBUG_AP, enable ? 1 : 0);
}

// Periodic health monitor (esp_timer task, not the audio path): reads the
// counters the audio engine already maintains and emits rate-limited service-log
// summaries for anomalies and low-memory edges. No hot-path work.
#define AUDIO_MON_PERIOD_US       (5ll * 1000000ll)
#define LOW_INTERNAL_HEAP_BYTES   (24u * 1024u)
#define LOW_PSRAM_BYTES           (256u * 1024u)

static void health_monitor_cb(void *arg)
{
    (void)arg;
    static uint32_t last_late = 0u, last_underrun = 0u, last_rate = 0u;
    static uint32_t last_link_crc = 0u, last_link_gap = 0u;
    static bool low_heap = false, low_psram = false;

    audio_engine_diagnostics_snapshot_t d;
    memset(&d, 0, sizeof(d));
    audio_engine_get_diagnostics_snapshot(&d);

    /* Coalesce the anomaly summaries. Emitting on every 5 s tick where a counter
     * moved produced long runs of records repeating an unchanged maximum — ten
     * in a row carrying the same a1 during one bad boot — which is noise, and
     * noise that costs microSD writes contending with the recorder on the same
     * card. Report immediately when the worst case actually grows, otherwise
     * accumulate and report at most once a minute. */
    static uint32_t last_late_max = 0u;
    static int64_t  late_report_us = 0, underrun_report_us = 0;
    const int64_t now_us = esp_timer_get_time();
    const int64_t QUIET_US = 60ll * 1000000ll;

    uint32_t underrun = d.pcm_underrun_count[0] + d.pcm_underrun_count[1];
    if (d.output_late_count > last_late) {
        bool worse = d.output_late_max_us > last_late_max;
        if (worse || late_report_us == 0 || (now_us - late_report_us) >= QUIET_US) {
            service_log_event(SERVICE_LOG_AUDIO_OUTPUT_LATE, SERVICE_LOG_WARN,
                              2u, d.output_late_count - last_late,
                              d.output_late_max_us, 0u, 0u, NULL);
            last_late = d.output_late_count;
            last_late_max = d.output_late_max_us;
            late_report_us = now_us;
        }
        /* else: leave last_late alone so the next report carries the full count */
    }
    if (underrun > last_underrun) {
        if (underrun_report_us == 0 || (now_us - underrun_report_us) >= QUIET_US) {
            service_log_event(SERVICE_LOG_AUDIO_UNDERRUN, SERVICE_LOG_WARN,
                              1u, underrun - last_underrun, 0u, 0u, 0u, NULL);
            last_underrun = underrun;
            underrun_report_us = now_us;
        }
    }
    if (d.output_sample_rate != 0u && d.output_sample_rate != last_rate) {
        service_log_event(SERVICE_LOG_AUDIO_RATE_CHANGED, SERVICE_LOG_INFO,
                          1u, d.output_sample_rate, 0u, 0u, 0u, NULL);
        last_rate = d.output_sample_rate;
    }

    /* S3 control-link presence is derived from heartbeat age; log the edges. */
    static int last_link = -1;   /* -1 unknown, 0 offline, 1 online */
    deck_state_t ds = deck_core_get_deck_state(CTRL_DECK_1);
    int link = ds.control_link_connected ? 1 : 0;
    if (link != last_link) {
        service_log_event(link ? SERVICE_LOG_CONTROL_LINK_ONLINE
                               : SERVICE_LOG_CONTROL_LINK_OFFLINE,
                          link ? SERVICE_LOG_INFO : SERVICE_LOG_WARN,
                          1u, ds.last_heartbeat_age_ms, 0u, 0u, 0u, NULL);
        last_link = link;
    }

    control_link_rx_stats_t link_stats;
    control_link_get_rx_stats(&link_stats);
    uint32_t link_crc = link_stats.event_checksum_errors +
                        link_stats.bulk_crc_errors;
    if (link_crc != last_link_crc) {
        service_log_event(SERVICE_LOG_CONTROL_LINK_CRC, SERVICE_LOG_WARN,
                          4u, link_crc - last_link_crc, link_crc,
                          link_stats.event_checksum_errors,
                          link_stats.bulk_crc_errors,
                          "event checksum + bulk crc/format");
        last_link_crc = link_crc;
    }
    if (link_stats.sequence_gaps != last_link_gap) {
        service_log_event(SERVICE_LOG_CONTROL_LINK_GAP, SERVICE_LOG_WARN,
                          3u, link_stats.sequence_gaps - last_link_gap,
                          link_stats.sequence_gaps,
                          link_stats.last_sequence, 0u, NULL);
        last_link_gap = link_stats.sequence_gaps;
    }

    size_t heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (heap_free < LOW_INTERNAL_HEAP_BYTES && !low_heap) {
        service_log_event(SERVICE_LOG_LOW_INTERNAL_HEAP, SERVICE_LOG_WARN,
                          1u, (uint32_t)heap_free, 0u, 0u, 0u, NULL);
        low_heap = true;
    } else if (heap_free >= LOW_INTERNAL_HEAP_BYTES * 2u) {
        low_heap = false;
    }
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psram_free < LOW_PSRAM_BYTES && !low_psram) {
        service_log_event(SERVICE_LOG_LOW_PSRAM, SERVICE_LOG_WARN,
                          1u, (uint32_t)psram_free, 0u, 0u, 0u, NULL);
        low_psram = true;
    } else if (psram_free >= LOW_PSRAM_BYTES * 2u) {
        low_psram = false;
    }
}

static const char *reset_reason_str(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    default:                return "OTHER";
    }
}

/* Adapters between the web layer and wifi_link. Thin on purpose: the only job
 * is to keep the two components from depending on each other. */
static int app_probe_start(int mode, const char *arg)
{
    /* 1 = check the update channel, 0 = prove the link only. Both make the
     * same AP->STA->AP round trip; the check adds one HTTPS GET. */
    if (mode == 2) return (int)p4_ota_pull_install_start(arg);
    return mode == 1 ? (int)p4_ota_pull_check_start()
                     : (int)wifi_link_probe_start();
}

static void app_probe_status(web_server_probe_status_t *out)
{
    if (!out) return;

    /* Whichever ran more recently is what the operator wants to see. The
     * update check is reported in preference because it is the operation with
     * something to say beyond "the link works". */
    p4_ota_pull_status_t chk = p4_ota_pull_get_status();
    if (chk.state != P4_OTA_PULL_IDLE) {
        switch (chk.state) {
        case P4_OTA_PULL_CHECKING:    out->state = 1; break;
        case P4_OTA_PULL_UP_TO_DATE:  out->state = 2; break;
        case P4_OTA_PULL_AVAILABLE:   out->state = 2; break;
        case P4_OTA_PULL_DOWNLOADING: out->state = 1; break;
        case P4_OTA_PULL_READY_TO_REBOOT: out->state = 2; break;
        case P4_OTA_PULL_FAILED:      out->state = 3; break;
        default:                      out->state = 0; break;
        }
        if (chk.state == P4_OTA_PULL_DOWNLOADING && chk.available_size > 0u) {
            snprintf(out->detail, sizeof(out->detail), "%s %u%%", chk.detail,
                     (unsigned)((uint64_t)chk.downloaded * 100u / chk.available_size));
        } else if (chk.state == P4_OTA_PULL_AVAILABLE) {
            snprintf(out->detail, sizeof(out->detail), "update available: %s",
                     chk.available_release);
        } else {
            snprintf(out->detail, sizeof(out->detail), "%s", chk.detail);
        }
        out->address[0] = '\0';
        return;
    }

    wifi_link_probe_status_t s = wifi_link_probe_status();
    out->state = (int)s.state;
    snprintf(out->detail, sizeof(out->detail), "%s", s.detail);
    snprintf(out->address, sizeof(out->address), "%s", s.address);
}

#if CONFIG_AUDIO_RECORDER_ENABLED
// Settings RECORD button -> master-output microSD recorder. Recording taps the
// exact post-limiter MAIN block, so it needs an established output rate.
static bool on_recording_toggle(bool enable)
{
    if (enable) {
        uint32_t rate = audio_engine_get_output_sample_rate();
        if (rate == 0u) {
            ESP_LOGW(TAG, "recorder: no output rate yet (load and play a track first)");
            return false;
        }
        return audio_recorder_start(rate) == ESP_OK;
    }
    return audio_recorder_stop() == ESP_OK;
}
#endif  /* CONFIG_AUDIO_RECORDER_ENABLED */

#if CONFIG_CONTROLLER_PROFILE_MANAGER
// S3 reports the connected controller over the 0xA6 bulk layer; the profile
// manager matches it against the SD/TF registry.
static void on_controller_descriptor(const ctrl_descriptor_report_t *rep)
{
    (void)controller_profile_manager_on_descriptor_report(rep->vid, rep->pid,
                                                          rep->caps, rep->product,
                                                          rep->connection_epoch);
}

static void on_controller_connection_state(bool connected)
{
    if (!connected) {
        /* Retire the S3 runtime immediately; registry disconnect alone only
         * clears P4 bookkeeping and would leave the old mapper eligible. */
        (void)control_link_send_profile_simple(CTRL_BULK_TYPE_PROFILE_CLEAR);
        (void)controller_profile_manager_on_disconnect();
    }
}
#endif

// Called from the USB storage task when the Rekordbox drive mounts/unmounts.
static void on_usb_storage_event(bool mounted)
{
    if (mounted) {
        service_log_note(SERVICE_LOG_USB_MOUNTED, SERVICE_LOG_INFO, "rekordbox drive");
        esp_err_t rc = library_init();   // open export.pdb, build the track index
        if (rc == ESP_OK) {
            ESP_LOGW(TAG, "USB media library loaded: %d tracks", library_count());
            service_log_event(SERVICE_LOG_LIBRARY_LOADED, SERVICE_LOG_INFO,
                              1u, (uint32_t)library_count(), 0u, 0u, 0u, NULL);
        } else {
            ESP_LOGW(TAG, "library_init after USB mount: %s", esp_err_to_name(rc));
            service_log_event(SERVICE_LOG_LIBRARY_LOAD_FAILED, SERVICE_LOG_WARN,
                              1u, (uint32_t)rc, 0u, 0u, 0u, esp_err_to_name(rc));
        }
        ui_trigger_library_refresh();            // safely schedule table repopulation in the LVGL task context
    } else {
        ESP_LOGW(TAG, "USB drive removed");
        service_log_note(SERVICE_LOG_USB_UNMOUNTED, SERVICE_LOG_INFO, "drive removed");
        esp_err_t stop_rc = audio_engine_suspend_loads_and_stop_all();
        bool owns_load_barrier = stop_rc == ESP_OK;
        if (stop_rc != ESP_OK) {
            ESP_LOGE(TAG, "audio_engine_stop on USB removal: %s", esp_err_to_name(stop_rc));
        }
        library_clear();
        esp_err_t clear_rc =
            deck_core_clear_loaded_tracks(library_generation());
        if (clear_rc != ESP_OK) {
            ESP_LOGW(TAG, "deck loaded-track clear on USB removal: %s",
                     esp_err_to_name(clear_rc));
        }
        ui_notify_usb_removed();
        ui_trigger_library_refresh();
        if (owns_load_barrier) {
            audio_engine_resume_loads();
        }
    }
}

void app_main(void)
{
    p4_tcm_heap_guard_keep();
    ESP_ERROR_CHECK(bsp_audio_force_safe_boot_state());
    ESP_ERROR_CHECK(firmware_health_init());
    ESP_ERROR_CHECK(p4_ota_init());

    ESP_LOGI(TAG, "Pajoniiir P4 main deck firmware starting");
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

    // ── Board support (stubs until hardware arrives) ─────────────────────────
    ESP_ERROR_CHECK(bsp_display_init());
    ESP_ERROR_CHECK(bsp_touch_init());
    ESP_ERROR_CHECK(bsp_audio_init());
    ESP_ERROR_CHECK(bsp_sd_init());

    // ── Structured microSD service journal ───────────────────────────────────
    {
        firmware_health_info_t fh;
        const char *ver = "n/a", *part = "n/a";
        if (firmware_health_get_info(&fh) == ESP_OK) {
            ver = fh.version;
            part = fh.partition_label;
        }
        service_log_init(ver, part, reset_reason_str());
        service_log_note(SERVICE_LOG_SD_MOUNTED, SERVICE_LOG_INFO, "/sd ready");
        service_log_event(SERVICE_LOG_RESET_REASON, SERVICE_LOG_INFO,
                          1u, (uint32_t)esp_reset_reason(), 0u, 0u, 0u,
                          reset_reason_str());
    }

#if CONFIG_CONTROLLER_PROFILE_MANAGER
    // Controller profiles live on the SD/TF card; a missing directory is
    // normal (no profiles yet) and must not block boot.
    ESP_ERROR_CHECK(controller_profile_manager_init());
    esp_err_t profile_rc = controller_profile_manager_scan_storage();
    if (profile_rc != ESP_OK && profile_rc != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "controller profile scan: %s", esp_err_to_name(profile_rc));
    }
#endif

    // Restore only the product-safe RCA route. app_settings_init() migrates
    // legacy speaker selections before this point.
    ESP_ERROR_CHECK(bsp_audio_set_output(BSP_AUDIO_OUT_RCA));
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
    // ── Media and audio ──────────────────────────────────────────────────────
    // library_init() returns NOT_FOUND when USB is not mounted — that is
    // normal at startup; the library will be re-initialised when USB connects.
    esp_err_t lib_rc = library_init();
    if (lib_rc != ESP_OK) {
        ESP_LOGW(TAG, "library_init: %s (USB not mounted yet — OK)", esp_err_to_name(lib_rc));
    }

    ESP_ERROR_CHECK(audio_engine_init());
#if CONFIG_AUDIO_RECORDER_ENABLED
    /* Prepare the recorder early so any crash-orphaned .part files on the SD
     * card are recovered before a new session starts. */
    audio_recorder_init();
#endif

    /* Periodic health monitor -> rate-limited service-log anomaly summaries. */
    {
        const esp_timer_create_args_t mon_args = {
            .callback = health_monitor_cb, .name = "svclog_mon"
        };
        esp_timer_handle_t mon = NULL;
        if (esp_timer_create(&mon_args, &mon) == ESP_OK) {
            esp_timer_start_periodic(mon, AUDIO_MON_PERIOD_US);
        }
    }
    app_settings_t settings = app_settings_get();
    audio_engine_set_cue_mode(settings.cue_mode);
    audio_engine_set_master_trim(ui_settings_master_trim_gain(settings.master_trim_preset));

    // ── Authoritative deck state ─────────────────────────────────────────────
    // Build the playback queue before constructing the UI, but delay the S3
    // UART consumer until every event target (profiles, audio and UI) is ready.
    QueueHandle_t ctrl_queue;
    ESP_ERROR_CHECK(deck_core_init(&ctrl_queue));

    // ── UI ───────────────────────────────────────────────────────────────────
    ESP_ERROR_CHECK(ui_init());

    // ── External control producers ───────────────────────────────────────────
    // From this point onward incoming S3 frames may update deck/UI state.
    deck_core_set_s3_debug_ap_status_cb(ui_settings_set_s3_debug_ap_status);
    deck_core_set_s3_debug_ap_token_cb(ui_settings_set_s3_debug_ap_token);
#if CONFIG_CONTROLLER_PROFILE_MANAGER
    control_link_set_descriptor_report_cb(on_controller_descriptor);
    control_link_set_controller_state_cb(on_controller_connection_state);
#endif
    ESP_ERROR_CHECK(control_link_init(ctrl_queue));
    control_link_send_state(CTRL_ID_S3_DEBUG_AP, 0);

    // Settings callbacks are published only after their downstream services
    // exist.  A saved Wi-Fi setting is likewise activated after the UI and
    // playback state are fully initialized, so web requests cannot race boot.
    /* Every controller/UI event resets the idle screensaver, and one that
     * wakes it is consumed there rather than acted on. */
    /* wifi_link already owns web_server's lifetime, so the web layer reaches
     * the connectivity probe through here rather than calling back into it. */
    web_server_set_probe_hooks(app_probe_start, app_probe_status);

    deck_core_set_activity_cb(ui_activity_notice);
    ui_settings_set_wifi_toggle_cb(wifi_link_request_enable);
#if CONFIG_AUDIO_RECORDER_ENABLED
    ui_settings_set_recording_toggle_cb(on_recording_toggle);
#endif
    ui_settings_set_s3_debug_ap_toggle_cb(on_s3_debug_ap_toggle);
    if (app_settings_get().wifi_remote) {
        ESP_LOGI(TAG, "Wi-Fi remote enabled in settings — starting web UI AP");
        wifi_link_request_enable(true);
    }

    // ── USB host (Rekordbox media drive) ─────────────────────────────────────
    // Starts the host + MSC stack; when a drive is plugged into the HS USB-C
    // port it mounts at /usb and on_usb_storage_event() loads the library.
    ESP_ERROR_CHECK(usb_storage_init(on_usb_storage_event));

    ESP_LOGI(TAG, "all subsystems ready — deck active, waiting for S3 events");
    ESP_ERROR_CHECK(firmware_health_mark_ready());
}
