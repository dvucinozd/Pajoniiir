/*
 * Production wrapper for deck_core.c.
 *
 * Public UI/web readers continue to consume the published seqlock snapshot.
 * The FLX4 LED publisher, however, runs inside the deck actor while the current
 * event is still being applied. Override only the final publisher call so LED
 * input is refreshed from actor-owned live state immediately before sending.
 */
#include "deck_core.h"
#include "flx4_led_snapshot.h"

static esp_err_t deck_core_live_led_publish(
    flx4_led_publisher_t *publisher,
    const flx4_led_snapshot_input_t *input,
    bool force,
    flx4_led_send_fn_t send,
    void *ctx);

#define flx4_led_publisher_publish deck_core_live_led_publish
#include "deck_core.c"
#undef flx4_led_publisher_publish

static esp_err_t deck_core_live_led_publish(
    flx4_led_publisher_t *publisher,
    const flx4_led_snapshot_input_t *input,
    bool force,
    flx4_led_send_fn_t send,
    void *ctx)
{
    if (!input) return ESP_ERR_INVALID_ARG;

    flx4_led_snapshot_input_t live = *input;
    for (uint8_t deck = 0; deck < DECK_CORE_DECK_COUNT; ++deck) {
        deck_state_t state = s_decks[deck];
        if (deck_uses_audio_engine(deck)) {
            state.playing = audio_engine_deck_is_playing(deck);
            state.position_ms = audio_engine_deck_position_ms(deck);
        }

        live.cue[deck] = state.position_ms == state.cue_point_ms ? 1u : 0u;
        live.play[deck] = state.playing ? 1u : 0u;
        live.sync[deck] = state.sync_enabled ? 1u : 0u;
        live.pad_mode[deck] = state.pad_mode;
        live.censor_active[deck] = state.censor_active ? 1u : 0u;
        live.loop_in_marker[deck] = s_loop_shadow[deck].pending_in ? 1u : 0u;
    }

    return flx4_led_publisher_publish(publisher, &live, force, send, ctx);
}
