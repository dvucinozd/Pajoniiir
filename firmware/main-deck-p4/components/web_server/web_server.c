#include "web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "audio_engine.h"
#include "media_catalog.h"
#include "ui.h"
#include "ui_library.h"
#include "web_api_helpers.h"
#include "deck_core.h"
#include "control_link.h"
#include "sdkconfig.h"
#if CONFIG_CONTROLLER_PROFILE_MANAGER
#include "controller_profile_manager.h"
#endif
#include <stdlib.h>
#include <string.h>

static const char *TAG = "web_server";
static httpd_handle_t s_web_server = NULL;

#if CONFIG_CONTROLLER_PROFILE_MANAGER
static const char *controller_profile_state_name(controller_profile_transfer_state_t state)
{
    switch (state) {
    case CPM_TRANSFER_IDLE:
        return "idle";
    case CPM_TRANSFER_MATCHED:
        return "matched";
    case CPM_TRANSFER_TRANSFERRING:
        return "transferring";
    case CPM_TRANSFER_ACTIVE:
        return "active";
    case CPM_TRANSFER_FAILED:
        return "failed";
    case CPM_TRANSFER_UNSUPPORTED:
        return "unsupported";
    default:
        return "unknown";
    }
}
#endif

static esp_err_t register_uri_or_stop(httpd_handle_t server, const httpd_uri_t *uri)
{
    esp_err_t rc = httpd_register_uri_handler(server, uri);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "URI handler registration failed for %s: %s",
                 uri && uri->uri ? uri->uri : "(null)",
                 esp_err_to_name(rc));
        httpd_stop(server);
        s_web_server = NULL;
    }
    return rc;
}

// Simboli za ugrađene datoteke
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");

// Provjera i preusmjeravanje za Captive Portal
static bool redirect_if_needed(httpd_req_t *req)
{
    char host[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK) {
        if (strstr(host, "192.168.4.1") == NULL) {
            ESP_LOGD(TAG, "Redirecting Host '%s' to 192.168.4.1", host);
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/index.html");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, NULL, 0);
            return true;
        }
    }
    return false;
}

// GET / i /index.html
static esp_err_t index_html_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET index.html: %s", req->uri);
    if (redirect_if_needed(req)) {
        return ESP_OK;
    }
    size_t size = strlen((const char *)index_html_start);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, (const char *)index_html_start, size);
}

// GET /style.css
static esp_err_t style_css_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET style.css: %s", req->uri);
    size_t size = strlen((const char *)style_css_start);
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, (const char *)style_css_start, size);
}

// GET /app.js
static esp_err_t app_js_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET app.js: %s", req->uri);
    size_t size = strlen((const char *)app_js_start);
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, (const char *)app_js_start, size);
}

