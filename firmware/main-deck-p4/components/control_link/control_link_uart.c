#include "control_link.h"
#include "control_link_p4_diagnostics.h"
#include "control_state_reconciler.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>

/* Referenced only by ESP_LOG*, which the PC host stubs compile away. */
__attribute__((unused)) static const char *TAG = "ctrl_link";

// ─── Pin assignment ───────────────────────────────────────────────────────────
// JP1 header pins — verify on hardware before wiring.
// S3 GPIO40 TX → P4 GPIO28 RX
// S3 GPIO41 RX ← P4 GPIO29 TX
#define PIN_UART_RX  GPIO_NUM_28
#define PIN_UART_TX  GPIO_NUM_29

#define UART_PORT    UART_NUM_1
/* 460800 (4x the original 115200) over the short JP1 board-to-board link: cuts a
 * full 16 KB 0xA6 profile transfer from ~1.5 s to ~0.4 s so it stops starving
 * LED/event frames for that window. MUST match the S3 side. A framing error from
 * marginal signal integrity is self-healing (the RX parser resyncs on the next
 * 0xA5/0xA6 start byte), so a bad wire degrades rather than wedges the link. */
#define UART_BAUD    460800
/* 1 KB rings (was 256 B ≈ 22 ms at 115200): headroom for the 0xA6 bulk profile
 * stream and brief RX-task stalls so event/LED frames are not dropped. */
#define RX_BUF_SIZE  1024
#define TX_BUF_SIZE  1024

/* How long a button/state edge may apply backpressure to the UART RX task when
 * the deck queue is full.
 *
 * The RX task is the only thing draining the serial FIFO, so while it waits the
 * FIFO fills. The bound therefore has to be a fraction of how long the ring
 * takes to overflow, not a comfortable-sounding number: waiting longer than the
 * ring holds guarantees losing the very release edges this backpressure exists
 * to protect, plus the 0xA6 bulk stream and the heartbeat.
 *
 * At 8N1 a byte is 10 bits, so the ring holds RX_BUF_SIZE * 10 / UART_BAUD
 * seconds of continuous traffic - about 22 ms for 1 KiB at 460800. A quarter of
 * that leaves the FIFO three quarters empty in the worst case, which is enough
 * to ride out a consumer hiccup without ever being the cause of an overrun. */
#define CTRL_RX_RING_MS ((RX_BUF_SIZE * 10u * 1000u) / UART_BAUD)
#define CTRL_EDGE_BACKPRESSURE_MS (CTRL_RX_RING_MS / 4u)

_Static_assert(CTRL_EDGE_BACKPRESSURE_MS >= 1u,
               "backpressure must be at least one millisecond to be useful");
_Static_assert(CTRL_EDGE_BACKPRESSURE_MS * 4u <= CTRL_RX_RING_MS,
               "a wait longer than a quarter of the RX ring can itself overrun the FIFO");

static QueueHandle_t    s_event_queue;
static atomic_uint_fast8_t s_seq = 0;
static uint32_t s_uart_write_fail_count;
static uint32_t s_event_drop_count;
static uint32_t s_event_coalesce_count;
static uint32_t s_edge_backpressure_timeout_count;
static TickType_t s_last_warn;
static ctrl_bulk_parser_t s_bulk_parser;
static control_link_descriptor_cb_t s_descriptor_cb;
static control_link_controller_state_cb_t s_controller_state_cb;
static control_link_profile_reply_cb_t s_profile_reply_cb;
static portMUX_TYPE s_firmware_mux = portMUX_INITIALIZER_UNLOCKED;
static ctrl_firmware_report_t s_s3_firmware;
static bool s_s3_firmware_received;
static portMUX_TYPE s_rx_stats_mux = portMUX_INITIALIZER_UNLOCKED;
static control_link_rx_stats_t s_rx_stats;

// ─── TX helpers ───────────────────────────────────────────────────────────────

