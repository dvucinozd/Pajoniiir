#include "control_link.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include <inttypes.h>
#include <stdatomic.h>

static const char *TAG = "ctrl_link";

// ─── Pin assignment ───────────────────────────────────────────────────────────
// JP1 header pins — verify on hardware before wiring.
// S3 GPIO40 TX → P4 GPIO28 RX
// S3 GPIO41 RX ← P4 GPIO29 TX
#define PIN_UART_RX  GPIO_NUM_28
#define PIN_UART_TX  GPIO_NUM_29

#define UART_PORT    UART_NUM_1
#define UART_BAUD    115200
#define RX_BUF_SIZE  256
#define TX_BUF_SIZE  256

static QueueHandle_t    s_event_queue;
static atomic_uint_fast8_t s_seq = 0;
static uint32_t s_uart_write_fail_count;
static uint32_t s_event_drop_count;
static TickType_t s_last_warn;

// ─── TX helpers ───────────────────────────────────────────────────────────────

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

void control_link_send_led(led_id_t led, uint8_t state)
{
    send_frame(CTRL_TYPE_LED, (uint8_t)led, (int16_t)state);
}

// ─── RX parser ────────────────────────────────────────────────────────────────

typedef struct {
    uint8_t buf[CTRL_FRAME_LEN];
    int     pos;
} rx_state_t;

static void dispatch_frame(const uint8_t *f)
{
    ctrl_event_t ev = {
        .id    = f[2],
        .value = (int16_t)((uint16_t)f[3] | ((uint16_t)f[4] << 8)),
        .seq   = f[5],
    };

    switch (f[1]) {
    case CTRL_TYPE_BUTTON:
        ev.type = CTRL_EV_BUTTON;
        break;
    case CTRL_TYPE_ENCODER:
        if (ev.id == 0) {
            ev.type = CTRL_EV_JOG;
        } else if (ev.id == 1) {
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
    default:
        ESP_LOGW(TAG, "unknown frame type 0x%02x", f[1]);
        return;
    }

    if (xQueueSend(s_event_queue, &ev, 0) != pdTRUE) {
        s_event_drop_count++;
        TickType_t now = xTaskGetTickCount();
        if (now - s_last_warn >= pdMS_TO_TICKS(1000)) {
            s_last_warn = now;
            ESP_LOGW(TAG, "control event queue full, drops=%" PRIu32 " write_fail=%" PRIu32,
                     s_event_drop_count, s_uart_write_fail_count);
        }
    }
}

static void parse_byte(rx_state_t *st, uint8_t b)
{
    if (st->pos == 0 && b != CTRL_FRAME_START) return;

    st->buf[st->pos++] = b;
    if (st->pos < CTRL_FRAME_LEN) return;

    st->pos = 0;

    uint8_t chk = st->buf[1] ^ st->buf[2] ^ st->buf[3] ^ st->buf[4] ^ st->buf[5];
    if (chk != st->buf[6]) {
        ESP_LOGW(TAG, "bad checksum (got 0x%02x expected 0x%02x)", st->buf[6], chk);
        return;
    }

    dispatch_frame(st->buf);
}

static void uart_rx_task(void *arg)
{
    rx_state_t st  = { 0 };
    uint8_t    buf[64];

    while (1) {
        int n = uart_read_bytes(UART_PORT, buf, sizeof(buf), pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) {
            parse_byte(&st, buf[i]);
        }
    }
}

// ─── Init ─────────────────────────────────────────────────────────────────────

esp_err_t control_link_init(QueueHandle_t ctrl_event_queue)
{
    s_event_queue = ctrl_event_queue;

    uart_config_t ucfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(UART_PORT, RX_BUF_SIZE, TX_BUF_SIZE,
                                            0, NULL, 0), TAG, "driver install");
    ESP_RETURN_ON_ERROR(uart_param_config(UART_PORT, &ucfg), TAG, "param config");
    ESP_RETURN_ON_ERROR(uart_set_pin(UART_PORT, PIN_UART_TX, PIN_UART_RX,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "set pin");

    if (xTaskCreate(uart_rx_task, "ctrl_rx", 2048, NULL, 5, NULL) != pdPASS) {
        uart_driver_delete(UART_PORT);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UART%d RX=GPIO%d TX=GPIO%d %d baud",
             UART_PORT, PIN_UART_RX, PIN_UART_TX, UART_BAUD);
    return ESP_OK;
}