// GET /api/status
static esp_err_t api_status_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET /api/status: %s", req->uri);
    audio_engine_deck_status_t deck1 = {0};
    audio_engine_deck_status_t deck2 = {0};
    audio_engine_mixer_snapshot_t mixer = {0};
    audio_engine_diagnostics_snapshot_t diagnostics = {0};

    audio_engine_deck_get_status(0, &deck1);
    audio_engine_deck_get_status(1, &deck2);
    audio_engine_get_mixer_snapshot(&mixer);
    audio_engine_get_diagnostics_snapshot(&diagnostics);

    char title1[64] = {0};
    char artist1[64] = {0};
    char title2[64] = {0};
    char artist2[64] = {0};
    uint16_t bpm1_val = 0;
    uint16_t bpm2_val = 0;
    uint32_t duration1_ms = 0;
    uint32_t duration2_ms = 0;

    ui_get_deck_track_info(0, title1, sizeof(title1), artist1, sizeof(artist1), &bpm1_val, &duration1_ms);
    ui_get_deck_track_info(1, title2, sizeof(title2), artist2, sizeof(artist2), &bpm2_val, &duration2_ms);

    char title1_esc[128] = {0};
    char artist1_esc[128] = {0};
    char title2_esc[128] = {0};
    char artist2_esc[128] = {0};
    web_api_json_escape(title1, title1_esc, sizeof(title1_esc));
    web_api_json_escape(artist1, artist1_esc, sizeof(artist1_esc));
    web_api_json_escape(title2, title2_esc, sizeof(title2_esc));
    web_api_json_escape(artist2, artist2_esc, sizeof(artist2_esc));

    const char *state_text1 = "IDLE";
    if (deck1.state == AE_LOADING) state_text1 = "LOADING";
    else if (deck1.state == AE_READY) state_text1 = "READY";
    else if (deck1.state == AE_PLAYING) state_text1 = "PLAYING";
    else if (deck1.state == AE_ERROR) state_text1 = "ERROR";

    const char *state_text2 = "IDLE";
    if (deck2.state == AE_LOADING) state_text2 = "LOADING";
    else if (deck2.state == AE_READY) state_text2 = "READY";
    else if (deck2.state == AE_PLAYING) state_text2 = "PLAYING";
    else if (deck2.state == AE_ERROR) state_text2 = "ERROR";

    deck_state_t state1 = deck_core_get_deck_state(0);
    deck_state_t state2 = deck_core_get_deck_state(1);
    deck_core_beat_fx_state_t beat_fx = deck_core_get_beat_fx_state();

    float p1 = deck_core_pitch_percent(&state1);
    float p2 = deck_core_pitch_percent(&state2);

    uint32_t current_bpm1 = bpm1_val * (1.0f + p1 / 100.0f);
    uint32_t current_bpm2 = bpm2_val * (1.0f + p2 / 100.0f);
    char beat_fx_json[128] = {0};
    web_api_format_beat_fx_json(beat_fx_json,
                                sizeof(beat_fx_json),
                                (int)beat_fx.effect,
                                (int)beat_fx.beat,
                                (int)beat_fx.target,
                                (unsigned)beat_fx.depth,
                                beat_fx.enabled);
    char beat_fx_echo_diag_json[160] = {0};
    web_api_format_beat_fx_echo_diag_json(beat_fx_echo_diag_json,
                                          sizeof(beat_fx_echo_diag_json),
                                          diagnostics.beat_fx_echo_allocated[0],
                                          diagnostics.beat_fx_echo_allocated[1],
                                          diagnostics.beat_fx_echo_enabled[0],
                                          diagnostics.beat_fx_echo_enabled[1],
                                          (unsigned)diagnostics.beat_fx_echo_delay_ms[0],
                                          (unsigned)diagnostics.beat_fx_echo_delay_ms[1]);

    char controller_json[256] = {0};
#if CONFIG_CONTROLLER_PROFILE_MANAGER
    {
        const controller_profile_registry_t *reg =
            controller_profile_manager_get_registry();
        char product_esc[2 * CPM_PRODUCT_MAX + 8] = {0};
        web_api_json_escape(reg->connected_product, product_esc, sizeof(product_esc));
        const char *active = (reg->active_index >= 0 &&
                              reg->active_index < (int)reg->count)
            ? reg->profiles[reg->active_index].id : "";
        char active_esc[2 * CPM_ID_MAX + 8] = {0};
        web_api_json_escape(active, active_esc, sizeof(active_esc));
        char state_esc[24] = {0};
        web_api_json_escape(controller_profile_state_name(reg->transfer_state),
                            state_esc, sizeof(state_esc));
        web_api_format_controller_json(controller_json, sizeof(controller_json),
                                       reg->controller_present,
                                       reg->connected_vid, reg->connected_pid,
                                       product_esc,
                                       (reg->connected_caps & CTRL_DESC_CAP_MIDI_IN) != 0,
                                       (reg->connected_caps & CTRL_DESC_CAP_MIDI_OUT) != 0,
                                       (reg->connected_caps & CTRL_DESC_CAP_USB_AUDIO) != 0,
                                       active_esc, state_esc, reg->count);
    }
#else
    web_api_format_controller_json(controller_json, sizeof(controller_json),
                                   false, 0, 0, "", false, false, false, "", "idle", 0);