static esp_err_t send_bulk_frame(const uint8_t *frame, size_t len, const char *what)
{
    (void)what;   /* names the frame in ESP_LOGW, which the host stubs elide */
    if (len == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    int written = uart_write_bytes(UART_PORT, frame, len);
    if (written == (int)len) {
        return ESP_OK;
    }
    s_uart_write_fail_count++;
    TickType_t now = xTaskGetTickCount();
    if (now - s_last_warn >= pdMS_TO_TICKS(1000)) {
        s_last_warn = now;
        ESP_LOGW(TAG, "%s UART short write (%d/%u)", what, written, (unsigned)len);
    }
    return ESP_FAIL;
}

static uint8_t next_seq(void)
{
    return atomic_fetch_add_explicit(&s_seq, 1, memory_order_relaxed);
}

static void send_frame(uint8_t type, uint8_t id, int16_t value)
{
    uint8_t seq = atomic_fetch_add_explicit(&s_seq, 1, memory_order_relaxed);
    uint16_t v  = (uint16_t)value;
    uint8_t frame[CTRL_FRAME_LEN];

    frame[0] = CTRL_FRAME_START;
    frame[1] = type;
    frame[2] = id;
    frame[3] = (uint8_t)(v & 0xFF);
    frame[4] = (uint8_t)((v >> 8) & 0xFF);
    frame[5] = seq;
    frame[6] = frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5];

    int written = uart_write_bytes(UART_PORT, frame, CTRL_FRAME_LEN);
    if (written != CTRL_FRAME_LEN) {
        s_uart_write_fail_count++;
        TickType_t now = xTaskGetTickCount();
        if (now - s_last_warn >= pdMS_TO_TICKS(1000)) {
            s_last_warn = now;
            ESP_LOGW(TAG, "LED UART short write (%d/%d), write_fail=%" PRIu32 " event_drop=%" PRIu32,
                     written, CTRL_FRAME_LEN, s_uart_write_fail_count, s_event_drop_count);
        }
    }
}

void control_link_send_led_deck(led_id_t led, uint8_t state, uint8_t deck)
{
    int16_t val = (int16_t)(state | (deck << 8));
    send_frame(CTRL_TYPE_LED, (uint8_t)led, val);
}

void control_link_send_led(led_id_t led, uint8_t state)
{
    control_link_send_led_deck(led, state, 0);
}

void control_link_send_state(uint8_t id, int16_t value)
{
    send_frame(CTRL_TYPE_STATE, id, value);
}

void control_link_set_descriptor_report_cb(control_link_descriptor_cb_t cb)
{
    s_descriptor_cb = cb;
}

void control_link_set_controller_state_cb(control_link_controller_state_cb_t cb)
{
    s_controller_state_cb = cb;
}

void control_link_set_profile_reply_cb(control_link_profile_reply_cb_t cb)
{
    s_profile_reply_cb = cb;
}

bool control_link_get_s3_firmware_report(ctrl_firmware_report_t *out_report)
{
    if (!out_report) return false;
    portENTER_CRITICAL(&s_firmware_mux);
    bool received = s_s3_firmware_received;
    *out_report = s_s3_firmware;
    portEXIT_CRITICAL(&s_firmware_mux);
    return received;
}

void control_link_get_rx_stats(control_link_rx_stats_t *out_stats)
{
    if (!out_stats) {
        return;
    }
    portENTER_CRITICAL(&s_rx_stats_mux);
    *out_stats = s_rx_stats;
    portEXIT_CRITICAL(&s_rx_stats_mux);
}

static void record_rx_frame(uint8_t sequence, bool bulk)
{
    portENTER_CRITICAL(&s_rx_stats_mux);
    control_link_rx_stats_record_frame(&s_rx_stats, sequence, bulk);
    portEXIT_CRITICAL(&s_rx_stats_mux);
}

static void record_rx_error(bool bulk)
{
    portENTER_CRITICAL(&s_rx_stats_mux);
    control_link_rx_stats_record_error(&s_rx_stats, bulk);
    portEXIT_CRITICAL(&s_rx_stats_mux);
}

