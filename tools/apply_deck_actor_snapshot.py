#!/usr/bin/env python3
from pathlib import Path

P = Path(__file__).resolve().parents[1] / "firmware/main-deck-p4/components/deck_core/deck_core.c"


def once(s, old, new, label):
    n = s.count(old)
    if n != 1: raise RuntimeError(f"{label}: {n}")
    return s.replace(old, new, 1)


def replace_function(s, signature, replacement):
    start = s.find(signature)
    if start < 0: raise RuntimeError(f"missing {signature}")
    brace = s.find('{', start)
    depth = 0
    for i in range(brace, len(s)):
        if s[i] == '{': depth += 1
        elif s[i] == '}':
            depth -= 1
            if depth == 0:
                return s[:start] + replacement.rstrip() + s[i+1:]
    raise RuntimeError(f"unterminated {signature}")

s = P.read_text(encoding='utf-8')
s = once(s, '#define DECK_CORE_TEST_UI_COMMAND_QUEUE_LEN 32u\n',
'''#define DECK_CORE_TEST_UI_COMMAND_QUEUE_LEN 32u
#define DECK_CORE_INTERNAL_RESET_ID 0xFEu
#define DECK_CORE_RESET_TIMEOUT_MS 2000u
''', 'constants')
s = once(s, 'static deck_state_t     s_decks[DECK_CORE_DECK_COUNT];\n',
'''static deck_state_t     s_decks[DECK_CORE_DECK_COUNT];
static deck_state_t     s_published_decks[DECK_CORE_DECK_COUNT];
static deck_core_beat_fx_state_t s_published_beat_fx;
static uint32_t         s_published_heartbeat_tick;
static uint32_t         s_snapshot_seq;
static bool             s_snapshot_writer;
static SemaphoreHandle_t s_reset_done_sem;
''', 'snapshot declarations')

# Add published loop shadow after its type/state declaration.
s = once(s, 'static deck_loop_shadow_t s_loop_shadow[DECK_CORE_DECK_COUNT];\n',
'''static deck_loop_shadow_t s_loop_shadow[DECK_CORE_DECK_COUNT];
static deck_loop_shadow_t s_published_loop_shadow[DECK_CORE_DECK_COUNT];
''', 'loop snapshot')

# Insert snapshot helpers after loop-shadow type exists.
anchor = 'static deck_shifted_loop_roll_t s_shifted_loop_roll[DECK_CORE_DECK_COUNT];\n'
helpers = r'''static deck_shifted_loop_roll_t s_shifted_loop_roll[DECK_CORE_DECK_COUNT];

static void publish_state_snapshot(void)
{
    while (__atomic_exchange_n(&s_snapshot_writer, true, __ATOMIC_ACQ_REL)) {
        taskYIELD();
    }
    (void)__atomic_add_fetch(&s_snapshot_seq, 1u, __ATOMIC_RELEASE); /* odd */
    memcpy(s_published_decks, s_decks, sizeof(s_published_decks));
    memcpy(s_published_loop_shadow, s_loop_shadow, sizeof(s_published_loop_shadow));
    s_published_beat_fx = s_beat_fx;
    s_published_heartbeat_tick = (uint32_t)s_last_heartbeat_tick;
    (void)__atomic_add_fetch(&s_snapshot_seq, 1u, __ATOMIC_RELEASE); /* even */
    __atomic_store_n(&s_snapshot_writer, false, __ATOMIC_RELEASE);
}

static void copy_state_snapshot(uint8_t deck,
                                deck_state_t *out_deck,
                                deck_loop_shadow_t *out_loop,
                                deck_core_beat_fx_state_t *out_fx,
                                uint32_t *out_heartbeat)
{
    uint32_t before;
    uint32_t after;
    do {
        before = __atomic_load_n(&s_snapshot_seq, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        if (out_deck) *out_deck = s_published_decks[deck];
        if (out_loop) *out_loop = s_published_loop_shadow[deck];
        if (out_fx) *out_fx = s_published_beat_fx;
        if (out_heartbeat) *out_heartbeat = s_published_heartbeat_tick;
        after = __atomic_load_n(&s_snapshot_seq, __ATOMIC_ACQUIRE);
    } while (before != after || (after & 1u));
}
'''
s = once(s, anchor, helpers, 'snapshot helpers')

# Actor publishes completed previous mutation before blocking for next event.
s = once(s,
'''    ctrl_event_t ev;
    while (1) {
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) != pdTRUE) continue;
''',
'''    ctrl_event_t ev;
    while (1) {
        publish_state_snapshot();
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) != pdTRUE) continue;

        if (ev.type == CTRL_EV_STATE && ev.id == DECK_CORE_INTERNAL_RESET_ID) {
            const uint8_t idx = normalize_deck(ev.deck);
            init_deck_state(&s_decks[idx]);
            s_jog_touched[idx] = false;
            s_jog_hold_active[idx] = false;
            s_jog_scratch_active[idx] = false;
            if (s_sync_master_deck == idx) s_sync_master_deck = CTRL_DECK_NONE;
            memset(&s_loop_shadow[idx], 0, sizeof(s_loop_shadow[idx]));
            memset(&s_shifted_loop_roll[idx], 0, sizeof(s_shifted_loop_roll[idx]));
            memset(&s_pad_fx_led[idx], 0, sizeof(s_pad_fx_led[idx]));
            memset(&s_beat_loop_led[idx], 0, sizeof(s_beat_loop_led[idx]));
            memset(&s_censor_shadow[idx], 0, sizeof(s_censor_shadow[idx]));
            hot_cue_mask_cache_invalidate(idx);
            publish_state_snapshot();
            if (s_reset_done_sem) xSemaphoreGive(s_reset_done_sem);
            continue;
        }
''', 'actor reset command')