#endif

    char *json = NULL;
    int json_len = web_api_alloc_printf(
        &json,
        "{"
        "\"deck1\":{"
        "\"title\":\"%s\","
        "\"artist\":\"%s\","
        "\"bpm\":%u,"
        "\"pitch_percent\":%.2f,"
        "\"raw_pitch\":%d,"
        "\"position_ms\":%u,"
        "\"duration_ms\":%u,"
        "\"playing\":%s,"
        "\"state_text\":\"%s\""
        "},"
        "\"deck2\":{"
        "\"title\":\"%s\","
        "\"artist\":\"%s\","
        "\"bpm\":%u,"
        "\"pitch_percent\":%.2f,"
        "\"raw_pitch\":%d,"
        "\"position_ms\":%u,"
        "\"duration_ms\":%u,"
        "\"playing\":%s,"
        "\"state_text\":\"%s\""
        "},"
        "\"mixer\":{"
        "\"volume1\":%u,"
        "\"volume2\":%u,"
        "\"crossfader\":%u,"
        "\"master_volume\":%u,"
        "\"headphone_mix\":%u,"
        "\"pregain1\":%u,"
        "\"pregain2\":%u,"
        "\"pregain_gain1\":%.3f,"
        "\"pregain_gain2\":%.3f,"
        "\"eq1_low\":%u,"
        "\"eq1_mid\":%u,"
        "\"eq1_high\":%u,"
        "\"eq2_low\":%u,"
        "\"eq2_mid\":%u,"
        "\"eq2_high\":%u,"
        "\"filter1\":%u,"
        "\"filter2\":%u,"
        "\"smart_cfx\":%s,"
        "\"smart_fader\":%s,"
        "\"pfl1\":%s,"
        "\"pfl2\":%s"
        "},"
        "%s,"
        "%s,"
        "\"diagnostics\":{"
        "\"output_codec_open\":%s,"
        "\"output_sample_rate\":%u,"
        "\"output_late_count\":%u,"
        "\"output_late_max_us\":%u,"
        "\"output_late_threshold_us\":%u,"
        "\"ring_capacity\":%u,"
        "\"ring_used1\":%u,"
        "\"ring_used2\":%u,"
        "\"deck_sample_rate1\":%u,"
        "\"deck_sample_rate2\":%u,"
        "\"deck_channels1\":%u,"
        "\"deck_channels2\":%u,"
        "\"deck_file_bytes1\":%u,"
        "\"deck_file_bytes2\":%u,"
        "\"deck_load_progress1\":%u,"
        "\"deck_load_progress2\":%u,"
        "\"deck_active1\":%s,"
        "\"deck_active2\":%s,"
        "\"limiter_samples\":%u,"
        "\"limiter_positive\":%u,"
        "\"limiter_negative\":%u,"
        "\"limiter_peak\":%d,"
        "\"usb_headphones\":{\"submitted_blocks\":%u,\"dropped_blocks\":%u,\"submitted_frames\":%u},"
        "%s,"
        "\"heap_free\":%u,"
        "\"internal_free\":%u,"
        "\"psram_free\":%u"
        "}"
        "}",
        title1_esc, artist1_esc, (unsigned)current_bpm1, p1, state1.pitch, (unsigned)state1.position_ms, (unsigned)duration1_ms, state1.playing ? "true" : "false", state_text1,
        title2_esc, artist2_esc, (unsigned)current_bpm2, p2, state2.pitch, (unsigned)state2.position_ms, (unsigned)duration2_ms, state2.playing ? "true" : "false", state_text2,
        mixer.channel_volume[0], mixer.channel_volume[1], mixer.crossfader,
        mixer.master_volume,
        mixer.headphone_mix,
        mixer.pregain[0], mixer.pregain[1],
        (double)mixer.pregain_gain[0], (double)mixer.pregain_gain[1],
        mixer.eq[0][AUDIO_EQ_BAND_LOW], mixer.eq[0][AUDIO_EQ_BAND_MID], mixer.eq[0][AUDIO_EQ_BAND_HIGH],
        mixer.eq[1][AUDIO_EQ_BAND_LOW], mixer.eq[1][AUDIO_EQ_BAND_MID], mixer.eq[1][AUDIO_EQ_BAND_HIGH],
        mixer.filter[0], mixer.filter[1],
        mixer.smart_cfx_enabled ? "true" : "false",
        mixer.smart_fader_enabled ? "true" : "false",
        mixer.pfl_enabled[0] ? "true" : "false", mixer.pfl_enabled[1] ? "true" : "false",
        beat_fx_json,
        controller_json,
        diagnostics.output_codec_open ? "true" : "false",
        (unsigned)diagnostics.output_sample_rate,
        (unsigned)diagnostics.output_late_count,
        (unsigned)diagnostics.output_late_max_us,
        (unsigned)diagnostics.output_late_threshold_us,
        (unsigned)diagnostics.ring_capacity,
        (unsigned)diagnostics.ring_used[0],
        (unsigned)diagnostics.ring_used[1],
        (unsigned)diagnostics.deck_sample_rate[0],
        (unsigned)diagnostics.deck_sample_rate[1],
        (unsigned)diagnostics.deck_channels[0],
        (unsigned)diagnostics.deck_channels[1],
        (unsigned)diagnostics.deck_file_bytes[0],
        (unsigned)diagnostics.deck_file_bytes[1],
        (unsigned)diagnostics.deck_load_progress[0],
        (unsigned)diagnostics.deck_load_progress[1],
        diagnostics.deck_active[0] ? "true" : "false",
        diagnostics.deck_active[1] ? "true" : "false",
        (unsigned)diagnostics.limiter.limited_samples,
        (unsigned)diagnostics.limiter.positive_overloads,
        (unsigned)diagnostics.limiter.negative_overloads,
        (int)diagnostics.limiter.peak_input_abs,
        (unsigned)diagnostics.usb_headphone_submitted_blocks,
        (unsigned)diagnostics.usb_headphone_dropped_blocks,
        (unsigned)diagnostics.usb_headphone_submitted_frames,
        beat_fx_echo_diag_json,
        (unsigned)diagnostics.heap_free,
        (unsigned)diagnostics.internal_free,
        (unsigned)diagnostics.psram_free);
    if (!json || json_len < 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json, (size_t)json_len);
    free(json);
    return ESP_OK;
}

