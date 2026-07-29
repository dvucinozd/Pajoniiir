#include "monitor_pcm_link.h"

#include "sdkconfig.h"

#if CONFIG_MONITOR_PCM_LINK_ENABLED

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "monitor_pcm_i2s";

/* The I2S link is a framed byte pipe, not an audio clock: 64 kHz 16-bit
   stereo slots carry 256000 B/s, ~1.3x the ~197000 B/s a framed 48 kHz
   stereo monitor stream needs. 96 kHz slots (3.072 MHz BCLK) showed ~90%
   CRC corruption over the bench jumper wires; 2.048 MHz is the fallback.
   Must match P4_AUDIO_LINK_I2S_PIPE_RATE_HZ on the S3 side. */
#define MONITOR_PCM_LINK_I2S_PIPE_RATE_HZ 64000u

#define MONITOR_PCM_LINK_TX_TASK_STACK 4096
#define MONITOR_PCM_LINK_TX_TASK_PRIO 10
#define MONITOR_PCM_LINK_MAX_BLOCK_BYTES \
    (sizeof(monitor_pcm_link_block_header_t) + \
     (size_t)MONITOR_PCM_LINK_MAX_FRAMES_PER_BLOCK * 2u * sizeof(int16_t))

static i2s_chan_handle_t s_tx_chan;
static TaskHandle_t s_transport_task;
#if CONFIG_MONITOR_PCM_LINK_BENCH_TONE
static TaskHandle_t s_bench_task;
#endif
static uint32_t s_transport_starting;

static void transport_task(void *arg)
{
    i2s_chan_handle_t tx_chan = (i2s_chan_handle_t)arg;
    static uint8_t block[MONITOR_PCM_LINK_MAX_BLOCK_BYTES];
    static const uint8_t zero_filler[512] = { 0 };
    uint32_t sent_blocks = 0u;
    TickType_t last_log = xTaskGetTickCount();

    while (1) {
        size_t block_bytes = monitor_pcm_link_read_block_for_transport(block, sizeof(block));
        size_t written = 0u;
        if (block_bytes > 0u) {
            esp_err_t rc = i2s_channel_write(tx_chan, block, block_bytes, &written, portMAX_DELAY);
            if (rc == ESP_OK && written == block_bytes) {
                sent_blocks++;
            } else {
                ESP_LOGW(TAG, "i2s write failed: %s written=%u",
                         esp_err_to_name(rc), (unsigned)written);
            }
        } else {
            /* The TX DMA is a circular buffer that transmits continuously; if
               the writer ever sleeps unfed, the DMA laps it and auto-cleared
               zeros go out mid-block (measured as ~74% block loss on the
               bench). Blocking filler writes keep the writer exactly at line
               rate, so the wire carries only whole blocks and explicit
               filler the deframer skips. */
            (void)i2s_channel_write(tx_chan, zero_filler, sizeof(zero_filler), &written, portMAX_DELAY);
        }

        const TickType_t now = xTaskGetTickCount();
        if ((now - last_log) >= pdMS_TO_TICKS(1000)) {
            last_log = now;
            monitor_pcm_link_stats_t stats;
            monitor_pcm_link_get_stats(&stats);
            ESP_LOGI(TAG, "MONITOR_PCM_LINK tx submitted=%u dropped=%u sent=%u",
                     (unsigned)stats.submitted_blocks,
                     (unsigned)stats.dropped_blocks,
                     (unsigned)sent_blocks);
        }
    }
}

#if CONFIG_MONITOR_PCM_LINK_BENCH_TONE

#define MONITOR_PCM_LINK_BENCH_BLOCK_FRAMES 192u
#define MONITOR_PCM_LINK_BENCH_SAMPLE_RATE 48000u

/* 48-sample 1 kHz sine at -18 dBFS: one full cycle per 48 samples at 48 kHz,
   same deterministic table the FLX4 USB tone smoke used. */
