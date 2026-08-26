#include "control_link.h"
#include "control_link_tx_serializer.h"
#include "controller_led_reconciler.h"
#include "flx4_midi_host.h"
#include "flx4_led_midi.h"
#include "controller_output_policy.h"
#include "controller_profile_runtime.h"
#include "s3_debug_ap.h"
#include "status_led.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ctrl_link";

// ─── Pin assignment ───────────────────────────────────────────────────────────
#define PIN_UART_TX  ((gpio_num_t)CONTROL_LINK_UART_TX_GPIO)   // S3 -> P4
#define PIN_UART_RX  ((gpio_num_t)CONTROL_LINK_UART_RX_GPIO)   // P4 -> S3

#define UART_PORT    UART_NUM_1
/* 460800 (4x the original 115200) over the short JP1 board-to-board link: cuts a
 * full 16 KB 0xA6 profile transfer from ~1.5 s to ~0.4 s so it stops starving
 * LED/event frames for that window. MUST match the P4 side. A framing error from
 * marginal signal integrity is self-healing (the RX parser resyncs on the next
 * 0xA5/0xA6 start byte), so a bad wire degrades rather than wedges the link. */
#define UART_BAUD    460800
/* 1 KB rings (was 256 B ≈ 22 ms at 115200): gives headroom for the 0xA6 bulk
 * profile stream and brief RX-task stalls so event/LED frames are not dropped. */
#define RX_BUF_SIZE  1024
#define TX_BUF_SIZE  1024
#define CTRL_RX_TASK_STACK 4096

static StaticSemaphore_t s_tx_mutex_storage;
static SemaphoreHandle_t s_tx_mutex;
static control_link_tx_serializer_t s_tx_serializer;
static uint32_t s_uart_write_fail_count;
static TickType_t s_last_uart_write_warn;
static controller_led_reconciler_t s_led_reconciler;
static control_link_state_cb_t s_state_cb;
static bool s_rx_task_started;

// ─── Profile transfer receiver (0xA6 bulk layer) ──────────────────────────────
#define S3_PROFILE_BUF_CAP 16384

static ctrl_bulk_parser_t s_bulk_parser;
static cp_xfer_rx_t s_xfer;
static uint8_t *s_profile_buf;
static bool s_profile_stored;
static size_t s_profile_len;
static uint16_t s_profile_vid;
static uint16_t s_profile_pid;
static uint32_t s_profile_connection_epoch;
static control_link_profile_activate_cb_t s_profile_activate_cb;

// ─── Frame helpers ────────────────────────────────────────────────────────────

typedef struct {
    uint8_t type;
    uint8_t id;
    int16_t value;
} fixed_frame_build_t;

static size_t build_fixed_frame(uint8_t *frame, size_t capacity,
                                uint8_t seq, const void *ctx)
{
    const fixed_frame_build_t *fixed = (const fixed_frame_build_t *)ctx;
    if (!frame || capacity < CTRL_FRAME_LEN || !fixed) return 0u;
    uint16_t v = (uint16_t)fixed->value;

    frame[0] = CTRL_FRAME_START;
    frame[1] = fixed->type;
    frame[2] = fixed->id;
    frame[3] = (uint8_t)(v & 0xFF);
    frame[4] = (uint8_t)((v >> 8) & 0xFF);
    frame[5] = seq;
    frame[6] = frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5];
    return CTRL_FRAME_LEN;
}

static bool tx_lock(void *ctx)
{
    (void)ctx;
    return s_tx_mutex && xSemaphoreTake(s_tx_mutex, portMAX_DELAY) == pdTRUE;
}

static void tx_unlock(void *ctx)
{
    (void)ctx;
    if (s_tx_mutex) xSemaphoreGive(s_tx_mutex);
}

static int tx_write(void *ctx, const uint8_t *data, size_t bytes)
{
    (void)ctx;
    return uart_write_bytes(UART_PORT, data, bytes);
}