// GET /api/library
static esp_err_t api_library_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET /api/library: %s", req->uri);
    int count = media_catalog_count();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // Pošalji početak JSON-a
    const char *header = "{\"tracks\":[";
    httpd_resp_send_chunk(req, header, strlen(header));

    // Alociramo manji buffer u RAM-u za chunkove
    size_t chunk_sz = 4096;
    char *chunk = malloc(chunk_sz);
    if (!chunk) {
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_ERR_NO_MEM;
    }

    int chunk_len = 0;
    bool first = true;

    for (int i = 0; i < count; i++) {
        media_catalog_row_t row;
        if (media_catalog_get_row(i, &row) == ESP_OK) {
            char title_esc[256];
            char artist_esc[256];
            char item[768];
            web_api_json_escape(row.title, title_esc, sizeof(title_esc));
            web_api_json_escape(row.artist, artist_esc, sizeof(artist_esc));
            int item_len = snprintf(item, sizeof(item),
                                    "%s{\"index\":%d,\"title\":\"%s\",\"artist\":\"%s\",\"bpm\":%u,\"duration_ms\":%u}",
                                    first ? "" : ",",
                                    i, title_esc, artist_esc, row.bpm, (unsigned)row.duration_ms);
            if (item_len < 0 || (size_t)item_len >= sizeof(item)) {
                ESP_LOGW(TAG, "Skipping oversized library JSON row index=%d", i);
                continue;
            }
            
            // Ako bi dodavanje ovog stavka premašilo sigurnosnu granicu chunka, pošalji trenutni chunk
            if (chunk_len + item_len >= chunk_sz - 10) {
                httpd_resp_send_chunk(req, chunk, chunk_len);
                chunk_len = 0;
            }
            
            memcpy(chunk + chunk_len, item, item_len);
            chunk_len += item_len;
            first = false;
        }
    }

    // Pošalji preostali dio chunka
    if (chunk_len > 0) {
        httpd_resp_send_chunk(req, chunk, chunk_len);
    }

    free(chunk);

    // Pošalji kraj JSON-a
    const char *footer = "]}";
    httpd_resp_send_chunk(req, footer, strlen(footer));

    // Pošalji prazan chunk za označavanje kraja prijenosa
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

