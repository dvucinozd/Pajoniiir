#!/usr/bin/env python3
"""Harden audio load sessions, PSRAM admission, PVBR math and MT ownership."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "firmware/main-deck-p4/components/audio_engine/audio_engine.c"


def once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n != 1:
        raise RuntimeError(f"{label}: expected one match, found {n}")
    return text.replace(old, new, 1)


def main() -> None:
    text = SRC.read_text(encoding="utf-8")

    # Every new load must join the previous session even when its asynchronous
    # loader already marked eng->loaded=false because of a decode/preload error.
    text = once(
        text,
        """    if (eng->loaded) {\n        esp_err_t stop_rc = audio_engine_stop_for_deck(deck);\n        if (stop_rc != ESP_OK) {\n            eng->last_error = stop_rc;\n            snprintf(eng->last_error_text, sizeof(eng->last_error_text), \"STOP ERR\");\n            return stop_rc;\n        }\n    }\n""",
        """    /* A failed asynchronous load can clear loaded while loader/decoder\n     * tasks and the PSRAM preload buffer still belong to the old session. Always\n     * stop and join the previous session before publishing run=true for a retry. */\n    esp_err_t stop_rc = audio_engine_stop_for_deck(deck);\n    if (stop_rc != ESP_OK) {\n        eng->last_error = stop_rc;\n        snprintf(eng->last_error_text, sizeof(eng->last_error_text), \"STOP ERR\");\n        return stop_rc;\n    }\n""",
        "unconditional load teardown",
    )
    text = once(
        text,
        """static esp_err_t audio_engine_stop_for_deck(uint8_t deck)\n{\n    audio_engine_state_t *eng = &s_engines[deck];\n    if (!eng->loaded) return ESP_OK;\n""",
        """static esp_err_t audio_engine_stop_for_deck(uint8_t deck)\n{\n    audio_engine_state_t *eng = &s_engines[deck];\n    /* Do not key teardown on eng->loaded: error paths deliberately clear that\n     * flag before the parked tasks have exited. Runtime task ownership and the\n     * preload pointer are the authoritative session-liveness indicators. */\n""",
        "stop early return",
    )

    # Admit a whole-track preload only when one contiguous PSRAM block exists.
    alloc_old = """    fw->buf = heap_caps_malloc((size_t)fsz, MALLOC_CAP_SPIRAM);\n    if (!fw->buf) {\n        ESP_LOGE(TAG, \"PSRAM alloc %ld B failed\", fsz);\n        fclose(src);\n        media_io_gate_end();\n        ae_fail_load(eng, fw, runtime, ESP_ERR_NO_MEM, \"NO MEM\");\n        goto park;\n    }\n"""
    alloc_new = """    const size_t track_bytes = (size_t)fsz;\n    const size_t largest_psram = heap_caps_get_largest_free_block(\n        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);\n    if (track_bytes > largest_psram) {\n        ESP_LOGE(TAG, \"TRACK TOO LARGE: need=%u largest_psram=%u\",\n                 (unsigned)track_bytes, (unsigned)largest_psram);\n        fclose(src);\n        media_io_gate_end();\n        ae_fail_load(eng, fw, runtime, ESP_ERR_NO_MEM, \"TRACK TOO LARGE\");\n        goto park;\n    }\n    fw->buf = heap_caps_malloc(track_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);\n    if (!fw->buf) {\n        ESP_LOGE(TAG, \"PSRAM alloc %ld B failed\", fsz);\n        fclose(src);\n        media_io_gate_end();\n        ae_fail_load(eng, fw, runtime, ESP_ERR_NO_MEM, \"TRACK TOO LARGE\");\n        goto park;\n    }\n"""
    text = once(text, alloc_old, alloc_new, "PSRAM preflight")

    # 64-bit multiply and a central duration clamp avoid the ~3-hour overflow.
    pvbr_old = """static void seek_pvbr(audio_engine_state_t *eng, uint32_t position_ms)\n{\n    uint32_t idx = (eng->duration_ms > 0)\n                   ? (position_ms * AUDIO_PVBR_LEN / eng->duration_ms)\n                   : 0u;\n"""
    pvbr_new = """static void seek_pvbr(audio_engine_state_t *eng, uint32_t position_ms)\n{\n    if (eng->duration_ms > 0u && position_ms > eng->duration_ms) {\n        position_ms = eng->duration_ms;\n    }\n    uint32_t idx = (eng->duration_ms > 0u)\n                   ? (uint32_t)(((uint64_t)position_ms *\n                                 (uint64_t)AUDIO_PVBR_LEN) /\n                                (uint64_t)eng->duration_ms)\n                   : 0u;\n"""
    text = once(text, pvbr_old, pvbr_new, "PVBR 64-bit math")

    # Add a monotonically increasing mailbox epoch next to the existing atomic
    # enable state. Static zero initialization means no startup reset is needed.
    pattern = re.compile(r"(static\s+bool\s+s_master_tempo_enabled\s*\[AUDIO_ENGINE_DECK_COUNT\]\s*;)")
    text, n = pattern.subn(
        r"\1\nstatic uint32_t         s_master_tempo_command_epoch[AUDIO_ENGINE_DECK_COUNT];\n"
        r"static uint32_t         s_master_tempo_applied_epoch[AUDIO_ENGINE_DECK_COUNT];",
        text,
        count=1,
    )
    if n != 1:
        raise RuntimeError(f"master tempo declaration: expected one match, found {n}")

    helper_anchor = """#if AE_FW\nstatic void audio_output_apply_pending_fx_commands(void)\n"""
    helper = """#if AE_FW\nstatic void audio_output_apply_master_tempo_commands(void)\n{\n    for (uint8_t deck = 0u; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {\n        const uint32_t epoch = __atomic_load_n(\n            &s_master_tempo_command_epoch[deck], __ATOMIC_ACQUIRE);\n        if (epoch == s_master_tempo_applied_epoch[deck]) {\n            continue;\n        }\n        /* Output task is the sole owner of keylock/resampler DSP mutation. The\n         * command takes effect exactly at an audio-block boundary. */\n        s_keylocks[deck].initialized = false;\n        audio_resampler_reset(&s_resamplers[deck]);\n        s_master_tempo_applied_epoch[deck] = epoch;\n    }\n}\n\nstatic void audio_output_apply_pending_fx_commands(void)\n"""
    text = once(text, helper_anchor, helper, "master tempo output mailbox")
    text = once(
        text,
        """        audio_output_apply_pending_fx_commands();\n""",
        """        audio_output_apply_master_tempo_commands();\n        audio_output_apply_pending_fx_commands();\n""",
        "apply MT at block boundary",
    )

    setter_old = """void audio_engine_deck_set_master_tempo(uint8_t deck, bool enabled)\n{\n    if (!deck_is_valid(deck)) return;\n#if AE_FW\n    atomic_store_bool(&s_master_tempo_enabled[deck], enabled);\n    s_keylocks[deck].initialized = false;\n    audio_resampler_reset(&s_resamplers[deck]);\n#else\n    (void)enabled;\n#endif\n}\n"""
    setter_new = """void audio_engine_deck_set_master_tempo(uint8_t deck, bool enabled)\n{\n    if (!deck_is_valid(deck)) return;\n#if AE_FW\n    atomic_store_bool(&s_master_tempo_enabled[deck], enabled);\n    (void)__atomic_add_fetch(&s_master_tempo_command_epoch[deck],\n                             1u, __ATOMIC_RELEASE);\n#else\n    (void)enabled;\n#endif\n}\n"""
    text = once(text, setter_old, setter_new, "master tempo producer")

    SRC.write_text(text, encoding="utf-8")
    print("Applied audio lifecycle, PSRAM, PVBR and master-tempo fixes.")


if __name__ == "__main__":
    main()
