/*
 * Host-only adapter: production publishes deck state from the actor loop.
 * Direct PC-test mutations bypass that loop, so mirror actor publication after
 * every reset/event. The LED callback is also refreshed from live actor state,
 * matching the production deck_core_live_led.c wrapper.
 */
#include "../../firmware/main-deck-p4/components/deck_core/include/deck_core.h"
#include "../../firmware/main-deck-p4/components/control_link/include/flx4_led_snapshot.h"

static esp_err_t deck_core_test_live_led_publish(
    flx4_led_publisher_t *publisher,
    const flx4_led_snapshot_input_t *input,
    bool force,
    flx4_led_send_fn_t send,
    void *ctx);

#define deck_core_test_reset deck_core_test_reset_unpublished
#define deck_core_test_apply_event deck_core_test_apply_event_unpublished
#define deck_core_test_get_beat_fx_state deck_core_test_get_beat_fx_state_unpublished
#define flx4_led_publisher_publish deck_core_test_live_led_publish
#include "../../firmware/main-deck-p4/components/deck_core/deck_core.c"
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
    return flx4_led_publisher_publish(publisher, &live, force, send, ctx);
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
