#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Durable physical-level state shared by the S3 translator and P4 receiver.
 *
 * The caller must include its control_link.h first. Both targets intentionally
 * expose the same semantic IDs and packed PAD/EXT value helpers.
 *
 * Discrete commands are deliberately absent: collapsing two PLAY presses or
 * two BEAT JUMP presses would change behaviour. Only controls whose release is
 * required to leave a held mode belong here.
 */

#define CONTROL_HELD_SIMPLE_COUNT       6u
#define CONTROL_HELD_PER_DECK_PAD_COUNT 24u
#define CONTROL_HELD_STATE_COUNT        \
    (CONTROL_HELD_SIMPLE_COUNT + 2u * CONTROL_HELD_PER_DECK_PAD_COUNT)

typedef struct {
    bool observed;
    bool scheduled_valid;
    bool dirty;
    uint8_t id;
    int16_t desired_value;
    int16_t scheduled_value;
    uint8_t sequence;
} control_held_state_slot_t;

typedef struct {
    control_held_state_slot_t slots[CONTROL_HELD_STATE_COUNT];
} control_held_state_reconciler_t;

static inline int control_held_state_key(uint8_t id, int16_t value)
{
    switch (id) {
    case CTRL_ID_DECK1_JOG_TOUCH: return 0;
    case CTRL_ID_DECK2_JOG_TOUCH: return 1;
    case CTRL_ID_DECK1_SHIFT: return 2;
    case CTRL_ID_DECK2_SHIFT: return 3;
    case CTRL_ID_DECK1_EXT_ACTION:
        return CTRL_DECK_EXT_ACTION(value) == CTRL_DECK_EXT_ACTION_CENSOR ? 4 : -1;
    case CTRL_ID_DECK2_EXT_ACTION:
        return CTRL_DECK_EXT_ACTION(value) == CTRL_DECK_EXT_ACTION_CENSOR ? 5 : -1;
    case CTRL_ID_DECK1_PAD_ACTION:
    case CTRL_ID_DECK2_PAD_ACTION:
        break;
    default:
        return -1;
    }

    const uint8_t mode = CTRL_PAD_ACTION_MODE(value);
    const uint8_t pad = CTRL_PAD_ACTION_PAD(value);
    unsigned mode_offset;
    if (mode == CTRL_PAD_MODE_PAD_FX1) {
        mode_offset = 0u;
    } else if (mode == CTRL_PAD_MODE_PAD_FX2) {
        mode_offset = 8u;
    } else if (mode == CTRL_PAD_MODE_BEAT_LOOP &&
               CTRL_PAD_ACTION_SHIFTED(value)) {
        mode_offset = 16u;
    } else {
        return -1;
    }

    const unsigned deck_offset =
        id == CTRL_ID_DECK2_PAD_ACTION ? CONTROL_HELD_PER_DECK_PAD_COUNT : 0u;
    return (int)(CONTROL_HELD_SIMPLE_COUNT + deck_offset + mode_offset + pad);
}

static inline int16_t control_held_state_release_value(uint8_t id, int16_t value)
{
    if (id == CTRL_ID_DECK1_EXT_ACTION || id == CTRL_ID_DECK2_EXT_ACTION) {
        return (int16_t)(value & 0x7F);
    }
    if (id == CTRL_ID_DECK1_PAD_ACTION || id == CTRL_ID_DECK2_PAD_ACTION) {
        return (int16_t)(value & ~0x80);
    }
    return 0;
}

static inline void control_held_state_reset(control_held_state_reconciler_t *state)
{
    if (state) {
        memset(state, 0, sizeof(*state));
    }
}

static inline int control_held_state_observe(control_held_state_reconciler_t *state,
                                             uint8_t id,
                                             int16_t value,
                                             uint8_t sequence)
{
    if (!state) {
        return -1;
    }
    const int key = control_held_state_key(id, value);
    if (key < 0) {
        return -1;
    }
    control_held_state_slot_t *slot = &state->slots[key];
    slot->observed = true;
    slot->id = id;
    slot->desired_value = value;
    slot->sequence = sequence;
    slot->dirty = !slot->scheduled_valid ||
                  slot->scheduled_value != slot->desired_value;
    return key;
}

static inline void control_held_state_mark_scheduled(
    control_held_state_reconciler_t *state,
    int key,
    int16_t value)
{
    if (!state || key < 0 || key >= (int)CONTROL_HELD_STATE_COUNT) {
        return;
    }
    control_held_state_slot_t *slot = &state->slots[key];
    slot->scheduled_valid = true;
    slot->scheduled_value = value;
    slot->dirty = slot->desired_value != slot->scheduled_value;
}

static inline bool control_held_state_next_dirty(
    const control_held_state_reconciler_t *state,
    size_t *cursor,
    int *key,
    uint8_t *id,
    int16_t *value,
    uint8_t *sequence)
{
    if (!state || !cursor) {
        return false;
    }
    for (size_t i = *cursor; i < CONTROL_HELD_STATE_COUNT; ++i) {
        const control_held_state_slot_t *slot = &state->slots[i];
        if (!slot->observed || !slot->dirty) {
            continue;
        }
        *cursor = i + 1u;
        if (key) *key = (int)i;
        if (id) *id = slot->id;
        if (value) *value = slot->desired_value;
        if (sequence) *sequence = slot->sequence;
        return true;
    }
    *cursor = CONTROL_HELD_STATE_COUNT;
    return false;
}

static inline bool control_held_state_next_observed(
    const control_held_state_reconciler_t *state,
    size_t *cursor,
    uint8_t *id,
    int16_t *value)
{
    if (!state || !cursor) {
        return false;
    }
    for (size_t i = *cursor; i < CONTROL_HELD_STATE_COUNT; ++i) {
        const control_held_state_slot_t *slot = &state->slots[i];
        if (!slot->observed) {
            continue;
        }
        *cursor = i + 1u;
        if (id) *id = slot->id;
        if (value) *value = slot->desired_value;
        return true;
    }
    *cursor = CONTROL_HELD_STATE_COUNT;
    return false;
}

static inline void control_held_state_release_all(
    control_held_state_reconciler_t *state,
    uint8_t sequence)
{
    if (!state) {
        return;
    }
    for (size_t i = 0; i < CONTROL_HELD_STATE_COUNT; ++i) {
        control_held_state_slot_t *slot = &state->slots[i];
        if (!slot->observed) {
            continue;
        }
        slot->desired_value =
            control_held_state_release_value(slot->id, slot->desired_value);
        slot->sequence = sequence;
        slot->dirty = !slot->scheduled_valid ||
                      slot->scheduled_value != slot->desired_value;
    }
}