static const int16_t s_sine_48_minus18dbfs[48] = {
    0, 539, 1069, 1581, 2066, 2515, 2921, 3276,
    3575, 3813, 3985, 4090, 4125, 4090, 3985, 3813,
    3575, 3276, 2921, 2515, 2066, 1581, 1069, 539,
    0, -539, -1069, -1581, -2066, -2515, -2921, -3276,
    -3575, -3813, -3985, -4090, -4125, -4090, -3985, -3813,
    -3575, -3276, -2921, -2515, -2066, -1581, -1069, -539,
};

static void bench_tone_task(void *arg)
{
    (void)arg;
    /* The transport now starts at the top of app_main; audio_engine_init()
       re-runs monitor_pcm_link_init() a few seconds later and would clobber
       the enabled flag and stats, so let boot settle before generating. */
    vTaskDelay(pdMS_TO_TICKS(20000));
    (void)monitor_pcm_link_set_format(MONITOR_PCM_LINK_BENCH_SAMPLE_RATE, 2u, 16u);
    monitor_pcm_link_set_enabled(true);
    ESP_LOGI(TAG, "bench tone generator running: 1 kHz stereo, %u Hz, %u-frame blocks",
             (unsigned)MONITOR_PCM_LINK_BENCH_SAMPLE_RATE,
             (unsigned)MONITOR_PCM_LINK_BENCH_BLOCK_FRAMES);

    static int16_t block[MONITOR_PCM_LINK_BENCH_BLOCK_FRAMES * 2u];
    uint32_t phase = 0u;
    const int64_t start_us = esp_timer_get_time();
    uint64_t produced_frames = 0u;

    while (1) {
        const int64_t elapsed_us = esp_timer_get_time() - start_us;
        const uint64_t owed_frames =
            (uint64_t)elapsed_us * MONITOR_PCM_LINK_BENCH_SAMPLE_RATE / 1000000u;
        while (produced_frames + MONITOR_PCM_LINK_BENCH_BLOCK_FRAMES <= owed_frames) {
            for (uint32_t i = 0; i < MONITOR_PCM_LINK_BENCH_BLOCK_FRAMES; ++i) {
                const int16_t sample = s_sine_48_minus18dbfs[phase];
                phase = (phase + 1u) % 48u;
                block[i * 2u] = sample;
                block[i * 2u + 1u] = sample;
            }
            /* Real-time pacing: advance even when the queue is full so a
               stalled transport shows up as dropped_blocks, not backlog. */
            (void)monitor_pcm_link_write_nonblocking(block,
                                                     MONITOR_PCM_LINK_BENCH_BLOCK_FRAMES);
            produced_frames += MONITOR_PCM_LINK_BENCH_BLOCK_FRAMES;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
#endif /* CONFIG_MONITOR_PCM_LINK_BENCH_TONE */

static void monitor_pcm_link_transport_cleanup(i2s_chan_handle_t tx_chan)
{
#if CONFIG_MONITOR_PCM_LINK_BENCH_TONE
    if (s_bench_task) {
        vTaskDelete(s_bench_task);
        s_bench_task = NULL;
    }
#endif
    if (s_transport_task) {
        vTaskDelete(s_transport_task);
        s_transport_task = NULL;
    }
    if (tx_chan) {
        (void)i2s_channel_disable(tx_chan);
        (void)i2s_del_channel(tx_chan);
    }
    s_tx_chan = NULL;
}

esp_err_t monitor_pcm_link_start_transport(void)
{
    uint32_t expected = 0u;
    if (!__atomic_compare_exchange_n(&s_transport_starting, &expected, 1u,
                                     false, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_tx_chan) {
        __atomic_store_n(&s_transport_starting, 0u, __ATOMIC_RELEASE);
        return ESP_ERR_INVALID_STATE;
    }

    /* P4 default log level is WARN; surface only this component's INFO
       diagnostics (requires CONFIG_LOG_MAXIMUM_LEVEL >= INFO). */
    esp_log_level_set(TAG, ESP_LOG_INFO);

    /* Explicit unit (never I2S_NUM_AUTO): this transport starts before
       bsp_audio_init, so AUTO would steal whichever unit the DACs need. Unit 2
       froze rev v1.3 (eco2) silicon. Bench uses unit 1 (ES8311 keeps unit 0);
       product uses unit 0 (ES8311 disabled, PCM5102A MAIN on unit 1). */
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_MONITOR_PCM_LINK_I2S_UNIT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; /* DMA sends zero filler when the queue idles */

    i2s_chan_handle_t tx_chan = NULL;
    esp_err_t rc = i2s_new_channel(&chan_cfg, &tx_chan, NULL);
    if (rc != ESP_OK) {
        __atomic_store_n(&s_transport_starting, 0u, __ATOMIC_RELEASE);
        return rc;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MONITOR_PCM_LINK_I2S_PIPE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_MONITOR_PCM_LINK_BCLK_GPIO,
            .ws = CONFIG_MONITOR_PCM_LINK_WS_GPIO,
            .dout = CONFIG_MONITOR_PCM_LINK_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
        },
    };
    /* The P4 I2S default clock source is the 40 MHz XTAL and the driver
       requires sclk > 1.99 x MCLK, so the default 256x multiple aborts with
       "sample rate is too large". 128x keeps MCLK well under the XTAL and
       still divides evenly to the BCLK. */
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_128;
    rc = i2s_channel_init_std_mode(tx_chan, &std_cfg);
    if (rc != ESP_OK) {
        monitor_pcm_link_transport_cleanup(tx_chan);
        __atomic_store_n(&s_transport_starting, 0u, __ATOMIC_RELEASE);
        return rc;
    }
    rc = i2s_channel_enable(tx_chan);
    if (rc != ESP_OK) {
        monitor_pcm_link_transport_cleanup(tx_chan);
        __atomic_store_n(&s_transport_starting, 0u, __ATOMIC_RELEASE);
        return rc;
    }

    /* Strongest pad drive for sharper edges into the inter-board wiring. */
    (void)gpio_set_drive_capability(CONFIG_MONITOR_PCM_LINK_BCLK_GPIO, GPIO_DRIVE_CAP_3);
    (void)gpio_set_drive_capability(CONFIG_MONITOR_PCM_LINK_WS_GPIO, GPIO_DRIVE_CAP_3);
    (void)gpio_set_drive_capability(CONFIG_MONITOR_PCM_LINK_DOUT_GPIO, GPIO_DRIVE_CAP_3);

    if (xTaskCreate(transport_task, "mon_pcm_tx", MONITOR_PCM_LINK_TX_TASK_STACK,
                    tx_chan, MONITOR_PCM_LINK_TX_TASK_PRIO,
                    &s_transport_task) != pdPASS) {
        monitor_pcm_link_transport_cleanup(tx_chan);
        __atomic_store_n(&s_transport_starting, 0u, __ATOMIC_RELEASE);
        return ESP_ERR_NO_MEM;
    }
#if CONFIG_MONITOR_PCM_LINK_BENCH_TONE
    if (xTaskCreate(bench_tone_task, "mon_pcm_tone", MONITOR_PCM_LINK_TX_TASK_STACK,
                    NULL, MONITOR_PCM_LINK_TX_TASK_PRIO - 1,
                    &s_bench_task) != pdPASS) {
        monitor_pcm_link_transport_cleanup(tx_chan);
        __atomic_store_n(&s_transport_starting, 0u, __ATOMIC_RELEASE);
        return ESP_ERR_NO_MEM;
    }
#endif

    s_tx_chan = tx_chan;
    __atomic_store_n(&s_transport_starting, 0u, __ATOMIC_RELEASE);
    ESP_LOGI(TAG, "monitor PCM I2S transport started: BCLK=%d WS=%d DOUT=%d pipe=%u Hz",
             CONFIG_MONITOR_PCM_LINK_BCLK_GPIO,
             CONFIG_MONITOR_PCM_LINK_WS_GPIO,
             CONFIG_MONITOR_PCM_LINK_DOUT_GPIO,
             (unsigned)MONITOR_PCM_LINK_I2S_PIPE_RATE_HZ);
    return ESP_OK;
}

#endif /* CONFIG_MONITOR_PCM_LINK_ENABLED */
