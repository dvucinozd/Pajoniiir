/* Host-only adapter matching the production deck actor publication model. */
#include "../../firmware/main-deck-p4/components/deck_core/include/deck_core.h"
#include "../../firmware/main-deck-p4/components/control_link/include/flx4_led_snapshot.h"

static esp_err_t deck_core_test_live_led_publish(
    flx4_led_publisher_t *publisher,
    const flx4_led_snapshot_input_t *input,
    bool force,
    flx4_led_send_fn_t send,
    void *ctx);
static void deck_core_test_live_send_led_deck(led_id_t led, uint8_t state, uint8_t deck);

#define deck_core_test_reset deck_core_test_reset_unpublished
#define deck_core_test_apply_event deck_core_test_apply_event_unpublished
#define deck_core_test_get_beat_fx_state deck_core_test_get_beat_fx_state_unpublished
#define flx4_led_publisher_publish deck_core_test_live_led_publish
#define control_link_send_led_deck deck_core_test_live_send_led_deck
#include "../../firmware/main-deck-p4/components/deck_core/deck_core.c"
#undef control_link_send_led_deck
#undef flx4_led_publisher_publish
#undef deck_core_test_reset
#undef deck_core_test_apply_event
#undef deck_core_test_get_beat_fx_state

static esp_err_t deck_core_test_live_led_publish(
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
    publish_state_snapshot();
    return flx4_led_publisher_publish(publisher, &live, force, send, ctx);
}

static void deck_core_test_live_send_led_deck(led_id_t led, uint8_t state, uint8_t deck)
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

void deck_core_test_reset(void)
{
    deck_core_test_reset_unpublished();
    publish_state_snapshot();
}

void deck_core_test_apply_event(const ctrl_event_t *ev)
{
    deck_core_test_apply_event_unpublished(ev);
    publish_state_snapshot();
}

deck_core_beat_fx_state_t deck_core_test_get_beat_fx_state(void)
{
    return deck_core_get_beat_fx_state();
}
