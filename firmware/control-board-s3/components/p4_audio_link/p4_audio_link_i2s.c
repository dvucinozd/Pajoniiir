#include "p4_audio_link.h"

#include "sdkconfig.h"

#if CONFIG_P4_AUDIO_LINK_ENABLED

#include <string.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "p4_audio_link";

/* Must match the P4 monitor_pcm_link I2S pipe: 64 kHz 16-bit stereo slots
   carrying a framed byte stream (the S3 slave follows the P4 master clock,
   the nominal rate here only sizes internal buffers). */
#define P4_AUDIO_LINK_I2S_PIPE_RATE_HZ 64000u

#define P4_AUDIO_LINK_RX_CHUNK_BYTES 2048u
#define P4_AUDIO_LINK_RX_TASK_STACK 6144
#define P4_AUDIO_LINK_RX_TASK_PRIO 12

#if CONFIG_P4_AUDIO_LINK_BENCH_CONSUMER
/* Bench-only paced drain target: keeps the ring near half fill so overruns
   and underruns both read zero when the link itself is healthy. */
#define P4_AUDIO_LINK_BENCH_DRAIN_TARGET_FRAMES 2048u
#define P4_AUDIO_LINK_BENCH_DRAIN_CHUNK_FRAMES 240u
#endif

static i2s_chan_handle_t s_rx_chan;

static void rx_task(void *arg)
{
    (void)arg;
    static uint8_t chunk[P4_AUDIO_LINK_RX_CHUNK_BYTES];
    TickType_t last_log = xTaskGetTickCount();

    while (1) {
        size_t got = 0u;
        esp_err_t rc = i2s_channel_read(s_rx_chan, chunk, sizeof(chunk), &got,
                                        pdMS_TO_TICKS(100));
        if (rc == ESP_OK && got > 0u) {
            /* The ring is fed and drained from this task only, so the pure
               p4_audio_link state needs no locking in the bench slice. The
               Task 8 USB consumer must add cross-task protection first. */
            p4_audio_link_feed_bytes(chunk, got);
        }

#if CONFIG_P4_AUDIO_LINK_BENCH_CONSUMER
        static int16_t scratch[P4_AUDIO_LINK_BENCH_DRAIN_CHUNK_FRAMES * 2u];
        p4_audio_link_stats_t drain_stats;
        p4_audio_link_get_stats(&drain_stats);
        while (drain_stats.ring_frames > P4_AUDIO_LINK_BENCH_DRAIN_TARGET_FRAMES) {
            size_t want = drain_stats.ring_frames - P4_AUDIO_LINK_BENCH_DRAIN_TARGET_FRAMES;
            if (want > P4_AUDIO_LINK_BENCH_DRAIN_CHUNK_FRAMES) {
                want = P4_AUDIO_LINK_BENCH_DRAIN_CHUNK_FRAMES;
            }
            (void)p4_audio_link_read_frames(scratch, want);
            p4_audio_link_get_stats(&drain_stats);
        }
#endif

        const TickType_t now = xTaskGetTickCount();
        if ((now - last_log) >= pdMS_TO_TICKS(1000)) {
            last_log = now;
            p4_audio_link_stats_t stats;
            p4_audio_link_get_stats(&stats);
            ESP_LOGI(TAG, "P4_AUDIO_LINK rx blocks=%u gaps=%u crc=%u ring=%u underruns=%u overruns=%u",
                     (unsigned)stats.received_blocks,
                     (unsigned)stats.sequence_gaps,
                     (unsigned)stats.crc_errors,
                     (unsigned)stats.ring_frames,
                     (unsigned)stats.underruns,
                     (unsigned)stats.overruns);
        }
    }
}

esp_err_t p4_audio_link_start(void)
{
    if (s_rx_chan) {
        return ESP_ERR_INVALID_STATE;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_SLAVE);
    /* ~64 ms of RX DMA cushion so USB host bursts cannot overwrite unread
       link data (default 6x240 frames is only ~22 ms at the pipe rate). */
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 500;
    esp_err_t rc = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (rc != ESP_OK) {
        return rc;
    }

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(P4_AUDIO_LINK_I2S_PIPE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_P4_AUDIO_LINK_BCLK_GPIO,
            .ws = CONFIG_P4_AUDIO_LINK_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = CONFIG_P4_AUDIO_LINK_DIN_GPIO,
        },
    };
    rc = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (rc != ESP_OK) {
        (void)i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return rc;
    }
    rc = i2s_channel_enable(s_rx_chan);
    if (rc != ESP_OK) {
        (void)i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return rc;
    }

    if (xTaskCreate(rx_task, "p4_link_rx", P4_AUDIO_LINK_RX_TASK_STACK,
                    NULL, P4_AUDIO_LINK_RX_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "P4 audio link RX started: BCLK=%d WS=%d DIN=%d pipe=%u Hz",
             CONFIG_P4_AUDIO_LINK_BCLK_GPIO,
             CONFIG_P4_AUDIO_LINK_WS_GPIO,
             CONFIG_P4_AUDIO_LINK_DIN_GPIO,
             (unsigned)P4_AUDIO_LINK_I2S_PIPE_RATE_HZ);
    return ESP_OK;
}

#endif /* CONFIG_P4_AUDIO_LINK_ENABLED */
