#!/usr/bin/env python3
"""Make physical control edges reliable without draining a shared queue."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "firmware/main-deck-p4/components/control_link/control_link_uart.c"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n != 1:
        raise RuntimeError(f"{label}: expected one match, found {n}")
    return text.replace(old, new, 1)


def replace_region(text: str, start_marker: str, end_marker: str, replacement: str) -> str:
    start = text.find(start_marker)
    end = text.find(end_marker, start)
    if start < 0 or end < 0:
        raise RuntimeError(f"region not found: {start_marker!r} .. {end_marker!r}")
    return text[:start] + replacement.rstrip() + "\n\n" + text[end:]


def main() -> None:
    text = SRC.read_text(encoding="utf-8")

    replacement = r'''typedef struct {
    bool valid;
    ctrl_event_t event;
} pending_value_t;

/* UART RX is the sole owner of these pending slots. Absolute values keep the
 * latest sample; jog deltas accumulate. No producer ever drains/reorders the
 * deck queue, so web/UI producers cannot race a remove-and-reinsert cycle. */
static pending_value_t s_pending_values[256];
static bool s_pending_jog_valid[256];
static int32_t s_pending_jog_delta[256];

static bool event_is_continuous_value(const ctrl_event_t *ev)
{
    if (!ev) return false;
    if (ev->type == CTRL_EV_JOG || ev->type == CTRL_EV_PITCH ||
        ev->type == CTRL_EV_HEARTBEAT) {
        return true;
    }
    if (ev->type != CTRL_EV_BUTTON) return false;
    switch (ev->id) {
    case CTRL_ID_CH1_VOLUME:
    case CTRL_ID_CH2_VOLUME:
    case CTRL_ID_CROSSFADER:
    case CTRL_ID_CH1_TRIM:
    case CTRL_ID_CH2_TRIM:
    case CTRL_ID_CH1_EQ_HIGH:
    case CTRL_ID_CH2_EQ_HIGH:
    case CTRL_ID_CH1_EQ_MID:
    case CTRL_ID_CH2_EQ_MID:
    case CTRL_ID_CH1_EQ_LOW:
    case CTRL_ID_CH2_EQ_LOW:
    case CTRL_ID_CH1_FILTER:
    case CTRL_ID_CH2_FILTER:
    case CTRL_ID_MASTER_VOLUME:
    case CTRL_ID_HEADPHONE_MIX:
        return true;
    default:
        return false;
    }
}

static int16_t clamp_jog_delta(int32_t value)
{
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

static void store_pending_continuous(const ctrl_event_t *ev)
{
    if (!ev) return;
    if (ev->type == CTRL_EV_JOG) {
        int32_t sum = s_pending_jog_delta[ev->id] + (int32_t)ev->value;
        if (sum > INT16_MAX) sum = INT16_MAX;
        if (sum < INT16_MIN) sum = INT16_MIN;
        s_pending_jog_delta[ev->id] = sum;
        s_pending_jog_valid[ev->id] = true;
    } else {
        s_pending_values[ev->id].event = *ev;
        s_pending_values[ev->id].valid = true;
    }
    s_event_coalesce_count++;
}

static void flush_pending_control_events(void)
{
    if (!s_event_queue) return;
    for (unsigned id = 0; id < 256u; ++id) {
        if (s_pending_jog_valid[id]) {
            ctrl_event_t ev = {
                .type = CTRL_EV_JOG,
                .id = (uint8_t)id,
                .value = clamp_jog_delta(s_pending_jog_delta[id]),
                .deck = control_link_id_deck((uint8_t)id),
                .control = control_link_id_control((uint8_t)id),
            };
            if (xQueueSend(s_event_queue, &ev, 0) != pdTRUE) return;
            s_pending_jog_valid[id] = false;
            s_pending_jog_delta[id] = 0;
        }
        if (s_pending_values[id].valid) {
            ctrl_event_t ev = s_pending_values[id].event;
            if (xQueueSend(s_event_queue, &ev, 0) != pdTRUE) return;
            s_pending_values[id].valid = false;
        }
    }
}

static bool enqueue_control_event(const ctrl_event_t *ev)
{
    if (!ev || !s_event_queue) return false;

    if (event_is_continuous_value(ev)) {
        if (xQueueSend(s_event_queue, ev, 0) == pdTRUE) return true;
        store_pending_continuous(ev);
        return true;
    }

    /* Button/state edges are lossless. Backpressure is preferable to a missing
     * release that leaves scratch, Censor, roll, Pad FX or SHIFT latched. */
    return xQueueSend(s_event_queue, ev, portMAX_DELAY) == pdTRUE;
}'''
    text = replace_region(
        text,
        "static bool event_is_high_rate(const ctrl_event_t *ev)",
        "static void dispatch_frame(const uint8_t *f)",
        replacement,
    )

    old_dispatch = """    if (xQueueSend(s_event_queue, &ev, 0) != pdTRUE &&\n        !enqueue_priority_touch(&ev) &&\n        !try_coalesce_latest_event(&ev)) {\n        s_event_drop_count++;\n        TickType_t now = xTaskGetTickCount();\n        if (now - s_last_warn >= pdMS_TO_TICKS(1000)) {\n            s_last_warn = now;\n            ESP_LOGW(TAG, \"control event queue full, drops=%\" PRIu32\n                     \" coalesced=%\" PRIu32 \" write_fail=%\" PRIu32,\n                     s_event_drop_count, s_event_coalesce_count, s_uart_write_fail_count);\n        }\n    }\n"""
    new_dispatch = """    flush_pending_control_events();\n    if (!enqueue_control_event(&ev)) {\n        s_event_drop_count++;\n        TickType_t now = xTaskGetTickCount();\n        if (now - s_last_warn >= pdMS_TO_TICKS(1000)) {\n            s_last_warn = now;\n            ESP_LOGW(TAG, \"control event enqueue failed, drops=%\" PRIu32\n                     \" coalesced=%\" PRIu32 \" write_fail=%\" PRIu32,\n                     s_event_drop_count, s_event_coalesce_count, s_uart_write_fail_count);\n        }\n    }\n"""
    text = replace_once(text, old_dispatch, new_dispatch, "dispatch queue policy")

    text = replace_once(
        text,
        """        for (int i = 0; i < n; i++) {\n            parse_byte(&st, buf[i]);\n        }\n""",
        """        for (int i = 0; i < n; i++) {\n            parse_byte(&st, buf[i]);\n        }\n        /* A pending final fader/jog sample must be delivered even when UART goes\n         * quiet immediately after the queue-full interval. */\n        flush_pending_control_events();\n""",
        "idle pending flush",
    )

    text = replace_once(
        text,
        """    ctrl_bulk_parser_reset(&s_bulk_parser);\n""",
        """    ctrl_bulk_parser_reset(&s_bulk_parser);\n    memset(s_pending_values, 0, sizeof(s_pending_values));\n    memset(s_pending_jog_valid, 0, sizeof(s_pending_jog_valid));\n    memset(s_pending_jog_delta, 0, sizeof(s_pending_jog_delta));\n""",
        "pending queue initialization",
    )

    SRC.write_text(text, encoding="utf-8")
    print("Applied reliable edge and producer-owned coalescing policy.")


if __name__ == "__main__":
    main()
