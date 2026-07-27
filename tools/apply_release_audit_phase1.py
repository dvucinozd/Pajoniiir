#!/usr/bin/env python3
"""Apply deterministic release-audit fixes that touch large generated/monolithic files.

This script is intentionally assertion-heavy: it aborts instead of silently
producing a partial patch when an expected source shape changes.
"""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def load(rel: str) -> tuple[Path, str]:
    path = ROOT / rel
    return path, path.read_text(encoding="utf-8")


def save(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def replace_count(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)


def patch_ui_library() -> None:
    path, text = load("firmware/main-deck-p4/components/ui/ui_library.c")

    text = replace_once(
        text,
        """typedef struct {\n    int index;\n    uint8_t deck;\n    uint32_t generation;\n    media_catalog_track_t item;\n    media_loaded_track_t loaded;\n    esp_err_t rc;\n    char status[40];\n} ui_track_load_result_t;\n\ntypedef struct {\n    int index;\n    uint8_t deck;\n    uint32_t generation;\n} ui_track_load_request_t;\n""",
        """typedef struct {\n    int index;\n    uint8_t deck;\n    uint32_t generation;\n    uint32_t track_key;\n    bool deck_reset;\n    media_catalog_track_t item;\n    media_loaded_track_t loaded;\n    esp_err_t rc;\n    char status[40];\n} ui_track_load_result_t;\n\ntypedef struct {\n    int index;\n    uint8_t deck;\n    uint32_t generation;\n    uint32_t track_key;\n} ui_track_load_request_t;\n""",
        "ui load request/result identity",
    )

    helper_anchor = """}\n\n#ifndef WIN32\nstatic void ui_track_load_worker(void *arg)\n"""
    helper = """}\n\nstatic void ui_library_apply_empty_track(uint8_t deck)\n{\n    deck = ui_library_deck_index(deck);\n    s_loaded_media_valid[deck] = false;\n    memset(&s_loaded_media[deck], 0, sizeof(s_loaded_media[deck]));\n    s_deck_loaded_track_valid[deck] = false;\n    s_deck_loaded_track_key[deck] = 0u;\n    if (s_library_config.actions.clear_deck_track_info) {\n        s_library_config.actions.clear_deck_track_info(deck);\n    }\n    if (s_library_config.actions.set_deck_anlz) {\n        s_library_config.actions.set_deck_anlz(deck, NULL);\n    }\n    if (s_library_config.actions.load_waveform_data) {\n        s_library_config.actions.load_waveform_data(deck, 0u, NULL, false, NULL);\n    }\n    if (s_library_config.actions.set_loop_shadow) {\n        s_library_config.actions.set_loop_shadow(deck, false, 0u, 0u, 0);\n    }\n    if (deck == CTRL_DECK_1) {\n        ui_library_cache_invalidate();\n        ui_library_set_header_track(\"No Track\", \"\", 0u);\n    }\n    if (s_library_config.actions.update_hot_cues) {\n        s_library_config.actions.update_hot_cues();\n    }\n    if (s_library_table) {\n        lv_obj_invalidate(s_library_table);\n    }\n}\n\n#ifndef WIN32\nstatic void ui_track_load_worker(void *arg)\n"""
    text = replace_once(text, helper_anchor, helper, "empty-deck publication helper")

    old_worker = """    result->index = req.index;\n    result->deck = req.deck;\n    result->generation = req.generation;\n    result->rc = ESP_OK;\n\n    if (media_catalog_get(req.index, &result->item) != ESP_OK) {\n        result->rc = ESP_ERR_NOT_FOUND;\n        ui_track_load_set_status(result, \"NO TRACK\", \"NO TRACK\");\n    } else {\n        result->rc = media_catalog_load(req.index, &result->loaded);\n        if (result->rc != ESP_OK) {\n            ui_track_load_set_status(result, \"LOAD ERR\", \"LOAD ERR\");\n        } else {\n            if (req.deck == CTRL_DECK_1) {\n                (void)audio_engine_deck_clear_loop(req.deck);\n            }\n            deck_core_reset_deck(req.deck);\n            result->rc = audio_engine_deck_load(req.deck,\n                                                result->loaded.audio_path,\n                                                result->loaded.has_pvbr ? result->loaded.pvbr : NULL,\n                                                result->loaded.duration_ms);\n            if (result->rc != ESP_OK) {\n                audio_engine_deck_status_t deck_status = {0};\n                const char *audio_err = NULL;\n                if (audio_engine_deck_get_status(req.deck, &deck_status) == ESP_OK) {\n                    audio_err = deck_status.last_error_text;\n                }\n                ui_track_load_set_status(result, audio_err, \"AUDIO ERR\");\n            } else {\n                ui_track_load_set_status(result, \"TRACK LOADED\", \"TRACK LOADED\");\n            }\n        }\n    }\n"""
    new_worker = """    result->index = req.index;\n    result->deck = req.deck;\n    result->generation = req.generation;\n    result->track_key = req.track_key;\n    result->rc = media_catalog_load_by_identity(req.track_key,\n                                                 req.generation,\n                                                 &result->item,\n                                                 &result->loaded);\n    if (result->rc == ESP_ERR_INVALID_STATE) {\n        ui_track_load_set_status(result, \"LIBRARY CHANGED\", \"LIBRARY CHANGED\");\n    } else if (result->rc != ESP_OK) {\n        ui_track_load_set_status(result, \"LOAD ERR\", \"LOAD ERR\");\n    } else {\n        if (req.deck == CTRL_DECK_1) {\n            (void)audio_engine_deck_clear_loop(req.deck);\n        }\n        deck_core_reset_deck(req.deck);\n        result->deck_reset = true;\n        result->rc = audio_engine_deck_load(req.deck,\n                                            result->loaded.audio_path,\n                                            result->loaded.has_pvbr ? result->loaded.pvbr : NULL,\n                                            result->loaded.duration_ms);\n        if (result->rc != ESP_OK) {\n            audio_engine_deck_status_t deck_status = {0};\n            const char *audio_err = NULL;\n            if (audio_engine_deck_get_status(req.deck, &deck_status) == ESP_OK) {\n                audio_err = deck_status.last_error_text;\n            }\n            ui_track_load_set_status(result, audio_err, \"AUDIO ERR\");\n        } else {\n            ui_track_load_set_status(result, \"TRACK LOADED\", \"TRACK LOADED\");\n        }\n    }\n"""
    text = replace_once(text, old_worker, new_worker, "transactional UI load worker")

    text = replace_once(
        text,
        "static esp_err_t ui_submit_track_load(int index, uint8_t deck)\n",
        "static esp_err_t ui_submit_track_load(int index, uint32_t track_key, uint32_t generation, uint8_t deck)\n",
        "identity submit signature",
    )
    text = replace_once(
        text,
        """    req->index = index;\n    req->deck = deck;\n    req->generation = library_generation();\n""",
        """    req->index = index;\n    req->deck = deck;\n    req->generation = generation;\n    req->track_key = track_key;\n""",
        "identity submit fields",
    )

    old_selected = """    media_catalog_track_t item;\n    if (media_catalog_get(s_selected_track_idx, &item) != ESP_OK) {\n        ESP_LOGW(TAG, \"No catalog row at index %d\", s_selected_track_idx);\n        ui_library_set_load_busy(false, NULL);\n        ui_library_finish_track_load();\n        return;\n    }\n\n    ui_library_status_hold(\"LOADING\", COL_ACCENT, 1500);\n    (void)ui_submit_track_load(s_selected_track_idx, deck);\n"""
    new_selected = """    const uint32_t generation = media_catalog_generation();\n    media_catalog_track_t item;\n    if (media_catalog_get(s_selected_track_idx, &item) != ESP_OK ||\n        media_catalog_generation() != generation) {\n        ESP_LOGW(TAG, \"Catalog changed while resolving index %d\", s_selected_track_idx);\n        ui_library_set_load_busy(false, \"LIBRARY CHANGED\");\n        ui_library_finish_track_load();\n        return;\n    }\n\n    ui_library_status_hold(\"LOADING\", COL_ACCENT, 1500);\n    (void)ui_submit_track_load(s_selected_track_idx, item.track_key, generation, deck);\n"""
    text = replace_once(text, old_selected, new_selected, "selected load identity snapshot")

    old_stale = """        bool stale = result.generation != library_generation();\n        if (stale) {\n            ui_library_set_load_busy(false, \"USB REMOVED\");\n            ui_library_finish_track_load();\n            continue;\n        }\n"""
    new_stale = """        bool stale = result.generation != media_catalog_generation();\n        if (stale) {\n            if (result.deck_reset) {\n                ui_library_apply_empty_track(result.deck);\n            }\n            ui_library_status_hold(\"LIBRARY CHANGED\", COL_AMBER, 2500);\n            ui_library_set_load_busy(false, \"LIBRARY CHANGED\");\n            ui_library_finish_track_load();\n            continue;\n        }\n"""
    text = replace_once(text, old_stale, new_stale, "stale load handling")

    old_failure = """        if (result.rc != ESP_OK) {\n            const char *display = result.status[0] ? result.status : \"LOAD ERR\";\n            ESP_LOGW(TAG, \"track load worker failed index=%d: %s\", result.index, esp_err_to_name(result.rc));\n            ui_library_status_hold(display, ui_library_status_color_for_text(display), 3500);\n            ui_library_set_load_busy(false, display);\n            ui_library_finish_track_load();\n            continue;\n        }\n"""
    new_failure = """        if (result.rc != ESP_OK) {\n            const char *display = result.status[0] ? result.status : \"LOAD ERR\";\n            ESP_LOGW(TAG, \"track load worker failed key=0x%08x index=%d: %s\",\n                     (unsigned)result.track_key, result.index, esp_err_to_name(result.rc));\n            if (result.deck_reset) {\n                ui_library_apply_empty_track(result.deck);\n            }\n            ui_library_status_hold(display, ui_library_status_color_for_text(display), 3500);\n            ui_library_set_load_busy(false, display);\n            ui_library_finish_track_load();\n            continue;\n        }\n"""
    text = replace_once(text, old_failure, new_failure, "destructive failure UI cleanup")

    sort_anchor = """    if (!s_library_table) return;\n    uint32_t target_key = ui_library_selected_key();\n"""
    sort_guard = """    if (!s_library_table) return;\n#ifndef WIN32\n    if (s_track_load_busy || media_catalog_load_in_progress()) {\n        ui_library_status_hold(\"LOAD BUSY\", COL_AMBER, 1200);\n        return;\n    }\n#endif\n    uint32_t target_key = ui_library_selected_key();\n"""
    text = replace_count(text, sort_anchor, sort_guard, 4, "sort load guard")

    old_public = """esp_err_t ui_library_load_track_index_for_deck(int index, uint8_t deck)\n{\n#ifndef WIN32\n    if (index < 0 || index >= media_catalog_count()) {\n        return ESP_ERR_INVALID_ARG;\n    }\n    if (!ui_library_try_begin_track_load()) {\n        return ESP_ERR_INVALID_STATE;\n    }\n    /* This entry point is called from the web/httpd task, off the LVGL thread.\n     * ui_submit_track_load()'s error paths touch LVGL objects (status label,\n     * load buttons), so hold the LVGL lock across it — the on-screen load path\n     * already runs it under the same lock. */\n    ui_lvgl_lock();\n    esp_err_t rc = ui_submit_track_load(index, deck);\n    ui_lvgl_unlock();\n    if (rc != ESP_OK) {\n        return rc;\n    }\n    return ESP_OK;\n#else\n    if (index < 0 || index >= library_count()) {\n        return ESP_ERR_INVALID_ARG;\n    }\n    if (!ui_library_try_begin_track_load()) {\n        return ESP_ERR_INVALID_STATE;\n    }\n    mock_library_load_track_to_deck(index);\n    library_track_t *track = library_get_ptr(index);\n    if (track) {\n        library_load_anlz(track);\n        anlz_metadata_t meta_snapshot;\n        const anlz_metadata_t *meta = ui_library_clone_loaded_anlz(&meta_snapshot);\n        s_deck_loaded_track_key[deck] = track->track_id;\n        s_deck_loaded_track_valid[deck] = true;\n        if (s_library_table) {\n            lv_obj_invalidate(s_library_table);\n        }\n        ui_library_apply_loaded_track(deck,\n                                      track->title,\n                                      track->artist,\n                                      track->bpm,\n                                      track->duration_ms,\n                                      track->waveform_low,\n                                      track->has_waveform != 0,\n                                      meta);\n        anlz_free(&meta_snapshot);\n    }\n    ui_library_finish_track_load();\n    return ESP_OK;\n#endif\n}\n"""
    new_public = """esp_err_t ui_library_load_track_identity_for_deck(uint32_t track_key,\n                                                   uint32_t generation,\n                                                   uint8_t deck)\n{\n#ifndef WIN32\n    if (track_key == 0u || deck >= DECK_CORE_DECK_COUNT) {\n        return ESP_ERR_INVALID_ARG;\n    }\n    if (media_catalog_generation() != generation) {\n        return ESP_ERR_INVALID_STATE;\n    }\n\n    int resolved_index = -1;\n    const int count = media_catalog_count();\n    for (int index = 0; index < count; ++index) {\n        media_catalog_row_t row;\n        if (media_catalog_get_row(index, &row) == ESP_OK && row.track_key == track_key) {\n            resolved_index = index;\n            break;\n        }\n    }\n    if (media_catalog_generation() != generation) {\n        return ESP_ERR_INVALID_STATE;\n    }\n    if (resolved_index < 0) {\n        return ESP_ERR_NOT_FOUND;\n    }\n    if (!ui_library_try_begin_track_load()) {\n        return ESP_ERR_INVALID_STATE;\n    }\n\n    ui_lvgl_lock();\n    esp_err_t rc = ui_submit_track_load(resolved_index, track_key, generation, deck);\n    ui_lvgl_unlock();\n    return rc;\n#else\n    (void)generation;\n    for (int index = 0; index < library_count(); ++index) {\n        library_track_t *track = library_get_ptr(index);\n        if (track && track->track_id == track_key) {\n            return ui_library_load_track_index_for_deck(index, deck);\n        }\n    }\n    return ESP_ERR_NOT_FOUND;\n#endif\n}\n\nesp_err_t ui_library_load_track_index_for_deck(int index, uint8_t deck)\n{\n#ifndef WIN32\n    if (index < 0 || index >= media_catalog_count()) {\n        return ESP_ERR_INVALID_ARG;\n    }\n    const uint32_t generation = media_catalog_generation();\n    media_catalog_track_t item;\n    if (media_catalog_get(index, &item) != ESP_OK ||\n        media_catalog_generation() != generation) {\n        return ESP_ERR_INVALID_STATE;\n    }\n    return ui_library_load_track_identity_for_deck(item.track_key, generation, deck);\n#else\n    if (index < 0 || index >= library_count()) {\n        return ESP_ERR_INVALID_ARG;\n    }\n    if (!ui_library_try_begin_track_load()) {\n        return ESP_ERR_INVALID_STATE;\n    }\n    mock_library_load_track_to_deck(index);\n    library_track_t *track = library_get_ptr(index);\n    if (track) {\n        library_load_anlz(track);\n        anlz_metadata_t meta_snapshot;\n        const anlz_metadata_t *meta = ui_library_clone_loaded_anlz(&meta_snapshot);\n        s_deck_loaded_track_key[deck] = track->track_id;\n        s_deck_loaded_track_valid[deck] = true;\n        if (s_library_table) {\n            lv_obj_invalidate(s_library_table);\n        }\n        ui_library_apply_loaded_track(deck,\n                                      track->title,\n                                      track->artist,\n                                      track->bpm,\n                                      track->duration_ms,\n                                      track->waveform_low,\n                                      track->has_waveform != 0,\n                                      meta);\n        anlz_free(&meta_snapshot);\n    }\n    ui_library_finish_track_load();\n    return ESP_OK;\n#endif\n}\n"""
    text = replace_once(text, old_public, new_public, "public identity load API")

    save(path, text)


def patch_web_server() -> None:
    path, text = load("firmware/main-deck-p4/components/web_server/web_server.c")

    old_get_json = """    char json[APP_SETTINGS_OTA_SSID_CAP + APP_SETTINGS_OTA_URL_CAP + 192u];\n    int n = snprintf(json, sizeof(json),\n                     \"{\\\"ssid\\\":\\\"%s\\\",\\\"url\\\":\\\"%s\\\",\\\"has_password\\\":%s,\"\n                     \"\\\"probe\\\":{\\\"state\\\":\\\"%s\\\",\\\"detail\\\":\\\"%s\\\",\"\n                     \"\\\"address\\\":\\\"%s\\\"}}\",\n                     ssid, url,\n                     app_settings_ota_has_password() ? \"true\" : \"false\",\n                     probe_name, probe.detail, probe.address);\n"""
    new_get_json = """    char ssid_esc[APP_SETTINGS_OTA_SSID_CAP * 2u + 1u];\n    char url_esc[APP_SETTINGS_OTA_URL_CAP * 2u + 1u];\n    char detail_esc[sizeof(probe.detail) * 2u + 1u];\n    char address_esc[sizeof(probe.address) * 2u + 1u];\n    web_api_json_escape(ssid, ssid_esc, sizeof(ssid_esc));\n    web_api_json_escape(url, url_esc, sizeof(url_esc));\n    web_api_json_escape(probe.detail, detail_esc, sizeof(detail_esc));\n    web_api_json_escape(probe.address, address_esc, sizeof(address_esc));\n\n    char json[sizeof(ssid_esc) + sizeof(url_esc) + sizeof(detail_esc) + sizeof(address_esc) + 192u];\n    int n = snprintf(json, sizeof(json),\n                     \"{\\\"ssid\\\":\\\"%s\\\",\\\"url\\\":\\\"%s\\\",\\\"has_password\\\":%s,\"\n                     \"\\\"probe\\\":{\\\"state\\\":\\\"%s\\\",\\\"detail\\\":\\\"%s\\\",\"\n                     \"\\\"address\\\":\\\"%s\\\"}}\",\n                     ssid_esc, url_esc,\n                     app_settings_ota_has_password() ? \"true\" : \"false\",\n                     probe_name, detail_esc, address_esc);\n"""
    text = replace_once(text, old_get_json, new_get_json, "OTA GET JSON escaping")

    old_recv = """    int got = ota_http_recv(req, (uint8_t *)body, (size_t)req->content_len);\n    if (got <= 0) {\n        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, \"Body read failed\");\n    }\n    size_t len = (size_t)got;\n"""
    new_recv = """    size_t len = 0u;\n    const size_t wanted = (size_t)req->content_len;\n    while (len < wanted) {\n        int got = ota_http_recv(req, (uint8_t *)body + len, wanted - len);\n        if (got <= 0) {\n            memset(body, 0, sizeof(body));\n            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, \"Body read failed\");\n        }\n        len += (size_t)got;\n    }\n    body[len] = '\\0';\n"""
    text = replace_once(text, old_recv, new_recv, "OTA POST complete-body receive")

    old_library_header = """    // Pošalji početak JSON-a\n    const char *header = \"{\\\"tracks\\\":[\";\n    esp_err_t send_rc = httpd_resp_send_chunk(req, header, strlen(header));\n"""
    new_library_header = """    // Publish one catalog generation for every row in this response.\n    const uint32_t generation = media_catalog_generation();\n    char header[64];\n    int header_len = snprintf(header, sizeof(header),\n                              \"{\\\"generation\\\":%u,\\\"tracks\\\":[\",\n                              (unsigned)generation);\n    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {\n        free(chunk);\n        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, \"encode\");\n    }\n    esp_err_t send_rc = httpd_resp_send_chunk(req, header, (size_t)header_len);\n"""
    text = replace_once(text, old_library_header, new_library_header, "library generation header")

    old_item = """            int item_len = snprintf(item, sizeof(item),\n                                    \"%s{\\\"index\\\":%d,\\\"title\\\":\\\"%s\\\",\\\"artist\\\":\\\"%s\\\",\\\"bpm\\\":%u,\\\"duration_ms\\\":%u}\",\n                                    first ? \"\" : \",\",\n                                    i, title_esc, artist_esc, row.bpm, (unsigned)row.duration_ms);\n"""
    new_item = """            int item_len = snprintf(item, sizeof(item),\n                                    \"%s{\\\"index\\\":%d,\\\"track_key\\\":%u,\\\"title\\\":\\\"%s\\\",\\\"artist\\\":\\\"%s\\\",\\\"bpm\\\":%u,\\\"duration_ms\\\":%u}\",\n                                    first ? \"\" : \",\",\n                                    i, (unsigned)row.track_key, title_esc, artist_esc,\n                                    row.bpm, (unsigned)row.duration_ms);\n"""
    text = replace_once(text, old_item, new_item, "library stable track identity")

    parse_anchor = """// POST /api/control (query parameters, protected by X-DDJ-Control)\nstatic bool api_parse_deck(const char *value, uint8_t *out_deck)\n"""
    parse_insert = """// POST /api/control (query parameters, protected by X-DDJ-Control)\nstatic bool api_parse_u32(const char *value, uint32_t *out_value)\n{\n    if (!value || !value[0] || !out_value) {\n        return false;\n    }\n    char *end = NULL;\n    unsigned long parsed = strtoul(value, &end, 10);\n    if (!end || *end != '\\0' || parsed > UINT32_MAX) {\n        return false;\n    }\n    *out_value = (uint32_t)parsed;\n    return true;\n}\n\nstatic bool api_parse_deck(const char *value, uint8_t *out_deck)\n"""
    text = replace_once(text, parse_anchor, parse_insert, "uint32 web parser")

    old_load = """// POST /api/load (query parameters, protected by X-DDJ-Control)\nstatic esp_err_t api_load_handler(httpd_req_t *req)\n{\n    if (!api_request_allowed(req, true)) return ESP_FAIL;\n    char query[64] = {0};\n    char index_str[16] = {0};\n    char deck_str[16] = {0};\n\n    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||\n        httpd_query_key_value(query, \"index\", index_str,\n                              sizeof(index_str)) != ESP_OK ||\n        httpd_query_key_value(query, \"deck\", deck_str,\n                              sizeof(deck_str)) != ESP_OK) {\n        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,\n                                   \"Missing or oversized load parameters\");\n    }\n\n    int32_t index = 0;\n    uint8_t deck = CTRL_DECK_NONE;\n    if (!web_api_parse_int32(index_str, 0, INT32_MAX, &index) ||\n        !api_parse_deck(deck_str, &deck) ||\n        index >= media_catalog_count()) {\n        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,\n                                   \"Invalid track index or deck\");\n    }\n\n    ESP_LOGI(TAG, \"API Load Request: index=%ld, deck=%d\", (long)index, deck);\n\n    esp_err_t rc = ui_library_load_track_index_for_deck((int)index, deck);\n    if (rc != ESP_OK) {\n        service_log_event(SERVICE_LOG_WEB_LOAD_REQ_FAILED, SERVICE_LOG_WARN,\n                          3u, (uint32_t)index, (uint32_t)deck, (uint32_t)rc, 0u, NULL);\n        if (rc == ESP_ERR_INVALID_STATE) {\n            httpd_resp_set_status(req, \"409 Conflict\");\n            httpd_resp_send(req, \"Load busy\", HTTPD_RESP_USE_STRLEN);\n            return ESP_FAIL;\n        }\n        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, \"Load failed\");\n        return ESP_FAIL;\n    }\n\n    httpd_resp_send(req, \"OK\", 2);\n    return ESP_OK;\n}\n"""
    new_load = """// POST /api/load (stable track identity, protected by X-DDJ-Control)\nstatic esp_err_t api_load_handler(httpd_req_t *req)\n{\n    if (!api_request_allowed(req, true)) return ESP_FAIL;\n    char query[128] = {0};\n    char track_key_str[16] = {0};\n    char generation_str[16] = {0};\n    char deck_str[16] = {0};\n\n    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||\n        httpd_query_key_value(query, \"track_key\", track_key_str,\n                              sizeof(track_key_str)) != ESP_OK ||\n        httpd_query_key_value(query, \"generation\", generation_str,\n                              sizeof(generation_str)) != ESP_OK ||\n        httpd_query_key_value(query, \"deck\", deck_str,\n                              sizeof(deck_str)) != ESP_OK) {\n        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,\n                                   \"Missing or oversized load parameters\");\n    }\n\n    uint32_t track_key = 0u;\n    uint32_t generation = 0u;\n    uint8_t deck = CTRL_DECK_NONE;\n    if (!api_parse_u32(track_key_str, &track_key) || track_key == 0u ||\n        !api_parse_u32(generation_str, &generation) ||\n        !api_parse_deck(deck_str, &deck)) {\n        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,\n                                   \"Invalid track identity or deck\");\n    }\n    if (generation != media_catalog_generation()) {\n        httpd_resp_set_status(req, \"409 Conflict\");\n        return httpd_resp_send(req, \"Library generation changed\", HTTPD_RESP_USE_STRLEN);\n    }\n\n    ESP_LOGI(TAG, \"API Load Request: key=0x%08x generation=%u deck=%d\",\n             (unsigned)track_key, (unsigned)generation, deck);\n\n    esp_err_t rc = ui_library_load_track_identity_for_deck(track_key, generation, deck);\n    if (rc != ESP_OK) {\n        service_log_event(SERVICE_LOG_WEB_LOAD_REQ_FAILED, SERVICE_LOG_WARN,\n                          4u, track_key, generation, (uint32_t)deck, (uint32_t)rc, NULL);\n        if (rc == ESP_ERR_INVALID_STATE) {\n            httpd_resp_set_status(req, \"409 Conflict\");\n            return httpd_resp_send(req, \"Library changed or load busy\", HTTPD_RESP_USE_STRLEN);\n        }\n        if (rc == ESP_ERR_NOT_FOUND) {\n            return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, \"Track not found\");\n        }\n        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, \"Load failed\");\n    }\n\n    return httpd_resp_send(req, \"OK\", 2);\n}\n"""
    text = replace_once(text, old_load, new_load, "stable web load endpoint")

    save(path, text)


def patch_web_app() -> None:
    path, text = load("firmware/main-deck-p4/components/web_server/web/app.js")

    text = replace_once(
        text,
        "let libraryData = [];\n",
        "let libraryData = [];\nlet libraryGeneration = 0;\n",
        "browser library generation state",
    )
    text = replace_once(
        text,
        """        .then(data => {\n            libraryData = data.tracks || [];\n            renderLibrary(libraryData);\n        })\n""",
        """        .then(data => {\n            libraryGeneration = Number.isInteger(data.generation) ? data.generation : 0;\n            libraryData = data.tracks || [];\n            renderLibrary(libraryData);\n        })\n""",
        "browser generation capture",
    )
    text = replace_once(
        text,
        """                        <button class=\"btn btn-load\" onclick=\"loadTrack(${track.index}, 1)\">D1</button>\n                        <button class=\"btn btn-load\" onclick=\"loadTrack(${track.index}, 2)\">D2</button>\n""",
        """                        <button class=\"btn btn-load\" onclick=\"loadTrack(${track.track_key}, libraryGeneration, 1)\">D1</button>\n                        <button class=\"btn btn-load\" onclick=\"loadTrack(${track.track_key}, libraryGeneration, 2)\">D2</button>\n""",
        "browser identity load buttons",
    )
    text = replace_once(
        text,
        """function loadTrack(index, deck) {\n    fetch(`/api/load?index=${index}&deck=${deck}`, mutationOptions)\n        .then(res => {\n            if (res.ok) {\n                console.log(`Učitavanje pjesme ${index} na špil ${deck}`);\n            } else {\n                alert('Greška prilikom učitavanja pjesme.');\n            }\n        })\n        .catch(err => console.error(err));\n}\n""",
        """function loadTrack(trackKey, generation, deck) {\n    const url = `/api/load?track_key=${encodeURIComponent(trackKey)}&generation=${encodeURIComponent(generation)}&deck=${deck}`;\n    fetch(url, mutationOptions)\n        .then(async res => {\n            if (res.ok) {\n                console.log(`Učitavanje pjesme ${trackKey} na špil ${deck}`);\n                return;\n            }\n            if (res.status === 409) {\n                await fetchLibrary();\n                alert('Knjižnica se promijenila. Popis je osvježen; ponovite učitavanje.');\n                return;\n            }\n            alert('Greška prilikom učitavanja pjesme.');\n        })\n        .catch(err => console.error(err));\n}\n""",
        "browser stable load request",
    )

    save(path, text)


def main() -> None:
    patch_ui_library()
    patch_web_server()
    patch_web_app()
    print("Applied release audit phase 1 fixes.")


if __name__ == "__main__":
    main()
