/*
 * Production wrapper that keeps the existing web server implementation while
 * routing every web-originated loop mutation through deck_core.
 */
#define audio_engine_deck_set_loop   web_bridge_set_loop
#define audio_engine_deck_clear_loop web_bridge_clear_loop
#include "web_server.c"
#undef audio_engine_deck_set_loop
#undef audio_engine_deck_clear_loop

esp_err_t web_bridge_set_loop(uint8_t deck, uint32_t start_ms, uint32_t end_ms)
{
    (void)start_ms;
    (void)end_ms;
    if (deck > CTRL_DECK_2) return ESP_ERR_INVALID_ARG;

    /* Pad 8 in Beat Loop mode is the existing authoritative four-beat action.
     * deck_core recalculates the current position/BPM, updates the audio loop,
     * remembers the loop and publishes coherent UI/LED state. */
    ctrl_event_t ev = {
        .type = CTRL_EV_BUTTON,
        .id = deck == CTRL_DECK_2 ? CTRL_ID_DECK2_PAD_ACTION
                                  : CTRL_ID_DECK1_PAD_ACTION,
        .value = CTRL_PAD_ACTION_VALUE(CTRL_PAD_MODE_BEAT_LOOP, 7u, false, true),
        .deck = deck,
        .control = CTRL_DECK_CTL_PAD_ACTION,
        .seq = 0u,
    };
    return deck_core_queue_event(&ev);
}

esp_err_t web_bridge_clear_loop(uint8_t deck)
{
    if (deck > CTRL_DECK_2) return ESP_ERR_INVALID_ARG;

    ctrl_event_t ev = {
        .type = CTRL_EV_BUTTON,
        .id = deck == CTRL_DECK_2 ? CTRL_ID_DECK2_EXT_ACTION
                                  : CTRL_ID_DECK1_EXT_ACTION,
        .value = CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_RELOOP_STOP, true),
        .deck = deck,
        .control = CTRL_DECK_CTL_EXT_ACTION,
        .seq = 0u,
    };
    return deck_core_queue_event(&ev);
}