// GET /api/control
static esp_err_t api_control_handler(httpd_req_t *req)
{
    char query[128] = {0};
    char deck_str[16] = {0};
    char action[32] = {0};
    char value_str[32] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "deck", deck_str, sizeof(deck_str));
        httpd_query_key_value(query, "action", action, sizeof(action));
        httpd_query_key_value(query, "value", value_str, sizeof(value_str));
    }

    uint8_t deck = (deck_str[0] == '2') ? CTRL_DECK_2 : CTRL_DECK_1;
    int value = atoi(value_str);

    ESP_LOGI(TAG, "Control action: deck=%d, action=%s, value=%d", deck, action, value);

    if (strcmp(action, "play_pause") == 0) {
        ctrl_event_t ev = {
            .type  = CTRL_EV_BUTTON,
            .id    = (deck == CTRL_DECK_2) ? CTRL_ID_DECK2_PLAY : CTRL_ID_DECK1_PLAY,
            .deck  = deck,
            .value = 1,
            .seq   = 0
        };
        deck_core_queue_event(&ev);
    } else if (strcmp(action, "cue") == 0) {
        ctrl_event_t ev = {
            .type  = CTRL_EV_BUTTON,
            .id    = (deck == CTRL_DECK_2) ? CTRL_ID_DECK2_CUE : CTRL_ID_DECK1_CUE,
            .deck  = deck,
            .value = 1,
            .seq   = 0
        };
        deck_core_queue_event(&ev);
    } else if (strcmp(action, "pfl") == 0) {
        ctrl_event_t ev = {
            .type  = CTRL_EV_BUTTON,
            .id    = (deck == CTRL_DECK_2) ? CTRL_ID_DECK2_PFL : CTRL_ID_DECK1_PFL,
            .deck  = deck,
            .value = 1,
            .seq   = 0
        };
        deck_core_queue_event(&ev);
    } else if (strcmp(action, "volume") == 0) {
        ctrl_event_t ev = {
            .type  = CTRL_EV_BUTTON,
            .id    = (deck == CTRL_DECK_2) ? CTRL_ID_CH2_VOLUME : CTRL_ID_CH1_VOLUME,
            .deck  = deck,
            .value = (int16_t)value,
            .seq   = 0
        };
        deck_core_queue_event(&ev);
    } else if (strcmp(action, "crossfader") == 0) {
        ctrl_event_t ev = {
            .type  = CTRL_EV_BUTTON,
            .id    = CTRL_ID_CROSSFADER,
            .deck  = CTRL_DECK_NONE,
            .value = (int16_t)value,
            .seq   = 0
        };
        deck_core_queue_event(&ev);
    } else if (strcmp(action, "pitch") == 0) {
        ctrl_event_t ev = {
            .type  = CTRL_EV_PITCH,
            .id    = (deck == CTRL_DECK_2) ? CTRL_ID_DECK2_TEMPO : CTRL_ID_DECK1_TEMPO,
            .deck  = deck,
            .value = (int16_t)value,
            .seq   = 0
        };
        deck_core_queue_event(&ev);
    } else if (strcmp(action, "loop_4") == 0) {
        audio_engine_deck_status_t status = {0};
        esp_err_t rc = audio_engine_deck_get_status(deck, &status);
        if (rc != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid deck");
            return ESP_FAIL;
        }
        uint32_t pos = status.position_ms;
        uint16_t bpm = ui_library_deck_bpm(deck, 120);
        if (bpm == 0) {
            bpm = 120;
        }
        uint32_t beat_len_ms = 60000u / bpm;
        uint32_t loop_len_ms = 4u * beat_len_ms;
        rc = audio_engine_deck_set_loop(deck, pos, pos + loop_len_ms);
        if (rc != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Loop failed");
            return ESP_FAIL;
        }
    } else if (strcmp(action, "loop_clear") == 0) {
        esp_err_t rc = audio_engine_deck_clear_loop(deck);
        if (rc != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Loop clear failed");
            return ESP_FAIL;
        }
    } else if (strcmp(action, "seek") == 0) {
        audio_engine_deck_status_t status = {0};
        if (audio_engine_deck_get_status(deck, &status) != ESP_OK ||
            !status.loaded ||
            status.state == AE_ERROR) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Deck not seekable");
            return ESP_FAIL;
        }
        uint32_t duration_ms = 0;
        ui_get_deck_track_info(deck, NULL, 0, NULL, 0, NULL, &duration_ms);
        uint32_t pos_ms = web_api_clamp_seek_ms(value, duration_ms, duration_ms > 0u);
        esp_err_t rc = audio_engine_deck_seek(deck, pos_ms);
        if (rc != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Seek failed");
            return ESP_FAIL;
        }
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown action");
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// GET /api/load
static esp_err_t api_load_handler(httpd_req_t *req)
{
    char query[64] = {0};
    char index_str[16] = {0};
    char deck_str[16] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "index", index_str, sizeof(index_str));
        httpd_query_key_value(query, "deck", deck_str, sizeof(deck_str));
    }

    int index = atoi(index_str);
    uint8_t deck = (deck_str[0] == '2') ? CTRL_DECK_2 : CTRL_DECK_1;

    ESP_LOGI(TAG, "API Load Request: index=%d, deck=%d", index, deck);

    esp_err_t rc = ui_library_load_track_index_for_deck(index, deck);
    if (rc != ESP_OK) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        if (rc == ESP_ERR_INVALID_STATE) {
            httpd_resp_set_status(req, "409 Conflict");
            httpd_resp_send(req, "Load busy", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Load failed");
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// Catch-all handler za Captive Portal
static esp_err_t catch_all_handler(httpd_req_t *req)
{
    // Ako klijent traži bilo što, a mi smo u Captive Portal modu, preusmjeri na index.html
    ESP_LOGD(TAG, "Catch-all request: %s", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/index.html");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_web_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 5;
    config.stack_size = 8192;
    config.ctrl_port = 32768; // pomaknuto da ne bude u konfliktu
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 16;
    config.task_priority = 3;
    config.core_id = 0;

    // Pokreni poslužitelj
    ESP_LOGI(TAG, "Pokretanje HTTP poslužitelja na portu %d...", config.server_port);
    esp_err_t rc = httpd_start(&s_web_server, &config);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Ne mogu pokrenuti HTTP poslužitelj: %s", esp_err_to_name(rc));
        return rc;
    }

    // Registracija URI handlera
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_html_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &index_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t index_html_uri = {
        .uri = "/index.html*",
        .method = HTTP_GET,
        .handler = index_html_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &index_html_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t style_uri = {
        .uri = "/style.css*",
        .method = HTTP_GET,
        .handler = style_css_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &style_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t js_uri = {
        .uri = "/app.js*",
        .method = HTTP_GET,
        .handler = app_js_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &js_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t status_uri = {
        .uri = "/api/status*",
        .method = HTTP_GET,
        .handler = api_status_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &status_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t library_uri = {
        .uri = "/api/library*",
        .method = HTTP_GET,
        .handler = api_library_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &library_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t control_uri = {
        .uri = "/api/control*",
        .method = HTTP_GET,
        .handler = api_control_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &control_uri);
    if (rc != ESP_OK) return rc;

    httpd_uri_t load_uri = {
        .uri = "/api/load*",
        .method = HTTP_GET,
        .handler = api_load_handler,
        .user_ctx = NULL
    };
    rc = register_uri_or_stop(s_web_server, &load_uri);
    if (rc != ESP_OK) return rc;

    // Registracija catch-all za preusmjeravanje Captive Portala
    httpd_uri_t catch_all_uri = {
        .uri = "*",
        .method = HTTP_GET,
        .handler = catch_all_handler,
        .user_ctx = NULL
    };
    // Zbog poretka pretraživanja registrirat ćemo ga na kraju
    rc = register_uri_or_stop(s_web_server, &catch_all_uri);
    if (rc != ESP_OK) return rc;

    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_web_server != NULL) {
        httpd_stop(s_web_server);
        s_web_server = NULL;
    }
}
