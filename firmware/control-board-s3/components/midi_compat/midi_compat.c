#include "midi_compat.h"
#include "panel_io.h"
#include "esp_check.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/midi/midi_device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "midi";

// ─── MIDI status bytes ────────────────────────────────────────────────────────
#define NOTE_ON_CH1   0x90   // channel 1
#define NOTE_OFF_CH1  0x80
#define CC_CH1        0xB0
#define NOTE_ON_CH2   0x91   // channel 2 – jog
#define CC_CH2        0xB1
#define NOTE_ON_CH3   0x92   // channel 3 – browse
#define NOTE_OFF_CH3  0x82

#define MIDI_COMPAT_MAX_ENCODER_BURST 8

// ─── Button → MIDI Note (XDJ100SX upstream mapping) ─────────────────────────
static const uint8_t BTN_NOTE[BTN_COUNT] = {
    [BTN_EJECT]        = 63,   // 0x3F
    [BTN_TRACK_PREV]   = 64,   // 0x40
    [BTN_TRACK_NEXT]   = 65,   // 0x41
    [BTN_SEARCH_BACK]  = 66,   // 0x42
    [BTN_SEARCH_FWD]   = 67,   // 0x43
    [BTN_CUE]          = 61,   // 0x3D
    [BTN_PLAY]         = 60,   // 0x3C
    [BTN_PERF1]        = 68,   // 0x44  Jet
    [BTN_PERF2]        = 69,   // 0x45  Zip
    [BTN_PERF3]        = 70,   // 0x46  Wah
    [BTN_HOLD]         = 71,   // 0x47  Hold
    [BTN_MODE]         = 72,   // 0x48  Time/Auto Cue
    [BTN_MASTER_TEMPO] = 62,   // 0x3E
    [BTN_LOAD]         = 73,   // 0x49  Load
};

// ─── LED note → LED id (received from USB host) ──────────────────────────────
// From source-xdj100sx-analysis.md "LEDs expected by firmware".
static inline led_id_t note_to_led(uint8_t note)
{
    switch (note) {
        case 61: return LED_PLAY;
        case 62: return LED_CUE;
        case 63: return LED_BEAT;
        case 64: return LED_END;
        default: return LED_COUNT;   // sentinel: invalid
    }
}

// ─── USB MIDI descriptor ─────────────────────────────────────────────────────

enum {
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
    ITF_COUNT,
};

enum {
    EP_EMPTY = 0,
    EPNUM_MIDI,
};

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_MIDI * TUD_MIDI_DESC_LEN)

static const char *s_str_desc[] = {
    (char[]){ 0x09, 0x04 },   // 0: language – English 0x0409
    "DIY CDJ",                // 1: Manufacturer
    "CDJ100S-XXX",            // 2: Product
    "001",                    // 3: Serial
    "MIDI Interface",         // 4: MIDI interface string
};

static const uint8_t s_midi_fs_cfg_desc[] = {
    // Full-speed (12 Mbit/s): endpoint max packet size = 64 bytes
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUSB_DESC_TOTAL_LEN, 0, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, EPNUM_MIDI, (0x80 | EPNUM_MIDI), 64),
};

#if TUD_OPT_HIGH_SPEED
static const uint8_t s_midi_hs_cfg_desc[] = {
    // High-speed (480 Mbit/s): endpoint max packet size = 512 bytes
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUSB_DESC_TOTAL_LEN, 0, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, EPNUM_MIDI, (0x80 | EPNUM_MIDI), 512),
};
#endif

// ─── Thread-safety ────────────────────────────────────────────────────────────
static SemaphoreHandle_t s_tx_mutex;

static inline bool midi_send(const uint8_t *msg, uint32_t len)
{
    if (!tud_midi_mounted()) return false;
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    bool ok = (tud_midi_stream_write(0, msg, len) == len);
    xSemaphoreGive(s_tx_mutex);
    return ok;
}

// ─── MIDI RX (LED commands from USB host) ────────────────────────────────────

// Minimal 3-byte Note On/Off parser – only what we need for LED feedback.
typedef struct {
    uint8_t status;
    uint8_t byte1;
    int     need;
} rx_parser_t;

