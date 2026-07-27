#!/usr/bin/env python3
"""Route web loop commands through the authoritative deck actor."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n != 1:
        raise RuntimeError(f"{label}: expected one match, found {n}")
    return text.replace(old, new, 1)


def patch_header() -> None:
    p = ROOT / "firmware/main-deck-p4/components/deck_core/include/deck_core.h"
    s = p.read_text(encoding="utf-8")
    s = once(
        s,
        "esp_err_t deck_core_queue_event(const ctrl_event_t *ev);\n",
        """esp_err_t deck_core_queue_event(const ctrl_event_t *ev);\n/* Web/UI loop requests are commands to the deck actor, never direct audio-engine\n * mutations. This keeps audio, deck state, loop shadow and LEDs coherent. */\nesp_err_t deck_core_request_loop_4(uint8_t deck);\nesp_err_t deck_core_request_loop_clear(uint8_t deck);\n""",
        "deck loop API declarations",
    )
    p.write_text(s, encoding="utf-8")


def patch_deck_core() -> None:
    p = ROOT / "firmware/main-deck-p4/components/deck_core/deck_core.c"
    s = p.read_text(encoding="utf-8")
    s = once(
        s,
        "#define DECK_CORE_COMPAT_DECK CTRL_DECK_1\n",
        """#define DECK_CORE_COMPAT_DECK CTRL_DECK_1\n#define DECK_CORE_INTERNAL_LOOP_4      0xF0u\n#define DECK_CORE_INTERNAL_LOOP_CLEAR  0xF1u\n""",
        "internal loop command IDs",
    ) if "#define DECK_CORE_COMPAT_DECK CTRL_DECK_1" in s else s
    # Some builds define the compat macro only in the header; anchor near queue constants.
    if "DECK_CORE_INTERNAL_LOOP_4" not in s:
        anchor = "#define CTRL_QUEUE_LEN"
        idx = s.find(anchor)
        if idx < 0:
            raise RuntimeError("deck queue constant anchor missing")
        line_end = s.find("\n", idx)
        s = s[:line_end + 1] + "#define DECK_CORE_INTERNAL_LOOP_4      0xF0u\n#define DECK_CORE_INTERNAL_LOOP_CLEAR  0xF1u\n" + s[line_end + 1:]

    old_loop = """        if (event_uses_ui_without_deck_state(&ev)) {\n"""
    new_loop = """        if (ev.type == CTRL_EV_BUTTON &&\n            (ev.id == DECK_CORE_INTERNAL_LOOP_4 ||\n             ev.id == DECK_CORE_INTERNAL_LOOP_CLEAR)) {\n            uint8_t deck = normalize_deck(ev.deck);\n            if (ev.value != 0) {\n                if (ev.id == DECK_CORE_INTERNAL_LOOP_4) {\n                    handle_beat_loop_pad_action(deck, 7u, &s_decks[deck]);\n                } else {\n                    stop_and_forget_loop(deck);\n                }\n            }\n            continue;\n        }\n\n        if (event_uses_ui_without_deck_state(&ev)) {\n"""
    s = once(s, old_loop, new_loop, "deck actor loop command handling")

    anchor = """esp_err_t deck_core_queue_event(const ctrl_event_t *ev)\n{\n"""
    idx = s.find(anchor)
    if idx < 0:
        raise RuntimeError("deck_core_queue_event missing")
    insert = r'''static esp_err_t deck_core_queue_internal_loop(uint8_t deck, uint8_t command_id)
{
    if (deck >= DECK_CORE_DECK_COUNT) return ESP_ERR_INVALID_ARG;
    ctrl_event_t ev = {
        .type = CTRL_EV_BUTTON,
        .id = command_id,
        .value = 1,
        .deck = deck,
    };
    return deck_core_queue_event(&ev);
}

esp_err_t deck_core_request_loop_4(uint8_t deck)
{
    return deck_core_queue_internal_loop(deck, DECK_CORE_INTERNAL_LOOP_4);
}

esp_err_t deck_core_request_loop_clear(uint8_t deck)
{
    return deck_core_queue_internal_loop(deck, DECK_CORE_INTERNAL_LOOP_CLEAR);
}

'''
    s = s[:idx] + insert + s[idx:]
    p.write_text(s, encoding="utf-8")


def patch_web() -> None:
    p = ROOT / "firmware/main-deck-p4/components/web_server/web_server.c"
    s = p.read_text(encoding="utf-8")
    old = """    } else if (strcmp(action, \"loop_4\") == 0) {\n        audio_engine_deck_status_t status = {0};\n        esp_err_t rc = audio_engine_deck_get_status(deck, &status);\n        if (rc != ESP_OK) {\n            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, \"Invalid deck\");\n            return ESP_FAIL;\n        }\n        uint32_t pos = status.position_ms;\n        uint16_t bpm = ui_library_deck_bpm(deck, 120);\n        if (bpm == 0) {\n            bpm = 120;\n        }\n        uint32_t beat_len_ms = 60000u / bpm;\n        uint32_t loop_len_ms = 4u * beat_len_ms;\n        uint32_t loop_end = pos > UINT32_MAX - loop_len_ms\n            ? UINT32_MAX : pos + loop_len_ms;\n        rc = audio_engine_deck_set_loop(deck, pos, loop_end);\n        if (rc != ESP_OK) {\n            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, \"Loop failed\");\n            return ESP_FAIL;\n        }\n    } else if (strcmp(action, \"loop_clear\") == 0) {\n        esp_err_t rc = audio_engine_deck_clear_loop(deck);\n        if (rc != ESP_OK) {\n            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, \"Loop clear failed\");\n            return ESP_FAIL;\n        }\n"""
    new = """    } else if (strcmp(action, \"loop_4\") == 0) {\n        queue_rc = deck_core_request_loop_4(deck);\n    } else if (strcmp(action, \"loop_clear\") == 0) {\n        queue_rc = deck_core_request_loop_clear(deck);\n"""
    s = once(s, old, new, "web loop actor routing")
    p.write_text(s, encoding="utf-8")


def main() -> None:
    patch_header()
    patch_deck_core()
    patch_web()
    print("Applied authoritative deck-loop routing.")


if __name__ == "__main__":
    main()