esp_err_t control_link_send_profile_begin(uint32_t total_size, uint32_t crc32,
                                          uint16_t vid, uint16_t pid)
{
    uint8_t frame[CTRL_BULK_MAX_FRAME];
    size_t len = ctrl_bulk_build_profile_begin(frame, sizeof(frame), next_seq(),
                                               total_size, crc32, vid, pid);
    return send_bulk_frame(frame, len, "profile begin");
}

esp_err_t control_link_send_profile_chunk(uint32_t offset, const uint8_t *data,
                                          size_t len)
{
    uint8_t frame[CTRL_BULK_MAX_FRAME];
    size_t n = ctrl_bulk_build_profile_chunk(frame, sizeof(frame), next_seq(),
                                             offset, data, len);
    return send_bulk_frame(frame, n, "profile chunk");
}

esp_err_t control_link_send_profile_simple(uint8_t type)
{
    uint8_t frame[CTRL_BULK_MAX_FRAME];
    size_t len = ctrl_bulk_build_profile_simple(frame, sizeof(frame), next_seq(),
                                                type);
    return send_bulk_frame(frame, len, "profile ctrl");
}

// ─── RX parser ────────────────────────────────────────────────────────────────

typedef struct {
    uint8_t buf[CTRL_FRAME_LEN];
    int     pos;
} rx_state_t;

typedef struct {
    bool valid;
    ctrl_event_t event;
} pending_value_t;

/* UART RX is the sole owner of these pending slots. No producer ever drains or
 * reorders the deck queue, so web/UI producers cannot race a remove-and-reinsert
 * cycle.
 *
 * Slots are keyed by id. That is safe only because every control id is
 * namespaced (CTRL_NS_DECK1/DECK2/MIXER/BROWSER); the one exception was the pair
 * of unnamespaced id-0 events, CTRL_TYPE_PITCH and CTRL_TYPE_HEARTBEAT, which
 * shared slot 0 and overwrote each other. A lost pitch sample leaves the deck at
 * the wrong tempo until the operator touches the fader again, so the heartbeat
 * is now handled as its own class below rather than competing for the slot. */
static pending_value_t s_pending_values[256];
static bool s_pending_delta_valid[256];
static int32_t s_pending_delta[256];
static control_held_state_reconciler_t s_held_states;

/* A liveness ping. It carries no state the deck needs, and the next one arrives
 * within a second, so under queue pressure it is simply dropped: coalescing it
 * would cost a slot it cannot use, and applying backpressure for it would hold
 * the RX task for a message whose whole point is that it is cheap. */
static bool event_is_droppable_ping(const ctrl_event_t *ev)
{
    return ev && ev->type == CTRL_EV_HEARTBEAT;
}

/* Relative controls: what is held back must be summed, because each event is a
 * movement and dropping one loses distance. Everything else in the continuous
 * class is absolute, where only the newest sample matters. */
static bool event_is_relative_delta(const ctrl_event_t *ev)
{
    return ev && (ev->type == CTRL_EV_JOG || ev->type == CTRL_EV_BROWSE);
}

