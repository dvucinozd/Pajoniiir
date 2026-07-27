/*
 * Host-only adapter: production publishes deck state from the actor loop.
 * Direct PC-test mutations bypass that loop, so publish immediately before the
 * test-facing Beat FX getter reads the coherent public snapshot.
 */
#define deck_core_test_get_beat_fx_state deck_core_test_get_beat_fx_state_unpublished
#include "../../firmware/main-deck-p4/components/deck_core/deck_core.c"
#undef deck_core_test_get_beat_fx_state

deck_core_beat_fx_state_t deck_core_test_get_beat_fx_state(void)
{
    publish_state_snapshot();
    return deck_core_get_beat_fx_state();
}
