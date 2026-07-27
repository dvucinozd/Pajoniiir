/*
 * Host-only adapter: production publishes deck state from the actor loop.
 * Direct PC-test mutations bypass that loop, so mirror the actor publication
 * after every reset/event before public snapshot readers run.
 */
#define deck_core_test_reset deck_core_test_reset_unpublished
#define deck_core_test_apply_event deck_core_test_apply_event_unpublished
#define deck_core_test_get_beat_fx_state deck_core_test_get_beat_fx_state_unpublished
#include "../../firmware/main-deck-p4/components/deck_core/deck_core.c"
#undef deck_core_test_reset
#undef deck_core_test_apply_event
#undef deck_core_test_get_beat_fx_state

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