static bool event_is_continuous_value(const ctrl_event_t *ev)
{
    if (!ev) return false;
    /* State reports are absolute snapshots. The latest connection/AP state must
     * survive pressure, but replaying an older intermediate state is useless. */
    if (ev->type == CTRL_EV_STATE) {
        return true;
    }
    /* BROWSE belongs here: a library-browse spin is a stream of deltas, and
     * without it each tick took the lossless path and applied backpressure to
     * the RX task while the operator was simply scrolling. */
    if (ev->type == CTRL_EV_JOG || ev->type == CTRL_EV_BROWSE ||
        ev->type == CTRL_EV_PITCH) {
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

/* Lowest occupied slot, so the flush does not scan all 256 on every pass. Only
 * ever moves down on a store; the flush recomputes it. Purely a hint - the scan
 * still checks each slot's own valid flag. */
static unsigned s_pending_first_key = 256u;

static bool flush_pending_held_states(void)
{
    size_t cursor = 0u;
    int key;
    uint8_t id;
    int16_t value;
    uint8_t sequence;
    while (control_held_state_next_dirty(&s_held_states, &cursor, &key,
                                         &id, &value, &sequence)) {
        ctrl_event_t ev = {
            .type = CTRL_EV_BUTTON,
            .id = id,
            .value = value,
            .seq = sequence,
            .deck = control_link_id_deck(id),
            .control = control_link_id_control(id),
        };
        if (xQueueSend(s_event_queue, &ev, 0) != pdTRUE) {
            return false;
        }
        control_held_state_mark_scheduled(&s_held_states, key, value);
    }
    return true;
}

static void store_pending_continuous(const ctrl_event_t *ev)
{
    if (!ev) return;
    const unsigned key = ev->id;
    if (key < s_pending_first_key) s_pending_first_key = key;
    if (event_is_relative_delta(ev)) {
        int32_t sum = s_pending_delta[key] + (int32_t)ev->value;
        if (sum > INT16_MAX) sum = INT16_MAX;
        if (sum < INT16_MIN) sum = INT16_MIN;
        s_pending_delta[key] = sum;
        /* Keep the rest of the event so the flush can rebuild it exactly, rather
         * than re-deriving deck/control from the id. */
        s_pending_values[key].event = *ev;
        s_pending_delta_valid[key] = true;
    } else {
        s_pending_values[key].event = *ev;
        s_pending_values[key].valid = true;
    }
    s_event_coalesce_count++;
}

static void flush_pending_control_events(void)
{
    if (!s_event_queue) return;
    /* Physical levels outrank fader/jog backlog: a delayed release can mute
     * scratch, Censor, roll or Pad FX indefinitely. */
    if (!flush_pending_held_states()) {
        return;
    }
    unsigned first_remaining = 256u;
    for (unsigned key = s_pending_first_key; key < 256u; ++key) {
        if (s_pending_delta_valid[key]) {
            ctrl_event_t ev = s_pending_values[key].event;
            ev.value = clamp_jog_delta(s_pending_delta[key]);
            if (xQueueSend(s_event_queue, &ev, 0) != pdTRUE) {
                s_pending_first_key = key;
                return;
            }
            s_pending_delta_valid[key] = false;
            s_pending_delta[key] = 0;
        }
        if (s_pending_values[key].valid) {
            ctrl_event_t ev = s_pending_values[key].event;
            if (xQueueSend(s_event_queue, &ev, 0) != pdTRUE) {
                s_pending_first_key = key;
                return;
            }
            s_pending_values[key].valid = false;
        }
        if (first_remaining == 256u &&
            (s_pending_delta_valid[key] || s_pending_values[key].valid)) {
            first_remaining = key;
        }
    }
    s_pending_first_key = first_remaining;
}

static bool enqueue_control_event(const ctrl_event_t *ev)
{
    if (!ev || !s_event_queue) return false;

    /* A ping is worth neither a slot nor a wait: drop it and count it. */
    if (event_is_droppable_ping(ev)) {
        return xQueueSend(s_event_queue, ev, 0) == pdTRUE;
    }

    if (event_is_continuous_value(ev)) {
        if (xQueueSend(s_event_queue, ev, 0) == pdTRUE) return true;
        store_pending_continuous(ev);
        return true;
    }

    const int held_key =
        ev->type == CTRL_EV_BUTTON
            ? control_held_state_observe(&s_held_states, ev->id, ev->value, ev->seq)
            : -1;
    if (held_key >= 0 && !s_held_states.slots[held_key].dirty) {
        /* Periodic S3 snapshots intentionally repeat physical levels so a
         * P4-only reboot can recover. Suppress a level already scheduled in
         * this P4 lifetime; repeated press handlers are not all commands. */
        s_event_coalesce_count++;
        return true;
    }

    /* Held levels get bounded backpressure plus a durable desired-state slot.
     * When the wait expires the UART parser keeps draining and a later pump
     * reconciles the newest physical level. */
    if (xQueueSend(s_event_queue, ev, pdMS_TO_TICKS(CTRL_EDGE_BACKPRESSURE_MS)) == pdTRUE) {
        if (held_key >= 0) {
            control_held_state_mark_scheduled(&s_held_states, held_key, ev->value);
        }
        return true;
    }
    s_edge_backpressure_timeout_count++;
    if (held_key >= 0) {
        s_event_coalesce_count++;
        return true;
    }
    /* Discrete commands retain FIFO semantics: silently collapsing repeated
     * presses would change their meaning. A future ACK/retry command channel
     * can strengthen this bounded failure path without conflating it with held
     * state reconciliation. */
    return false;
}

static void dispatch_frame(const uint8_t *f)
{
    ctrl_event_t ev = {
        .id    = f[2],
        .value = (int16_t)((uint16_t)f[3] | ((uint16_t)f[4] << 8)),
        .seq   = f[5],
        .deck  = control_link_id_deck(f[2]),
        .control = control_link_id_control(f[2]),
    };

    switch (f[1]) {
    case CTRL_TYPE_BUTTON:
        ev.type = CTRL_EV_BUTTON;
        break;
    case CTRL_TYPE_ENCODER:
        if (ev.id == 0 || control_link_id_is_deck_jog(ev.id)) {
            ev.type = CTRL_EV_JOG;
        } else if (ev.id == 1 ||
                   ev.id == CTRL_ID_BROWSE_DELTA ||
                   ev.id == CTRL_ID_BROWSE_SHIFT_DELTA) {
            ev.type = CTRL_EV_BROWSE;
        } else {
            ESP_LOGW(TAG, "unknown encoder id %u", (unsigned)ev.id);
            return;
        }
        break;
    case CTRL_TYPE_PITCH:
        ev.type = CTRL_EV_PITCH;
        break;
    case CTRL_TYPE_HEARTBEAT:
        ev.type = CTRL_EV_HEARTBEAT;
        break;
    case CTRL_TYPE_STATE:
        ev.type = CTRL_EV_STATE;
        break;
    default:
        ESP_LOGW(TAG, "unknown frame type 0x%02x", f[1]);
        return;
    }

    if (ev.type == CTRL_EV_STATE && ev.id == CTRL_ID_FLX4_CONNECTION &&
        (ev.value == CTRL_FLX4_CONNECTED ||
         ev.value == CTRL_FLX4_DISCONNECTED) &&
        s_controller_state_cb) {
        s_controller_state_cb(ev.value == CTRL_FLX4_CONNECTED);
    }

    if (ev.type == CTRL_EV_STATE && ev.id == CTRL_ID_FLX4_CONNECTION &&
        ev.value == CTRL_FLX4_DISCONNECTED) {
        /* A disconnected controller cannot provide its final Note-Off frames.
         * Convert every observed held input to release before the connection
         * snapshot reaches deck_core. */
        control_held_state_release_all(&s_held_states, ev.seq);
    }

    flush_pending_control_events();
    if (!enqueue_control_event(&ev)) {
        s_event_drop_count++;
        TickType_t now = xTaskGetTickCount();
        if (now - s_last_warn >= pdMS_TO_TICKS(1000)) {
            s_last_warn = now;
            ESP_LOGW(TAG, "control event enqueue failed, drops=%" PRIu32
                     " coalesced=%" PRIu32 " write_fail=%" PRIu32
                     " edge_timeouts=%" PRIu32,
                     s_event_drop_count, s_event_coalesce_count,
                     s_uart_write_fail_count, s_edge_backpressure_timeout_count);
        }
    }
}

static void handle_bulk_frame(const uint8_t *frame, size_t frame_len)
{
    switch (frame[1]) {
    case CTRL_BULK_TYPE_CONTROLLER_DESCRIPTOR: {
        ctrl_descriptor_report_t rep;
        if (!ctrl_bulk_decode_descriptor(frame, frame_len, &rep)) {
            ESP_LOGW(TAG, "bad controller descriptor frame (len=%u)",
                     (unsigned)frame_len);
            return;
        }
        ESP_LOGI(TAG, "controller descriptor: VID=0x%04X PID=0x%04X caps=0x%04X '%s'",
                 rep.vid, rep.pid, rep.caps, rep.product);
        if (s_descriptor_cb) {
            s_descriptor_cb(&rep);
        }
        break;
    }
    case CTRL_BULK_TYPE_PROFILE_ACK: {
        uint8_t acked;
        if (ctrl_bulk_decode_profile_ack(frame, frame_len, &acked) &&
            s_profile_reply_cb) {
            s_profile_reply_cb(true, acked, CTRL_PROFILE_NACK_NONE);
        }
        break;
    }
    case CTRL_BULK_TYPE_PROFILE_NACK: {
        uint8_t nacked, reason;
        if (ctrl_bulk_decode_profile_nack(frame, frame_len, &nacked, &reason)) {
            ESP_LOGW(TAG, "profile NACK type=0x%02X reason=%u", nacked, reason);
            if (s_profile_reply_cb) {
                s_profile_reply_cb(false, nacked, reason);
            }
        }
        break;
    }
    case CTRL_BULK_TYPE_PROFILE_STATUS: {
        uint8_t state;
        uint16_t vid, pid;
        if (ctrl_bulk_decode_profile_status(frame, frame_len, &state, &vid, &pid)) {
            ESP_LOGI(TAG, "profile status=%u VID=0x%04X PID=0x%04X", state, vid, pid);
        }
        break;
    }
    case CTRL_BULK_TYPE_FIRMWARE_REPORT: {
        ctrl_firmware_report_t report;
        if (!ctrl_bulk_decode_firmware_report(frame, frame_len, &report)) {
            ESP_LOGW(TAG, "bad S3 firmware report (len=%u)", (unsigned)frame_len);
            return;
        }
        portENTER_CRITICAL(&s_firmware_mux);
        bool changed = !s_s3_firmware_received ||
                       s_s3_firmware.slot != report.slot ||
                       s_s3_firmware.state != report.state ||
                       strcmp(s_s3_firmware.version, report.version) != 0;
        s_s3_firmware = report;
        s_s3_firmware_received = true;
        portEXIT_CRITICAL(&s_firmware_mux);
        if (changed) {
            ESP_LOGW(TAG, "S3 firmware version=%s slot=%u state=%u",
                     report.version, report.slot, report.state);
        }
        break;
    }
    default:
        ESP_LOGW(TAG, "unknown bulk frame type 0x%02X", frame[1]);
        break;
    }
}

static void parse_byte(rx_state_t *st, uint8_t b)
{
    /* Route 0xA6 bulk frames to the bulk parser; it owns the stream while a
     * bulk frame is in progress. 0xA5 event frames keep the existing path. */
    if (st->pos == 0) {
        if (s_bulk_parser.pos > 0 || b == CTRL_BULK_FRAME_START) {
            int r = ctrl_bulk_parser_feed(&s_bulk_parser, b);
            if (r > 0) {
                record_rx_frame(s_bulk_parser.buf[2], true);
                handle_bulk_frame(s_bulk_parser.buf, (size_t)r);
            } else if (r < 0) {
                record_rx_error(true);
                ESP_LOGW(TAG, "bulk frame CRC/format error");
            }
            return;
        }
        if (b != CTRL_FRAME_START) return;
    }

    st->buf[st->pos++] = b;
    if (st->pos < CTRL_FRAME_LEN) return;

    st->pos = 0;

    uint8_t chk = st->buf[1] ^ st->buf[2] ^ st->buf[3] ^ st->buf[4] ^ st->buf[5];
    if (chk != st->buf[6]) {
        record_rx_error(false);
        ESP_LOGW(TAG, "bad checksum (got 0x%02x expected 0x%02x)", st->buf[6], chk);
        /* A dropped byte shifts framing: the real frame start is likely inside
           the bytes just rejected. Resync on it instead of discarding all 7,
           otherwise one lost byte can corrupt a long run of frames. */
        for (int i = 1; i < CTRL_FRAME_LEN; i++) {
            if (st->buf[i] == CTRL_FRAME_START) {
                memmove(st->buf, &st->buf[i], (size_t)(CTRL_FRAME_LEN - i));
                st->pos = CTRL_FRAME_LEN - i;
                break;
            }
        }
        return;
    }

    record_rx_frame(st->buf[5], false);
    dispatch_frame(st->buf);
}

/* One pass of the receive loop: read whatever the driver has, parse it, then
 * release anything the queue-full path held back. Split out of the task loop so
 * a host test can run exactly one pass — the loop itself never returns, and the
 * behaviour worth testing is what one pass does to the queue, not the looping.
 *
 * `st` is static rather than local so partial frames survive across passes, the
 * same way they do across iterations of the task loop. The RX task is its sole
 * caller in firmware. */
static void uart_rx_pump(void)
{
    static rx_state_t st;
    uint8_t buf[64];

    int n = uart_read_bytes(UART_PORT, buf, sizeof(buf), pdMS_TO_TICKS(20));
    for (int i = 0; i < n; i++) {
        parse_byte(&st, buf[i]);
    }
    /* A pending final fader/jog sample must be delivered even when UART goes
     * quiet immediately after the queue-full interval. */
    flush_pending_control_events();
}

#if defined(CONTROL_LINK_HOST_TEST)
void control_link_test_pump_rx(void)
{
    uart_rx_pump();
}
#endif

static void uart_rx_task(void *arg)
{
    (void)arg;
    while (1) {
        uart_rx_pump();
    }
}

// ─── Init ─────────────────────────────────────────────────────────────────────

esp_err_t control_link_init(QueueHandle_t ctrl_event_queue)
{
    if (!ctrl_event_queue) return ESP_ERR_INVALID_ARG;
    s_event_queue = ctrl_event_queue;
    portENTER_CRITICAL(&s_rx_stats_mux);
    control_link_rx_stats_reset(&s_rx_stats);
    portEXIT_CRITICAL(&s_rx_stats_mux);
    ctrl_bulk_parser_reset(&s_bulk_parser);
    memset(s_pending_values, 0, sizeof(s_pending_values));
    memset(s_pending_delta_valid, 0, sizeof(s_pending_delta_valid));
    memset(s_pending_delta, 0, sizeof(s_pending_delta));
    control_held_state_reset(&s_held_states);
    s_pending_first_key = 256u;
    s_edge_backpressure_timeout_count = 0u;

    uart_config_t ucfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t rc = uart_driver_install(UART_PORT, RX_BUF_SIZE, TX_BUF_SIZE,
                                       0, NULL, 0);
    if (rc != ESP_OK) return rc;
    rc = uart_param_config(UART_PORT, &ucfg);
    if (rc != ESP_OK) {
        uart_driver_delete(UART_PORT);
        return rc;
    }
    rc = uart_set_pin(UART_PORT, PIN_UART_TX, PIN_UART_RX,
                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (rc != ESP_OK) {
        uart_driver_delete(UART_PORT);
        return rc;
    }

    if (xTaskCreate(uart_rx_task, "ctrl_rx", 4096, NULL, 5, NULL) != pdPASS) {
        uart_driver_delete(UART_PORT);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UART%d RX=GPIO%d TX=GPIO%d %d baud",
             UART_PORT, PIN_UART_RX, PIN_UART_TX, UART_BAUD);
    return ESP_OK;
}