static esp_err_t tx_send(uint8_t *frame, size_t capacity,
                         control_link_tx_build_fn build, const void *build_ctx,
                         const char *what)
{
    control_link_tx_result_t result;
    bool ok = control_link_tx_serializer_send(&s_tx_serializer,
                                               frame, capacity,
                                               build, build_ctx, &result);
    if (ok) return ESP_OK;

    s_uart_write_fail_count++;
    TickType_t now = xTaskGetTickCount();
    if (now - s_last_uart_write_warn >= pdMS_TO_TICKS(1000)) {
        s_last_uart_write_warn = now;
        ESP_LOGW(TAG, "%s UART write failed (%d/%u), failures=%" PRIu32,
                 what, result.written_bytes,
                 (unsigned)result.expected_bytes, s_uart_write_fail_count);
    }
    return ESP_FAIL;
}

static esp_err_t send_fixed_frame(uint8_t type, uint8_t id, int16_t value,
                                  const char *what)
{
    uint8_t frame[CTRL_FRAME_LEN];
    fixed_frame_build_t build = { .type = type, .id = id, .value = value };
    return tx_send(frame, sizeof(frame), build_fixed_frame, &build, what);
}

// ─── RX parser ───────────────────────────────────────────────────────────────

typedef struct {
    uint8_t buf[CTRL_FRAME_LEN];
    int     pos;
} rx_state_t;

static bool build_controller_led_packet(uint8_t id, uint8_t deck,
                                        uint8_t state, uint8_t packet[4],
                                        bool *intentional_drop)
{
    bool dynamic_active = controller_profile_runtime_active();
    bool dynamic_mapped = dynamic_active &&
        controller_profile_runtime_map_led(id, deck, state, packet);
    bool confirmed_flx4 = flx4_midi_host_builtin_flx4_active();
    bool builtin_mapped = !dynamic_active && confirmed_flx4 &&
        flx4_led_midi_build_packet(id, state, deck, packet);
    controller_output_route_t route = controller_output_select_route(
        dynamic_active, dynamic_mapped, confirmed_flx4, builtin_mapped);
    if (intentional_drop) {
        *intentional_drop = route == CONTROLLER_OUTPUT_DROP &&
            (dynamic_active || confirmed_flx4);
    }
    return route != CONTROLLER_OUTPUT_DROP;
}

static void flush_pending_controller_leds(uint32_t budget)
{
    controller_led_desired_t item;
    while (budget-- > 0u &&
           controller_led_reconciler_next(&s_led_reconciler, &item)) {
        uint8_t packet[4];
        bool intentional_drop = false;
        bool built = build_controller_led_packet(item.id, item.deck, item.state,
                                                 packet, &intentional_drop);
        bool complete = intentional_drop;
        if (built) {
            complete = flx4_midi_host_send_packet(packet) == ESP_OK;
        }
        controller_led_reconciler_complete(&s_led_reconciler, &item, complete);
    }
}

static void handle_p4_frame(const uint8_t *f)
{
    uint8_t type = f[1];
    uint8_t id   = f[2];
    uint8_t state = f[3];
    uint8_t deck  = f[4];
    int16_t value = (int16_t)((uint16_t)f[3] | ((uint16_t)f[4] << 8));

    if (type == CTRL_TYPE_LED) {
        if (deck == CTRL_DECK_1 || deck == CTRL_DECK_2) {
            if (id == LED_VU_METER) {
                uint8_t packet[4];
                bool intentional_drop = false;
                if (build_controller_led_packet(id, deck, state, packet,
                                                &intentional_drop)) {
                    (void)flx4_midi_host_send_packet(packet);
                }
                return;
            }
            if (controller_led_reconciler_observe(&s_led_reconciler, id, deck,
                                                  state)) {
                flush_pending_controller_leds(1u);
            }
        }

    } else if (type == CTRL_TYPE_STATE) {
        if (id == CTRL_ID_S3_DEBUG_AP) {
            (void)s3_debug_ap_request(state != 0);
        } else if (s_state_cb) {
            s_state_cb(id, value);
        }
    }
}

