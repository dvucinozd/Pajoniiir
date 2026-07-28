/*
 * Production wrapper for deck_core.c.
 *
 * Public UI/web readers consume the seqlock snapshot. FLX4 feedback runs inside
 * the deck actor, so refresh state-driven LED values from actor-owned live data
 * and publish that state before the remaining LED cache helpers execute.
 */
#include "deck_core.h"
#include "flx4_led_snapshot.h"

static esp_err_t deck_core_live_led_publish(
    flx4_led_publisher_t *publisher,
    const flx4_led_snapshot_input_t *input,
    bool force,
    flx4_led_send_fn_t send,
    void *ctx);
static void deck_core_live_send_led_deck(led_id_t led, uint8_t state, uint8_t deck);

#define flx4_led_publisher_publish deck_core_live_led_publish
#define control_link_send_led_deck deck_core_live_send_led_deck
#include "deck_core.c"
#undef control_link_send_led_deck
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

    /* The original publish function invokes track/Beat-Jump cache helpers after
     * this callback returns. Publish now so those helpers calculate and store the
     * same value that is actually sent, including track unload transitions. */
    publish_state_snapshot();
    return flx4_led_publisher_publish(publisher, &live, force, send, ctx);
}

static void deck_core_live_send_led_deck(led_id_t led, uint8_t state, uint8_t deck)
{
    if (deck < DECK_CORE_DECK_COUNT) {
        const bool beat_jump_mode = s_decks[deck].pad_mode == CTRL_PAD_MODE_BEAT_JUMP;
        const bool loaded = deck_has_loaded_track(deck);
        if (led >= LED_BEAT_JUMP_PAD_1 && led <= LED_BEAT_JUMP_PAD_8) {
            state = beat_jump_mode && loaded ? 1u : 0u;
        } else if (led >= LED_BEAT_JUMP_SHIFT_HELPER_7 &&
                   led <= LED_BEAT_JUMP_SHIFT_HELPER_8) {
            state = beat_jump_mode && s_deck_shift_held[deck] && loaded ? 1u : 0u;
        }
    }
    control_link_send_led_deck(led, state, deck);
}