# Create reset semaphore and initial snapshot.
s = once(s,
'''    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_queue = xQueueCreate(CTRL_QUEUE_LEN, sizeof(ctrl_event_t));
''',
'''    s_mutex = xSemaphoreCreateMutex();
    s_reset_done_sem = xSemaphoreCreateBinary();
    if (!s_mutex || !s_reset_done_sem) return ESP_ERR_NO_MEM;
    publish_state_snapshot();

    s_queue = xQueueCreate(CTRL_QUEUE_LEN, sizeof(ctrl_event_t));
''', 'init snapshot reset sem')

s = replace_function(s, 'deck_state_t deck_core_get_deck_state(uint8_t deck)', r'''deck_state_t deck_core_get_deck_state(uint8_t deck)
{
    const uint8_t idx = normalize_deck(deck);
    deck_state_t snap = {0};
    uint32_t heartbeat_tick = 0u;
    copy_state_snapshot(idx, &snap, NULL, NULL, &heartbeat_tick);

    if (deck_uses_audio_engine(idx)) {
        snap.playing = audio_engine_deck_is_playing(idx);
        snap.position_ms = audio_engine_deck_position_ms(idx);
    }
    const TickType_t now = xTaskGetTickCount();
    if (heartbeat_tick != 0u) {
        const uint32_t age_ms = (uint32_t)((now - (TickType_t)heartbeat_tick) * portTICK_PERIOD_MS);
        snap.last_heartbeat_age_ms = age_ms;
        snap.control_link_connected = age_ms <= 10000u;
    } else {
        snap.last_heartbeat_age_ms = UINT32_MAX;
        snap.control_link_connected = false;
    }
    return snap;
}''')

s = replace_function(s, 'deck_core_beat_fx_state_t deck_core_get_beat_fx_state(void)', r'''deck_core_beat_fx_state_t deck_core_get_beat_fx_state(void)
{
    deck_core_beat_fx_state_t snap = {0};
    copy_state_snapshot(0u, NULL, NULL, &snap, NULL);
    return snap;
}''')

s = replace_function(s, 'deck_core_loop_display_t deck_core_get_loop_display(uint8_t deck)', r'''deck_core_loop_display_t deck_core_get_loop_display(uint8_t deck)
{
    deck_core_loop_display_t out = {0};
    if (deck >= DECK_CORE_DECK_COUNT) return out;

    deck_loop_shadow_t shadow = {0};
    copy_state_snapshot(deck, NULL, &shadow, NULL, NULL);
    bool active = false;
    uint32_t start_ms = 0u;
    uint32_t end_ms = 0u;
    if (read_active_loop(deck, &active, &start_ms, &end_ms) && active && end_ms > start_ms) {
        out.active = true;
        out.start_ms = start_ms;
        out.end_ms = end_ms;
    } else if (shadow.pending_in) {
        out.armed = true;
        out.start_ms = shadow.pending_start_ms;
    }
    return out;
}''')

s = replace_function(s, 'void deck_core_reset_deck(uint8_t deck)', r'''void deck_core_reset_deck(uint8_t deck)
{
#if defined(DECK_CORE_PC_TEST)
    const uint8_t idx = normalize_deck(deck);
    init_deck_state(&s_decks[idx]);
    s_jog_touched[idx] = false;
    s_jog_hold_active[idx] = false;
    s_jog_scratch_active[idx] = false;
    if (s_sync_master_deck == idx) s_sync_master_deck = CTRL_DECK_NONE;
    memset(&s_loop_shadow[idx], 0, sizeof(s_loop_shadow[idx]));
    memset(&s_shifted_loop_roll[idx], 0, sizeof(s_shifted_loop_roll[idx]));
    memset(&s_pad_fx_led[idx], 0, sizeof(s_pad_fx_led[idx]));
    memset(&s_beat_loop_led[idx], 0, sizeof(s_beat_loop_led[idx]));
    memset(&s_censor_shadow[idx], 0, sizeof(s_censor_shadow[idx]));
    hot_cue_mask_cache_invalidate(idx);
    publish_state_snapshot();
#else
    if (!s_queue || !s_reset_done_sem) return;
    while (xSemaphoreTake(s_reset_done_sem, 0) == pdTRUE) {}
    ctrl_event_t ev = {
        .type = CTRL_EV_STATE,
        .id = DECK_CORE_INTERNAL_RESET_ID,
        .deck = normalize_deck(deck),
    };
    if (xQueueSend(s_queue, &ev, portMAX_DELAY) != pdTRUE ||
        xSemaphoreTake(s_reset_done_sem, pdMS_TO_TICKS(DECK_CORE_RESET_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "deck %u actor reset timed out", (unsigned)ev.deck + 1u);
        return;
    }
    ESP_LOGI(TAG, "deck %u core reset", (unsigned)ev.deck + 1u);
#endif
}''')

P.write_text(s, encoding='utf-8')
print('deck actor snapshot patch applied')