typedef struct {
    uint8_t type;
    uint8_t reason;
} profile_reply_build_t;

static size_t build_profile_ack(uint8_t *frame, size_t capacity,
                                uint8_t sequence, const void *ctx)
{
    const profile_reply_build_t *reply = (const profile_reply_build_t *)ctx;
    return reply ? ctrl_bulk_build_profile_ack(frame, capacity, sequence,
                                                reply->type) : 0u;
}

static size_t build_profile_nack(uint8_t *frame, size_t capacity,
                                 uint8_t sequence, const void *ctx)
{
    const profile_reply_build_t *reply = (const profile_reply_build_t *)ctx;
    return reply ? ctrl_bulk_build_profile_nack(frame, capacity, sequence,
                                                 reply->type, reply->reason) : 0u;
}

static void send_profile_reply_ack(uint8_t acked_type)
{
    uint8_t frame[CTRL_BULK_MAX_FRAME];
    profile_reply_build_t build = { .type = acked_type };
    (void)tx_send(frame, sizeof(frame), build_profile_ack, &build,
                  "profile ACK");
}

static void send_profile_reply_nack(uint8_t nacked_type, uint8_t reason)
{
    uint8_t frame[CTRL_BULK_MAX_FRAME];
    profile_reply_build_t build = { .type = nacked_type, .reason = reason };
    (void)tx_send(frame, sizeof(frame), build_profile_nack, &build,
                  "profile NACK");
    ESP_LOGW(TAG, "profile NACK type=0x%02X reason=%u", nacked_type, reason);
}

