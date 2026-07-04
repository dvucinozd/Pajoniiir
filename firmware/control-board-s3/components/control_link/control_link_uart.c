#include "control_link.h"
#include "panel_io.h"
#include "flx4_midi_host.h"
#include "flx4_led_midi.h"
#include "status_led.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <inttypes.h>
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "ctrl_link";

// ─── Pin assignment ───────────────────────────────────────────────────────────
#define PIN_UART_TX  GPIO_NUM_40   // S3 → P4
#define PIN_UART_RX  GPIO_NUM_41   // P4 → S3

#define UART_PORT    UART_NUM_1
#define UART_BAUD    115200
#define RX_BUF_SIZE  256
#define TX_BUF_SIZE  256
#define RX_QUEUE_LEN 10
#define CTRL_RX_TASK_STACK 4096

static QueueHandle_t   s_uart_event_queue;
static atomic_uint_fast8_t s_seq = 0;
static uint32_t s_uart_write_fail_count;
static TickType_t s_last_uart_write_warn;
static bool s_panel_led_fallback_enabled;

// ─── Frame helpers ────────────────────────────────────────────────────────────

static void build_frame(uint8_t frame[CTRL_FRAME_LEN],
                        uint8_t type, uint8_t id, int16_t value)
{
    uint8_t seq = atomic_fetch_add_explicit(&s_seq, 1, memory_order_relaxed);
    uint16_t v  = (uint16_t)value;

    frame[0] = CTRL_FRAME_START;
    frame[1] = type;
    frame[2] = id;
    frame[3] = (uint8_t)(v & 0xFF);
    frame[4] = (uint8_t)((v >> 8) & 0xFF);
    frame[5] = seq;
    frame[6] = frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5];
}

static esp_err_t send_frame_checked(const uint8_t frame[CTRL_FRAME_LEN], const char *what)
{
    int written = uart_write_bytes(UART_PORT, frame, CTRL_FRAME_LEN);
    if (written == CTRL_FRAME_LEN) {
        return ESP_OK;
    }

    s_uart_write_fail_count++;
    TickType_t now = xTaskGetTickCount();
    if (now - s_last_uart_write_warn >= pdMS_TO_TICKS(1000)) {
        s_last_uart_write_warn = now;
        ESP_LOGW(TAG, "%s UART short write (%d/%d), failures=%" PRIu32,
                 what, written, CTRL_FRAME_LEN, s_uart_write_fail_count);
    }
    return ESP_FAIL;
}

// ─── RX parser ───────────────────────────────────────────────────────────────

typedef struct {
    uint8_t buf[CTRL_FRAME_LEN];
    int     pos;
} rx_state_t;

static void handle_p4_frame(const uint8_t *f)
{
    uint8_t type = f[1];
    uint8_t id   = f[2];
    uint8_t state = f[3];
    uint8_t deck  = f[4];

    if (type == CTRL_TYPE_LED) {
        // 1. Forward to DDJ-FLX4 via USB MIDI
        if (deck == CTRL_DECK_1 || deck == CTRL_DECK_2) {
            uint8_t packet[4];
            if (flx4_led_midi_build_packet(id, state, deck, packet)) {
                #if !defined(FLX4_MIDI_HOST_PC_TEST)
                flx4_midi_host_send_packet(packet);
                #endif
                if (id == LED_VU_METER) {
                    return;
                }
            }
        }

        // 2. Fallback to local panel LEDs (compatibility / tests)
        if (s_panel_led_fallback_enabled && id < LED_COUNT) {
            if (state == 2) {
                panel_led_blink((led_id_t)id, 500);
            } else {
                panel_led_set((led_id_t)id, state != 0);
            }
        }
    }
    // CTRL_TYPE_STATE reserved for future use
}

static void parse_rx_byte(rx_state_t *st, uint8_t b)
{
    if (st->pos == 0) {
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

    while (1) {
        int n = uart_read_bytes(UART_PORT, buf, sizeof(buf), pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) {
            parse_rx_byte(&st, buf[i]);
        }
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

esp_err_t control_link_init(QueueHandle_t panel_event_queue)
{
    s_panel_led_fallback_enabled = panel_event_queue != NULL;

    uart_config_t ucfg = {
        .baud_rate           = UART_BAUD,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .source_clk          = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(UART_PORT, RX_BUF_SIZE, TX_BUF_SIZE,
                                            RX_QUEUE_LEN, &s_uart_event_queue, 0),
                        TAG, "driver install");
    ESP_RETURN_ON_ERROR(uart_param_config(UART_PORT, &ucfg), TAG, "param config");
    ESP_RETURN_ON_ERROR(uart_set_pin(UART_PORT, PIN_UART_TX, PIN_UART_RX,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "set pin");

    if (xTaskCreate(uart_rx_task, "ctrl_rx", CTRL_RX_TASK_STACK, NULL, 5, NULL) != pdPASS) {
        uart_driver_delete(UART_PORT);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UART%d TX=%d RX=%d %d baud", UART_PORT, PIN_UART_TX, PIN_UART_RX, UART_BAUD);
    return ESP_OK;
}

void control_link_send_heartbeat(void)
{
    uint8_t frame[CTRL_FRAME_LEN];
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    build_frame(frame, CTRL_TYPE_HEARTBEAT, 0, (int16_t)(uptime_s & 0xFFFF));
    (void)send_frame_checked(frame, "heartbeat");
}

esp_err_t control_link_send_semantic(uint8_t type, uint8_t id, int16_t value)
{
    uint8_t frame[CTRL_FRAME_LEN];
    build_frame(frame, type, id, value);
    return send_frame_checked(frame, "semantic event");
}

void control_link_send_event(const panel_event_t *ev)
{
    uint8_t frame[CTRL_FRAME_LEN];

    switch (ev->type) {
    case PANEL_EV_BUTTON:
        build_frame(frame, CTRL_TYPE_BUTTON, ev->id, ev->value);
        break;
    case PANEL_EV_JOG:
        build_frame(frame, CTRL_TYPE_ENCODER, 0, ev->value);
        break;
    case PANEL_EV_BROWSE:
        build_frame(frame, CTRL_TYPE_ENCODER, 1, ev->value);
        break;
    case PANEL_EV_PITCH:
        build_frame(frame, CTRL_TYPE_PITCH, 0, ev->value);
        break;
    default:
        return;
    }

    (void)send_frame_checked(frame, "panel event");
}
