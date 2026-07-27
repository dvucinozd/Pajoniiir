#!/usr/bin/env python3
"""Fix brightness persistence, track duration and ANLZ short-read publication."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n != 1:
        raise RuntimeError(f"{label}: expected one match, found {n}")
    return text.replace(old, new, 1)


def patch_settings() -> None:
    path = ROOT / "firmware/main-deck-p4/components/ui/ui_settings.c"
    text = path.read_text(encoding="utf-8")
    text = once(
        text,
        """static void slider_brightness_event_cb(lv_event_t *event)\n{\n    lv_obj_t *slider = lv_event_get_target(event);\n    int val = lv_slider_get_value(slider);\n    lv_label_set_text_fmt(s_label_brightness_val, \"%d%%\", val);\n#ifndef WIN32\n    bsp_display_set_backlight((uint8_t)val);\n    app_settings_set_backlight((uint8_t)val);\n#endif\n    ESP_LOGI(TAG, \"Backlight brightness set to %d%%\", val);\n}\n""",
        """static void slider_brightness_event_cb(lv_event_t *event)\n{\n    const lv_event_code_t code = lv_event_get_code(event);\n    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) return;\n    lv_obj_t *slider = lv_event_get_target(event);\n    int val = lv_slider_get_value(slider);\n    lv_label_set_text_fmt(s_label_brightness_val, \"%d%%\", val);\n#ifndef WIN32\n    if (code == LV_EVENT_VALUE_CHANGED) {\n        /* Real-time hardware feedback without a flash write for every drag tick. */\n        bsp_display_set_backlight((uint8_t)val);\n    } else {\n        app_settings_set_backlight((uint8_t)val);\n        ESP_LOGI(TAG, \"Backlight brightness persisted at %d%%\", val);\n    }\n#endif\n}\n""",
        "brightness release persistence",
    )
    text = once(
        text,
        "lv_obj_add_event_cb(slider_backlight, slider_brightness_event_cb, LV_EVENT_VALUE_CHANGED, NULL);",
        "lv_obj_add_event_cb(slider_backlight, slider_brightness_event_cb, LV_EVENT_ALL, NULL);",
        "brightness event registration",
    )
    path.write_text(text, encoding="utf-8")


def patch_duration() -> None:
    path = ROOT / "firmware/main-deck-p4/components/library/library.c"
    text = path.read_text(encoding="utf-8")
    text = once(
        text,
        """    if (meta->beat_count > 0 && meta->beats) {\n        track->duration_ms = meta->beats[meta->beat_count - 1].time_ms;\n    }\n""",
        """    /* PDB/audio duration includes the outro after the final beat. Beatgrid\n     * duration is only a fallback when the catalog has no duration at all. */\n    if (track->duration_ms == 0u && meta->beat_count > 0 && meta->beats) {\n        track->duration_ms = meta->beats[meta->beat_count - 1].time_ms;\n    }\n""",
        "preserve PDB duration",
    )
    path.write_text(text, encoding="utf-8")


def patch_anlz() -> None:
    path = ROOT / "firmware/main-deck-p4/components/library/rekordbox_anlz.c"
    text = path.read_text(encoding="utf-8")

    text = once(
        text,
        "#define ANLZ_MAX_BEATS 0xFFFFu\n",
        """#define ANLZ_MAX_BEATS 0xFFFFu\n\nstatic bool s_read_failed;\nstatic bool s_structure_malformed;\n""",
        "ANLZ parser failure state",
    )
    text = once(
        text,
        """    uint8_t b[2];\n    if (fread(b, 1, 2, fp) != 2) return 0;\n""",
        """    uint8_t b[2];\n    if (fread(b, 1, 2, fp) != 2) {\n        s_read_failed = true;\n        return 0;\n    }\n""",
        "BE16 exact read",
    )
    text = once(
        text,
        """    uint8_t b[4];\n    if (fread(b, 1, 4, fp) != 4) return 0;\n""",
        """    uint8_t b[4];\n    if (fread(b, 1, 4, fp) != 4) {\n        s_read_failed = true;\n        return 0;\n    }\n""",
        "BE32 exact read",
    )

    old_find = """static bool find_tag(FILE *fp, uint32_t target)\n{\n    tag_walk_result_t rc = walk_sections_for_tag(fp, target);\n    if (rc == TAG_WALK_FOUND) return true;\n    if (rc == TAG_WALK_ABSENT) return false;   /* well-formed file, tag not present */\n    return scan_bytes_for_tag(fp, target);\n}\n"""
    new_find = """static bool find_tag(FILE *fp, uint32_t target)\n{\n    tag_walk_result_t rc = walk_sections_for_tag(fp, target);\n    if (rc == TAG_WALK_MALFORMED) s_structure_malformed = true;\n    /* Never scan payload bytes in a structurally broken file. A false tag hit\n     * could otherwise publish and cache truncated waveform/cue/beatgrid data. */\n    return rc == TAG_WALK_FOUND;\n}\n"""
    text = once(text, old_find, new_find, "bounded structural tag lookup")

    text = once(
        text,
        """        uint8_t hi = (uint8_t)fgetc(fp);\n        uint8_t lo = (uint8_t)fgetc(fp);\n        uint16_t wc = (uint16_t)(((uint16_t)hi << 8u) | (uint16_t)lo);\n""",
        """        int hi_raw = fgetc(fp);\n        int lo_raw = fgetc(fp);\n        if (hi_raw == EOF || lo_raw == EOF) return ESP_ERR_INVALID_SIZE;\n        uint8_t hi = (uint8_t)hi_raw;\n        uint8_t lo = (uint8_t)lo_raw;\n        uint16_t wc = (uint16_t)(((uint16_t)hi << 8u) | (uint16_t)lo);\n""",
        "PPTH exact UTF16 read",
    )
    text = once(
        text,
        """            uint8_t hi2 = (uint8_t)fgetc(fp);\n            uint8_t lo2 = (uint8_t)fgetc(fp);\n            uint16_t wc2 = (uint16_t)(((uint16_t)hi2 << 8u) | (uint16_t)lo2);\n""",
        """            int hi2_raw = fgetc(fp);\n            int lo2_raw = fgetc(fp);\n            if (hi2_raw == EOF || lo2_raw == EOF) return ESP_ERR_INVALID_SIZE;\n            uint8_t hi2 = (uint8_t)hi2_raw;\n            uint8_t lo2 = (uint8_t)lo2_raw;\n            uint16_t wc2 = (uint16_t)(((uint16_t)hi2 << 8u) | (uint16_t)lo2);\n""",
        "PPTH exact surrogate read",
    )
    text = once(
        text,
        """        uint8_t buf[56];\n        if (fread(buf, 1, 56, fp) != 56) break;\n""",
        """        uint8_t buf[56];\n        if (fread(buf, 1, 56, fp) != 56) return ESP_ERR_INVALID_SIZE;\n""",
        "PCOB exact record read",
    )
    text = once(
        text,
        """    size_t n = fread(meta->waveform_high, 1, data_len, fp);\n    meta->waveform_high_len = (uint32_t)n;\n\n    ANLZ_LOGI(TAG, \"PWV3: %u bytes high-res waveform\", (unsigned)n);\n    return (n == data_len) ? ESP_OK : ESP_FAIL;\n""",
        """    size_t n = fread(meta->waveform_high, 1, data_len, fp);\n    if (n != data_len) {\n        free(meta->waveform_high);\n        meta->waveform_high = NULL;\n        meta->waveform_high_len = 0u;\n        return ESP_ERR_INVALID_SIZE;\n    }\n    meta->waveform_high_len = data_len;\n\n    ANLZ_LOGI(TAG, \"PWV3: %u bytes high-res waveform\", (unsigned)n);\n    return ESP_OK;\n""",
        "PWV3 transactional publish",
    )

    text = once(
        text,
        """    memset(out, 0, sizeof(*out));\n\n    FILE *fp = fopen(dat_path, \"rb\");\n""",
        """    memset(out, 0, sizeof(*out));\n    s_read_failed = false;\n    s_structure_malformed = false;\n\n    FILE *fp = fopen(dat_path, \"rb\");\n""",
        "DAT failure reset",
    )
    text = once(
        text,
        """        if (rc == ESP_OK) {\n            found |= wanted[i].flag;\n        } else {\n""",
        """        if (s_read_failed || s_structure_malformed) {\n            rc = ESP_ERR_INVALID_SIZE;\n        }\n        if (rc == ESP_OK) {\n            found |= wanted[i].flag;\n        } else {\n""",
        "DAT short-read propagation",
    )
    text = once(
        text,
        """    if (!(found & GOT_PPTH)) {\n""",
        """    if (s_read_failed || s_structure_malformed) {\n        ANLZ_LOGE(TAG, \"DAT structure truncated or malformed\");\n        anlz_free(out);\n        memset(out, 0, sizeof(*out));\n        return ESP_ERR_INVALID_SIZE;\n    }\n\n    if (!(found & GOT_PPTH)) {\n""",
        "DAT reject partial object",
    )
    text = once(
        text,
        """    FILE *fp = fopen(ext_path, \"rb\");\n""",
        """    s_read_failed = false;\n    s_structure_malformed = false;\n    FILE *fp = fopen(ext_path, \"rb\");\n""",
        "EXT failure reset",
    )
    text = once(
        text,
        """    fclose(fp);\n    return rc;\n}\n\nesp_err_t anlz_clone""",
        """    fclose(fp);\n    if (s_read_failed || s_structure_malformed) {\n        if (meta->waveform_high) {\n            free(meta->waveform_high);\n            meta->waveform_high = NULL;\n            meta->waveform_high_len = 0u;\n        }\n        return ESP_ERR_INVALID_SIZE;\n    }\n    return rc;\n}\n\nesp_err_t anlz_clone""",
        "EXT reject partial object",
    )

    # The fallback is now intentionally dead and would trigger -Wunused-function.
    start = text.find("static bool scan_bytes_for_tag(FILE *fp, uint32_t target)")
    end = text.find("static bool find_tag(FILE *fp, uint32_t target)", start)
    if start < 0 or end < 0:
        raise RuntimeError("legacy tag scanner region not found")
    text = text[:start] + text[end:]

    path.write_text(text, encoding="utf-8")


def main() -> None:
    patch_settings()
    patch_duration()
    patch_anlz()
    print("Applied parser, duration and brightness persistence fixes.")


if __name__ == "__main__":
    main()