static void handle_profile_frame(const uint8_t *frame, size_t frame_len)
{
    switch (frame[1]) {
    case CTRL_BULK_TYPE_PROFILE_BEGIN: {
        uint32_t total, crc;
        uint16_t vid, pid;
        if (!ctrl_bulk_decode_profile_begin(frame, frame_len, &total, &crc,
                                            &vid, &pid)) {
            send_profile_reply_nack(CTRL_BULK_TYPE_PROFILE_BEGIN,
                                    CTRL_PROFILE_NACK_STATE);
            return;
        }
        flx4_midi_connection_context_t connection;
        if (!flx4_midi_host_get_connection_context(&connection) ||
            connection.vid != vid || connection.pid != pid) {
            send_profile_reply_nack(CTRL_BULK_TYPE_PROFILE_BEGIN,
                                    CTRL_PROFILE_NACK_STATE);
            return;
        }
        if (!s_profile_buf) {
            s_profile_buf = malloc(S3_PROFILE_BUF_CAP);
            if (!s_profile_buf) {
                send_profile_reply_nack(CTRL_BULK_TYPE_PROFILE_BEGIN,
                                        CTRL_PROFILE_NACK_SIZE);
                return;
            }
        }
        s_profile_stored = false;
        cp_xfer_rx_init(&s_xfer, s_profile_buf, S3_PROFILE_BUF_CAP);
        uint8_t reason = cp_xfer_rx_begin(&s_xfer, total, crc, vid, pid);
        if (reason == CTRL_PROFILE_NACK_NONE) {
            s_profile_connection_epoch = connection.connection_epoch;
            send_profile_reply_ack(CTRL_BULK_TYPE_PROFILE_BEGIN);
        } else {
            send_profile_reply_nack(CTRL_BULK_TYPE_PROFILE_BEGIN, reason);
        }
        break;
    }
    case CTRL_BULK_TYPE_PROFILE_CHUNK: {
        uint32_t offset;
        const uint8_t *data;
        size_t len;
        if (!ctrl_bulk_decode_profile_chunk(frame, frame_len, &offset, &data,
                                            &len)) {
            send_profile_reply_nack(CTRL_BULK_TYPE_PROFILE_CHUNK,
                                    CTRL_PROFILE_NACK_OFFSET);
            return;
        }
        uint8_t reason = cp_xfer_rx_chunk(&s_xfer, offset, data, len);
        /* Silent on success (ACK only after END); NACK aborts the P4 sender. */
        if (reason != CTRL_PROFILE_NACK_NONE) {
            send_profile_reply_nack(CTRL_BULK_TYPE_PROFILE_CHUNK, reason);
        }
        break;
    }
    case CTRL_BULK_TYPE_PROFILE_END: {
        uint8_t reason = cp_xfer_rx_end(&s_xfer);
        if (reason == CTRL_PROFILE_NACK_NONE) {
            s_profile_stored = true;
            s_profile_len = s_xfer.total;
            s_profile_vid = s_xfer.vid;
            s_profile_pid = s_xfer.pid;
            send_profile_reply_ack(CTRL_BULK_TYPE_PROFILE_END);
            ESP_LOGI(TAG, "profile received: %u B VID=0x%04X PID=0x%04X",
                     (unsigned)s_profile_len, s_profile_vid, s_profile_pid);
        } else {
            send_profile_reply_nack(CTRL_BULK_TYPE_PROFILE_END, reason);
        }
        break;
    }
    case CTRL_BULK_TYPE_PROFILE_ACTIVATE: {
        if (!s_profile_stored) {
            send_profile_reply_nack(CTRL_BULK_TYPE_PROFILE_ACTIVATE,
                                    CTRL_PROFILE_NACK_STATE);
            return;
        }
        bool ok = true;
        if (s_profile_activate_cb) {
            ok = s_profile_activate_cb(s_profile_buf, s_profile_len,
                                       s_profile_vid, s_profile_pid,
                                       s_profile_connection_epoch);
        }
        if (ok) {
            controller_led_reconciler_mark_all_dirty(&s_led_reconciler);
            send_profile_reply_ack(CTRL_BULK_TYPE_PROFILE_ACTIVATE);
            ESP_LOGI(TAG, "profile activated VID=0x%04X PID=0x%04X",
                     s_profile_vid, s_profile_pid);
        } else {
            send_profile_reply_nack(CTRL_BULK_TYPE_PROFILE_ACTIVATE,
                                    CTRL_PROFILE_NACK_PARSE);
        }
        break;
    }
    case CTRL_BULK_TYPE_PROFILE_CLEAR:
        s_profile_stored = false;
        s_profile_len = 0;
        s_profile_connection_epoch = 0u;
        cp_xfer_rx_init(&s_xfer, s_profile_buf, S3_PROFILE_BUF_CAP);
        if (s_profile_activate_cb) {
            (void)s_profile_activate_cb(NULL, 0, 0, 0, 0u);
        }
        controller_led_reconciler_mark_all_dirty(&s_led_reconciler);
        send_profile_reply_ack(CTRL_BULK_TYPE_PROFILE_CLEAR);
        break;
    default:
        ESP_LOGW(TAG, "unexpected bulk frame type 0x%02X", frame[1]);
        break;
    }
}

