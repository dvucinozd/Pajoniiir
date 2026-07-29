#pragma once

/* Minimal UART fake. The suite drives control_link_uart.c by feeding bytes into
 * a scripted RX buffer and reading back whatever the component transmits, so
 * frame parsing and queue discipline can be exercised without a driver. */

#include "esp_err.h"

#include <stdint.h>
#include <string.h>

typedef int uart_port_t;

#define UART_NUM_1              1
#define UART_PIN_NO_CHANGE      (-1)
#define UART_DATA_8_BITS        3
#define UART_PARITY_DISABLE     0
#define UART_STOP_BITS_1        1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT       0

typedef struct {
    int baud_rate;
    int data_bits;
    int parity;
    int stop_bits;
    int flow_ctrl;
    int rx_flow_ctrl_thresh;
    int source_clk;
} uart_config_t;

/* Scripted RX bytes the component will read, and a capture of every TX byte. */
#define TEST_UART_BUF 1024
typedef struct {
    uint8_t rx[TEST_UART_BUF];
    size_t rx_len;
    size_t rx_pos;
    uint8_t tx[TEST_UART_BUF];
    size_t tx_len;
    int installed;
    int write_fail;   /* non-zero: uart_write_bytes reports a short write */
} test_uart_state_t;

extern test_uart_state_t g_test_uart;

static inline void test_uart_reset(void)
{
    memset(&g_test_uart, 0, sizeof(g_test_uart));
}

/* Queue bytes for the component to receive. */
static inline void test_uart_feed(const uint8_t *bytes, size_t len)
{
    if (g_test_uart.rx_len + len > sizeof(g_test_uart.rx)) return;
    memcpy(g_test_uart.rx + g_test_uart.rx_len, bytes, len);
    g_test_uart.rx_len += len;
}

static inline esp_err_t uart_driver_install(uart_port_t port, int rx_buf, int tx_buf,
                                            int queue_size, void *queue, int flags)
{
    (void)port; (void)rx_buf; (void)tx_buf; (void)queue_size; (void)queue; (void)flags;
    g_test_uart.installed = 1;
    return ESP_OK;
}

static inline esp_err_t uart_driver_delete(uart_port_t port)
{
    (void)port;
    g_test_uart.installed = 0;
    return ESP_OK;
}

static inline esp_err_t uart_param_config(uart_port_t port, const uart_config_t *cfg)
{
    (void)port; (void)cfg;
    return ESP_OK;
}

static inline esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts)
{
    (void)port; (void)tx; (void)rx; (void)rts; (void)cts;
    return ESP_OK;
}

static inline int uart_read_bytes(uart_port_t port, void *buf, uint32_t len, uint32_t ticks)
{
    (void)port; (void)ticks;
    size_t available = g_test_uart.rx_len - g_test_uart.rx_pos;
    if (available == 0u) return 0;
    size_t take = available < len ? available : len;
    memcpy(buf, g_test_uart.rx + g_test_uart.rx_pos, take);
    g_test_uart.rx_pos += take;
    return (int)take;
}

static inline int uart_write_bytes(uart_port_t port, const void *buf, size_t len)
{
    (void)port;
    if (g_test_uart.write_fail) return (int)len - 1;
    if (g_test_uart.tx_len + len > sizeof(g_test_uart.tx)) return -1;
    memcpy(g_test_uart.tx + g_test_uart.tx_len, buf, len);
    g_test_uart.tx_len += len;
    return (int)len;
}