static void process_rx_byte(rx_parser_t *p, uint8_t b)
{
    if (b & 0x80) {
        uint8_t type = b & 0xF0;
        p->status = b;
        p->need   = (type == 0x80 || type == 0x90) ? 2 : 0;
        return;
    }
    if (p->need == 2) { p->byte1 = b; p->need = 1; return; }
    if (p->need == 1) {
        p->need = 0;
        led_id_t led = note_to_led(p->byte1);
        if (led < LED_COUNT) {
            bool on = ((p->status & 0xF0) == 0x90) && (b > 0);
            panel_led_set(led, on);
        }
    }
}

static void midi_rx_task(void *arg)
{
    rx_parser_t parser = { 0 };
    uint8_t     buf[64];
    while (1) {
        if (tud_midi_mounted() && tud_midi_available()) {
            uint32_t n = tud_midi_stream_read(buf, sizeof(buf));
            for (uint32_t i = 0; i < n; i++) {
                process_rx_byte(&parser, buf[i]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

esp_err_t midi_compat_init(QueueHandle_t panel_event_queue)
{
    (void)panel_event_queue;

    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) return ESP_ERR_NO_MEM;

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.string       = s_str_desc;
    tusb_cfg.descriptor.string_count = sizeof(s_str_desc) / sizeof(s_str_desc[0]);
    tusb_cfg.descriptor.full_speed_config = s_midi_fs_cfg_desc;
#if TUD_OPT_HIGH_SPEED
    tusb_cfg.descriptor.high_speed_config = s_midi_hs_cfg_desc;
    tusb_cfg.descriptor.qualifier         = NULL;
#endif

    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "install");

    if (xTaskCreate(midi_rx_task, "midi_rx", 2048, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "USB MIDI device ready – product=\"CDJ100S-XXX\"");
    return ESP_OK;
}

void midi_compat_process_event(const panel_event_t *ev)
{
    uint8_t msg[3];

    switch (ev->type) {

    case PANEL_EV_BUTTON: {
        if (ev->id >= BTN_COUNT) return;
        msg[0] = ev->value ? NOTE_ON_CH1 : NOTE_OFF_CH1;
        msg[1] = BTN_NOTE[ev->id];
        msg[2] = ev->value ? 127 : 0;
        midi_send(msg, 3);
        break;
    }

    case PANEL_EV_JOG: {
        // One CC 20 per encoder tick: CW = 65, CCW = 63 (XDJ100SX spec).
        int ticks = ev->value;
        uint8_t v = (ticks > 0) ? 65 : 63;
        int n = (ticks < 0) ? -ticks : ticks;
        if (n > MIDI_COMPAT_MAX_ENCODER_BURST) {
            n = MIDI_COMPAT_MAX_ENCODER_BURST;
        }
        msg[0] = CC_CH2;
        msg[1] = 20;
        msg[2] = v;
        for (int i = 0; i < n; i++) midi_send(msg, 3);
        break;
    }

    case PANEL_EV_BROWSE: {
        // Browse encoder uses upstream Ch3 note pulses: down/up = notes 70/71.
        int ticks = ev->value;
        uint8_t note = (ticks > 0) ? 70 : 71;
        int n = (ticks < 0) ? -ticks : ticks;
        if (n > MIDI_COMPAT_MAX_ENCODER_BURST) {
            n = MIDI_COMPAT_MAX_ENCODER_BURST;
        }
        for (int i = 0; i < n; i++) {
            uint8_t on[3]  = { NOTE_ON_CH3,  note, 127 };
            uint8_t off[3] = { NOTE_OFF_CH3, note, 0 };
            midi_send(on, 3);
            midi_send(off, 3);
        }
        break;
    }

    case PANEL_EV_PITCH: {
        // 14-bit pitch: CC 0 MSB + CC 32 LSB on Ch1.
        uint16_t val   = (uint16_t)ev->value;
        uint8_t msb[3] = { CC_CH1, 0x00,        (val >> 7) & 0x7F };
        uint8_t lsb[3] = { CC_CH1, 0x20 /*32*/, val        & 0x7F };
        midi_send(msb, 3);
        midi_send(lsb, 3);
        break;
    }
    }
}