static void parse_rx_byte(rx_state_t *st, uint8_t b)
{
    /* 0xA6 bulk frames (profile transfer) run through the bulk parser, which
       owns the stream while a bulk frame is in progress. 0xA5 event frames use
       the fixed 7-byte path below. */
    if (st->pos == 0) {
        if (s_bulk_parser.pos > 0 || b == CTRL_BULK_FRAME_START) {
            int r = ctrl_bulk_parser_feed(&s_bulk_parser, b);
            if (r > 0) {
                status_led_notify_p4_frame();
                handle_profile_frame(s_bulk_parser.buf, (size_t)r);
            } else if (r < 0) {
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
        ESP_LOGW(TAG, "bad checksum");
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

    /* A valid frame proves the P4 UART link is alive; the status LED uses this
       to distinguish "link down" (amber) from FLX4 connection state. */
    status_led_notify_p4_frame();
    handle_p4_frame(st->buf);
}

static void uart_rx_task(void *arg)
{
    rx_state_t  st  = { 0 };
    uint8_t     buf[64];

    __atomic_store_n(&s_rx_task_started, true, __ATOMIC_RELEASE);
    while (1) {
        int n = uart_read_bytes(UART_PORT, buf, sizeof(buf), pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) {
            parse_rx_byte(&st, buf[i]);
        }
        /* Queue-full leaves the desired non-VU LED dirty. Retry from this
         * bounded producer budget while the USB client task remains the sole
         * owner of transfer submission and completion. */
        flush_pending_controller_leds(4u);
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

esp_err_t control_link_init(void)
{
    __atomic_store_n(&s_rx_task_started, false, __ATOMIC_RELEASE);
    if (!s_tx_mutex) {
        s_tx_mutex = xSemaphoreCreateMutexStatic(&s_tx_mutex_storage);
    }
    if (!s_tx_mutex) return ESP_ERR_NO_MEM;
    control_link_tx_serializer_init(&s_tx_serializer, tx_lock, tx_unlock,
                                    tx_write, NULL);
    controller_led_reconciler_reset(&s_led_reconciler);

    uart_config_t ucfg = {
        .baud_rate           = UART_BAUD,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .source_clk          = UART_SCLK_DEFAULT,
    };
    /* No UART event queue: the RX task polls with uart_read_bytes and the event
     * queue was never consumed (matches the P4 side). */
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

    if (xTaskCreate(uart_rx_task, "ctrl_rx", CTRL_RX_TASK_STACK, NULL, 5, NULL) != pdPASS) {
        uart_driver_delete(UART_PORT);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UART%d TX=%d RX=%d %d baud", UART_PORT, PIN_UART_TX, PIN_UART_RX, UART_BAUD);
    return ESP_OK;
}

void control_link_send_heartbeat(void)
{
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    (void)send_fixed_frame(CTRL_TYPE_HEARTBEAT, 0,
                           (int16_t)(uptime_s & 0xFFFF), "heartbeat");
}

esp_err_t control_link_send_semantic(uint8_t type, uint8_t id, int16_t value)
{
    return send_fixed_frame(type, id, value, "semantic event");
}

void control_link_set_state_cb(control_link_state_cb_t cb)
{
    s_state_cb = cb;
}

bool control_link_rx_task_started(void)
{
    return __atomic_load_n(&s_rx_task_started, __ATOMIC_ACQUIRE);
}

void control_link_set_profile_activate_cb(control_link_profile_activate_cb_t cb)
{
    s_profile_activate_cb = cb;
}

const uint8_t *control_link_get_stored_profile(size_t *len, uint16_t *vid,
                                               uint16_t *pid)
{
    if (!s_profile_stored) {
        return NULL;
    }
    if (len) *len = s_profile_len;
    if (vid) *vid = s_profile_vid;
    if (pid) *pid = s_profile_pid;
    return s_profile_buf;
}

static size_t build_descriptor_report(uint8_t *frame, size_t capacity,
                                      uint8_t sequence, const void *ctx)
{
    return ctrl_bulk_build_descriptor_frame(
        frame, capacity, sequence, (const ctrl_descriptor_report_t *)ctx);
}

static size_t build_firmware_report(uint8_t *frame, size_t capacity,
                                    uint8_t sequence, const void *ctx)
{
    return ctrl_bulk_build_firmware_report(
        frame, capacity, sequence, (const ctrl_firmware_report_t *)ctx);
}

esp_err_t control_link_send_descriptor_report(const ctrl_descriptor_report_t *rep)
{
    if (!rep) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t frame[CTRL_BULK_MAX_FRAME];
    return tx_send(frame, sizeof(frame), build_descriptor_report, rep,
                   "descriptor report");
}

esp_err_t control_link_send_firmware_report(const ctrl_firmware_report_t *rep)
{
    if (!rep) return ESP_ERR_INVALID_ARG;
    uint8_t frame[CTRL_BULK_MAX_FRAME];
    return tx_send(frame, sizeof(frame), build_firmware_report, rep,
                   "firmware report");
}
