#!/usr/bin/env python3
"""Eliminate cross-task borrowed ANLZ pointers with locked owned snapshots."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def replace_function(text: str, signature: str, replacement: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise RuntimeError(f"missing function: {signature}")
    brace = text.find("{", start)
    if brace < 0:
        raise RuntimeError(f"missing body: {signature}")
    depth = 0
    for pos in range(brace, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[:start] + replacement.rstrip() + text[pos + 1:]
    raise RuntimeError(f"unterminated function: {signature}")


def patch_store_header() -> None:
    path = ROOT / "firmware/main-deck-p4/components/ui/include/ui_deck_anlz_store.h"
    text = path.read_text(encoding="utf-8")
    text = replace_once(
        text,
        """typedef struct {\n    anlz_metadata_t meta[UI_DECK_ANLZ_STORE_DECK_COUNT];\n    bool valid[UI_DECK_ANLZ_STORE_DECK_COUNT];\n} ui_deck_anlz_store_t;\n""",
        """typedef struct {\n    anlz_metadata_t meta[UI_DECK_ANLZ_STORE_DECK_COUNT];\n    bool valid[UI_DECK_ANLZ_STORE_DECK_COUNT];\n    volatile uint32_t lock;\n} ui_deck_anlz_store_t;\n""",
        "ANLZ store lock field",
    )
    text = replace_once(
        text,
        """const anlz_metadata_t *ui_deck_anlz_store_get(const ui_deck_anlz_store_t *store,\n                                                uint8_t deck);\n""",
        """/* UI-task-only borrowed access. Cross-task users must call clone(). */\nconst anlz_metadata_t *ui_deck_anlz_store_get(const ui_deck_anlz_store_t *store,\n                                                uint8_t deck);\nbool ui_deck_anlz_store_clone(ui_deck_anlz_store_t *store,\n                              uint8_t deck,\n                              anlz_metadata_t *out);\n""",
        "ANLZ clone API declaration",
    )
    path.write_text(text, encoding="utf-8")


def patch_store_source() -> None:
    path = ROOT / "firmware/main-deck-p4/components/ui/ui_deck_anlz_store.c"
    text = path.read_text(encoding="utf-8")
    text = replace_once(
        text,
        """static bool deck_index(uint8_t deck, uint8_t *out_idx)\n""",
        """static void store_lock(ui_deck_anlz_store_t *store)\n{\n    while (__atomic_test_and_set(&store->lock, __ATOMIC_ACQUIRE)) {\n        /* Very short metadata pointer/clone critical section. */\n    }\n}\n\nstatic void store_unlock(ui_deck_anlz_store_t *store)\n{\n    __atomic_clear(&store->lock, __ATOMIC_RELEASE);\n}\n\nstatic bool deck_index(uint8_t deck, uint8_t *out_idx)\n""",
        "ANLZ store lock helpers",
    )

    text = replace_function(
        text,
        "void ui_deck_anlz_store_clear(ui_deck_anlz_store_t *store, uint8_t deck)",
        r'''void ui_deck_anlz_store_clear(ui_deck_anlz_store_t *store, uint8_t deck)
{
    uint8_t idx = 0;
    if (!store || !deck_index(deck, &idx)) {
        return;
    }

    anlz_metadata_t old = {0};
    bool had_old = false;
    store_lock(store);
    if (store->valid[idx]) {
        old = store->meta[idx];
        had_old = true;
    }
    memset(&store->meta[idx], 0, sizeof(store->meta[idx]));
    store->valid[idx] = false;
    store_unlock(store);

    if (had_old) {
        anlz_free(&old);
    }
}''',
    )

    text = replace_function(
        text,
        "bool ui_deck_anlz_store_set(ui_deck_anlz_store_t *store,",
        r'''bool ui_deck_anlz_store_set(ui_deck_anlz_store_t *store,
                             uint8_t deck,
                             const anlz_metadata_t *meta)
{
    uint8_t idx = 0;
    if (!store || !deck_index(deck, &idx) || !meta) {
        return false;
    }

    anlz_metadata_t next = {0};
    if (anlz_clone(meta, &next) != ESP_OK) {
        return false;
    }

    anlz_metadata_t old = {0};
    bool had_old = false;
    store_lock(store);
    if (store->valid[idx]) {
        old = store->meta[idx];
        had_old = true;
    }
    store->meta[idx] = next;
    store->valid[idx] = true;
    store_unlock(store);

    if (had_old) {
        anlz_free(&old);
    }
    return true;
}''',
    )

    text += r'''

bool ui_deck_anlz_store_clone(ui_deck_anlz_store_t *store,
                              uint8_t deck,
                              anlz_metadata_t *out)
{
    uint8_t idx = 0;
    if (!store || !out || !deck_index(deck, &idx)) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    store_lock(store);
    const bool valid = store->valid[idx];
    const esp_err_t rc = valid ? anlz_clone(&store->meta[idx], out) : ESP_ERR_NOT_FOUND;
    store_unlock(store);
    return rc == ESP_OK;
}
'''
    path.write_text(text, encoding="utf-8")


def patch_ui_api() -> None:
    header = ROOT / "firmware/main-deck-p4/components/ui/include/ui.h"
    text = header.read_text(encoding="utf-8")
    text = replace_once(
        text,
        "const anlz_metadata_t *ui_get_deck_anlz_metadata(uint8_t deck);\n",
        """/* UI-only borrowed pointer retained for rendering callbacks. */\nconst anlz_metadata_t *ui_get_deck_anlz_metadata(uint8_t deck);\n/* Thread-safe owned copy for deck/audio tasks; caller calls anlz_free(). */\nbool ui_clone_deck_anlz_metadata(uint8_t deck, anlz_metadata_t *out);\n""",
        "UI ANLZ clone declaration",
    )
    header.write_text(text, encoding="utf-8")

    source = ROOT / "firmware/main-deck-p4/components/ui/ui.c"
    text = source.read_text(encoding="utf-8")
    text = replace_once(
        text,
        """const anlz_metadata_t *ui_get_deck_anlz_metadata(uint8_t deck)\n{\n    return ui_deck_anlz(deck);\n}\n""",
        """const anlz_metadata_t *ui_get_deck_anlz_metadata(uint8_t deck)\n{\n    return ui_deck_anlz(deck);\n}\n\nbool ui_clone_deck_anlz_metadata(uint8_t deck, anlz_metadata_t *out)\n{\n    return ui_deck_anlz_store_clone(&s_deck_anlz_store, ui_deck_index(deck), out);\n}\n""",
        "UI ANLZ clone implementation",
    )
    source.write_text(text, encoding="utf-8")


def patch_deck_core() -> None:
    path = ROOT / "firmware/main-deck-p4/components/deck_core/deck_core.c"
    text = path.read_text(encoding="utf-8")
    text = replace_once(
        text,
        "extern const anlz_metadata_t *ui_get_deck_anlz_metadata(uint8_t deck) __attribute__((weak));\n",
        "extern bool ui_clone_deck_anlz_metadata(uint8_t deck, anlz_metadata_t *out) __attribute__((weak));\n",
        "deck weak clone API",
    )
    text = replace_function(
        text,
        "static const anlz_metadata_t *loaded_anlz_for_deck(uint8_t deck)",
        r'''static bool clone_loaded_anlz_for_deck(uint8_t deck, anlz_metadata_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (deck >= DECK_CORE_DECK_COUNT || !ui_clone_deck_anlz_metadata) {
        return false;
    }
    return ui_clone_deck_anlz_metadata(deck, out);
}''',
    )

    text = replace_function(
        text,
        "static void handle_beat_jump(uint8_t deck, int beat_shift, deck_state_t *state)",
        r'''static void handle_beat_jump(uint8_t deck, int beat_shift, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state || beat_shift == 0) {
        return;
    }

    uint32_t position_ms = current_deck_position_ms(deck, state);
    anlz_metadata_t meta = {0};
    const bool has_meta = clone_loaded_anlz_for_deck(deck, &meta);
    uint16_t bpm = loaded_bpm_for_deck(deck);
    uint32_t target_ms = beat_jump_calculate_target_ms(position_ms,
                                                        bpm,
                                                        beat_shift,
                                                        has_meta ? &meta : NULL);
    if (has_meta) {
        anlz_free(&meta);
    }

    esp_err_t rc = audio_engine_deck_seek(deck, target_ms);
    if (rc == ESP_OK) {
        state->position_ms = target_ms;
        ESP_LOGI(TAG, "deck %u beat jump %+d -> %lu ms",
                 (unsigned)deck + 1,
                 beat_shift,
                 (unsigned long)target_ms);
    } else {
        ESP_LOGW(TAG, "deck %u beat jump %+d failed: %s",
                 (unsigned)deck + 1,
                 beat_shift,
                 esp_err_to_name(rc));
    }
}''',
    )

    text = replace_function(
        text,
        "static uint32_t nearest_beat_ms(uint8_t deck, uint32_t position_ms)",
        r'''static uint32_t nearest_beat_ms(uint8_t deck, uint32_t position_ms)
{
    anlz_metadata_t meta = {0};
    if (!clone_loaded_anlz_for_deck(deck, &meta) || !meta.beats || meta.beat_count == 0) {
        anlz_free(&meta);
        return position_ms;
    }

    uint16_t lo = 0;
    uint16_t hi = meta.beat_count;
    while (lo < hi) {
        uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
        if (meta.beats[mid].time_ms < position_ms) {
            lo = (uint16_t)(mid + 1u);
        } else {
            hi = mid;
        }
    }

    uint32_t result = position_ms;
    if (lo == 0u) {
        result = meta.beats[0].time_ms;
    } else if (lo >= meta.beat_count) {
        result = meta.beats[meta.beat_count - 1u].time_ms;
    } else {
        const uint32_t after_ms = meta.beats[lo].time_ms;
        const uint32_t before_ms = meta.beats[lo - 1u].time_ms;
        result = (position_ms - before_ms) <= (after_ms - position_ms)
                     ? before_ms
                     : after_ms;
    }
    anlz_free(&meta);
    return result;
}''',
    )

    text = replace_function(
        text,
        "static void handle_beat_loop_pad_action(uint8_t deck, uint8_t pad, deck_state_t *state)",
        r'''static void handle_beat_loop_pad_action(uint8_t deck, uint8_t pad, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state) {
        return;
    }

    beat_loop_length_t length = {0};
    if (!beat_loop_length_for_pad(pad, &length)) {
        return;
    }

    uint32_t start_ms = current_deck_position_ms(deck, state);
    anlz_metadata_t meta = {0};
    const bool has_meta = clone_loaded_anlz_for_deck(deck, &meta);
    uint32_t duration_ms = beat_loop_calculate_duration_ms(start_ms,
                                                           loaded_bpm_for_deck(deck),
                                                           length.numerator,
                                                           length.denominator,
                                                           has_meta ? &meta : NULL);
    if (has_meta) {
        anlz_free(&meta);
    }
    if (duration_ms == 0 || start_ms > UINT32_MAX - duration_ms) {
        return;
    }
    s_beat_loop_led[deck].active = true;
    s_beat_loop_led[deck].pad = pad;
    set_deck_loop(deck, start_ms, start_ms + duration_ms);
}''',
    )

    remaining = text.count("loaded_anlz_for_deck(")
    if remaining != 0:
        raise RuntimeError(f"unconverted borrowed ANLZ calls remain: {remaining}")
    path.write_text(text, encoding="utf-8")


def main() -> None:
    patch_store_header()
    patch_store_source()
    patch_ui_api()
    patch_deck_core()
    print("Applied owned ANLZ snapshot fixes.")


if __name__ == "__main__":
    main()
