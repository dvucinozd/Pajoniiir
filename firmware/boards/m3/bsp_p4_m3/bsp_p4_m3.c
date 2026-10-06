#include "bsp_p4_m3.h"
#include "bsp_dsi_id_probe.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_idf_version.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_hosted.h"
#include "audio_output_timing.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "mipi_dsi_priv.h"
#undef TAG
#include "esp_lcd_touch_ft5x06.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "driver/sdmmc_host.h"
#include "ff.h"

#include <string.h>

static const char *TAG = "bsp_m3";

// ── Display controller & configuration ──────────────────────────────────────
// The DSI-506 is wired in its factory fixed-backlight mode. GPIO23 is not
// connected unless the module's 0-ohm selector is moved and PWM/GND wires are
// added. In the factory configuration the ATTiny-compatible controller at 0x45
// sequences panel power and controls backlight brightness.
#define BSP_PANEL_CTRL_I2C_ADDR 0x45
#define BSP_PANEL_CTRL_I2C_HZ   100000
#define BSP_BL_DEFAULT_PCT      100

enum {
    BSP_PANEL_REG_ID       = 0x80,
    BSP_PANEL_REG_PORTA    = 0x81,
    BSP_PANEL_REG_PORTB    = 0x82,
    BSP_PANEL_REG_PORTC    = 0x83,
    BSP_PANEL_REG_POWERON  = 0x85,
    BSP_PANEL_REG_PWM      = 0x86,
    BSP_PANEL_REG_ID2      = 0x92,
};

// ICN6211 is only an identification candidate, NOT a selected panel driver.
// Read its ID registers only if the direct I2C address responds after power-on.
#define BSP_ICN6211_CANDIDATE_ADDR 0x2c
#define BSP_DSI_ID_TIMEOUT_US     200000

// ── Shared I2C bus (FT5426 touch @0x38 on pins 11/12 of J2 DSI connector) ────
#define BSP_I2C_PORT            I2C_NUM_1
#define BSP_I2C_SDA_GPIO        GPIO_NUM_7
#define BSP_I2C_SCL_GPIO        GPIO_NUM_8
#define BSP_TOUCH_I2C_HZ        100000

// ── Master Out: PCM5102A I2S DAC ─────────────────────────────────────────────
#define BSP_PCM5102_I2S_NUM        I2S_NUM_1
#define BSP_PCM5102_BCLK_GPIO      GPIO_NUM_1
#define BSP_PCM5102_WS_GPIO        GPIO_NUM_2
#define BSP_PCM5102_DOUT_GPIO      GPIO_NUM_3
#define BSP_PCM5102_MCLK_GPIO      I2S_GPIO_UNUSED

// Retired onboard NS4150 monitor amplifier. Keep its enable pin low.
#define BSP_AUDIO_PA_GPIO          GPIO_NUM_11
#define BSP_SPEAKER_ROUTE_RETIRED  1

// ── MicroSD card on SDMMC Slot 0 ─────────────────────────────────────────────
#define BSP_SD_LDO_CHAN             4
#define BSP_SD_MOUNT_ATTEMPTS       3
#define BSP_SD_MOUNT_RETRY_DELAY_MS 150

#if defined(CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE) && \
    ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
/* ESP-Hosted's IDF 6 constructor owns the one physical SDMMC controller and
 * registers SDIO slot 1 before app_main(). The microSD card shares that
 * controller on slot 0, so a second sdmmc_host_init() must be skipped. Keep the
 * default slot-aware deinit_p callback: it removes only slot 0 after a failed
 * mount or unmount while the Hosted slot keeps the controller alive. */
static esp_err_t bsp_sdmmc_host_already_initialized(void)
{
    return ESP_OK;
}
#endif

// ── MIPI DSI PHY power (ESP32-P4 internal LDO VO3 → VDD_MIPI_DPHY 2.5 V) ──────
#define BSP_MIPI_LDO_CHAN       3
#define BSP_MIPI_LDO_MV         2500

// ── MIPI DSI link ────────────────────────────────────────────────────────────
#define BSP_DSI_LANE_NUM        1
#define BSP_DSI_LANE_MBPS       800

// ── 5.0" MIPI-DSI IPS Video Timing (800x480 native landscape) ────────────────
// Horizontal timing is based on the Raspberry Pi Linux
// vc4-kms-dsi-waveshare-800x480 overlay: one lane, RGB888, 27.777 MHz and
// HFP/HSW/HBP=59/2/45. VFP is extended from the reference value 7 to 109 so
// Overview work is phase-locked to an approximately 50 Hz panel refresh:
//   Htotal = 800 + 59 + 2 + 45 = 906
//   Vtotal = 480 + 109 + 2 + 22 = 613
//   refresh = 27.777 MHz / (906 * 613) = 50.0146 Hz
// The same refresh-synchronised 50 Hz strategy previously removed the physical
// DSI "watery waveform" effect without changing horizontal phase or colours.
#define BSP_DPI_CLK_MHZ         27.777f
#define BSP_LCD_HSYNC           2
#define BSP_LCD_HBP             45
#define BSP_LCD_HFP             59
#define BSP_LCD_VSYNC           2
#define BSP_LCD_VBP             22
#define BSP_LCD_VFP             109

static esp_lcd_panel_handle_t   s_panel    = NULL;
static esp_ldo_channel_handle_t s_mipi_ldo = NULL;
static i2c_master_bus_handle_t  s_i2c_bus  = NULL;
static i2c_master_dev_handle_t  s_panel_ctrl = NULL;
static esp_lcd_touch_handle_t   s_touch    = NULL;
static i2s_chan_handle_t        s_i2s_tx_pcm5102 = NULL;
static bool                     s_i2s_tx_pcm5102_enabled = false;
static esp_codec_dev_handle_t   s_codec    = NULL;
static bsp_audio_out_t          s_audio_out = BSP_AUDIO_OUT_RCA;
static bool                     s_audio_pa_gpio_ready = false;
static bool                     s_speaker_pa_enabled = false;
static bsp_monitor_route_t      s_monitor_route = BSP_MONITOR_ROUTE_HEADPHONES;
static sdmmc_card_t            *s_sd_card  = NULL;
static sd_pwr_ctrl_handle_t     s_sd_pwr   = NULL;
static bool                    s_i2c_last_seen[128];

esp_lcd_panel_handle_t bsp_display_get_panel_handle(void)
{
    return s_panel;
}

esp_lcd_touch_handle_t bsp_touch_get_handle(void)
{
    return s_touch;
}

i2c_master_bus_handle_t bsp_get_i2c_bus(void)
{
    return s_i2c_bus;
}

static void bsp_display_i2c_scan(const char *phase)
{
    unsigned found = 0;
    unsigned errors = 0;
    // Bounded arrival-only address probes; never write register contents.
    for (uint8_t address = 0x08; address <= 0x77; ++address) {
        esp_err_t err = i2c_master_probe(s_i2c_bus, address, 10);
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            ++errors;
            ESP_LOGW(TAG, "DSI I2C [%s] probe 0x%02X failed: %s",
                     phase, address, esp_err_to_name(err));
            continue; // A bus error is not proof of device removal.
        }
        bool present = err == ESP_OK;
        if (present) {
            ESP_LOGW(TAG, "DSI I2C [%s] addr=0x%02X (%s)", phase, address,
                     s_i2c_last_seen[address] ? "still present" : "new response");
            ++found;
        } else if (s_i2c_last_seen[address]) {
            ESP_LOGW(TAG, "DSI I2C [%s] addr=0x%02X no longer responds", phase, address);
        }
        s_i2c_last_seen[address] = present;
    }
    ESP_LOGW(TAG, "DSI I2C [%s] scan complete: %u device(s), %u probe error(s)",
             phase, found, errors);
}

static esp_err_t bsp_i2c_bus_init(void)
{
    if (s_i2c_bus) {
        return ESP_OK;
    }
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BSP_I2C_SDA_GPIO,
        .scl_io_num = BSP_I2C_SCL_GPIO,
        .i2c_port   = BSP_I2C_PORT,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        return err;
    }

    bsp_display_i2c_scan("before-power-sequence");

    return ESP_OK;
}

static esp_err_t panel_ctrl_write(uint8_t reg, uint8_t value)
{
    const uint8_t tx[] = { reg, value };
    return i2c_master_transmit(s_panel_ctrl, tx, sizeof(tx), 20);
}

static esp_err_t panel_ctrl_read(uint8_t reg, uint8_t *value)
{
    ESP_RETURN_ON_FALSE(value, ESP_ERR_INVALID_ARG, TAG, "panel controller read target is NULL");
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_panel_ctrl, &reg, 1, 20),
                        TAG, "panel controller register select 0x%02X failed", reg);
    // Match the controller's required stop-to-read settling time.
    esp_rom_delay_us(200);
    return i2c_master_receive(s_panel_ctrl, value, 1, 20);
}

static void panel_ctrl_log_ports(const char *phase)
{
    // Raw readbacks only. Clone pin functions are not established by the
    // Raspberry Pi reference driver, so do not infer reset/power voltages.
    const uint8_t regs[] = {BSP_PANEL_REG_PORTA, BSP_PANEL_REG_PORTB, BSP_PANEL_REG_PORTC};
    for (unsigned i = 0; i < sizeof(regs); ++i) {
        uint8_t value = 0;
        esp_err_t err = panel_ctrl_read(regs[i], &value);
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "DSI MCU [%s] reg=0x%02X raw=0x%02X", phase, regs[i], value);
        } else {
            ESP_LOGW(TAG, "DSI MCU [%s] reg=0x%02X read failed: %s", phase, regs[i], esp_err_to_name(err));
        }
    }
}

static esp_err_t panel_ctrl_init(void)
{
    if (s_panel_ctrl) {
        return ESP_OK;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_PANEL_CTRL_I2C_ADDR,
        .scl_speed_hz = BSP_PANEL_CTRL_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_panel_ctrl),
                        TAG, "panel controller 0x45 attach failed");

    uint8_t id = 0;
    uint8_t id2 = 0;
    esp_err_t id_err = panel_ctrl_read(BSP_PANEL_REG_ID, &id);
    esp_err_t id2_err = panel_ctrl_read(BSP_PANEL_REG_ID2, &id2);
    if (id_err == ESP_OK && id2_err == ESP_OK) {
        ESP_LOGW(TAG, "DSI panel controller ready (addr=0x45, ID=0x%02X, ID2=0x%02X)", id, id2);
    } else {
        ESP_LOGW(TAG, "DSI panel controller IDs unavailable (ID=%s ID2=%s); continuing with detected 0x45 device",
                 esp_err_to_name(id_err), esp_err_to_name(id2_err));
    }

    panel_ctrl_log_ports("before-power-off");
    // Begin from a deterministic dark/powered-down state. The controller can
    // ignore I2C briefly after POWERON writes, hence the mandatory settling.
    ESP_RETURN_ON_ERROR(panel_ctrl_write(BSP_PANEL_REG_PWM, 0), TAG, "panel backlight off failed");
    ESP_RETURN_ON_ERROR(panel_ctrl_write(BSP_PANEL_REG_POWERON, 0), TAG, "panel power off failed");
    vTaskDelay(pdMS_TO_TICKS(25));
    panel_ctrl_log_ports("power-off");
    return ESP_OK;
}

static esp_err_t panel_ctrl_power_on(void)
{
    // Keep the bridge off for at least 100 ms before a new power-on edge.
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(panel_ctrl_write(BSP_PANEL_REG_POWERON, 1), TAG, "panel power on failed");
    vTaskDelay(pdMS_TO_TICKS(25));

    uint8_t port_b = 0;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if (panel_ctrl_read(BSP_PANEL_REG_PORTB, &port_b) == ESP_OK && (port_b & 0x01u)) {
            ESP_LOGW(TAG, "DSI legacy power-status bit asserted (PORTB=0x%02X); not a voltage measurement", port_b);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGW(TAG, "DSI power status unconfirmed (last PORTB=0x%02X); diagnosis only", port_b);
    return ESP_OK;
}

static void bsp_display_bridge_identify(void)
{
    esp_err_t err = i2c_master_probe(s_i2c_bus, BSP_ICN6211_CANDIDATE_ADDR, 10);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DSI bridge candidate 0x2C unavailable: %s; identity remains UNKNOWN",
                 esp_err_to_name(err));
        return; // A hidden/private I2C segment cannot be ruled out by this scan.
    }

    i2c_master_dev_handle_t candidate = NULL;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_ICN6211_CANDIDATE_ADDR,
        .scl_speed_hz = BSP_PANEL_CTRL_I2C_HZ,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &cfg, &candidate);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DSI bridge candidate attach failed: %s", esp_err_to_name(err));
        return;
    }
    // Linux chipone-icn6211.c checks VENDOR_ID/DEVICE_ID_H/DEVICE_ID_L.
    // Only select read-only ID registers; do not unlock or configure the chip.
    uint8_t id[4] = {0};
    for (uint8_t reg = 0; reg < sizeof(id); ++reg) {
        err = i2c_master_transmit_receive(candidate, &reg, 1, &id[reg], 1, 20);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "DSI bridge ID register 0x%02X read failed: %s", reg, esp_err_to_name(err));
            break;
        }
    }
    if (err == ESP_OK) {
        bool matched = id[0] == 0xc1 && id[1] == 0x62 && id[2] == 0x11;
        ESP_LOGW(TAG, "DSI bridge 0x2C ID=%02X %02X %02X rev=%02X -> %s (no configuration writes)",
                 id[0], id[1], id[2], id[3], matched ? "ICN6211 identity match" : "UNKNOWN");
    }
    err = i2c_master_bus_rm_device(candidate);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DSI bridge candidate detach failed: %s", esp_err_to_name(err));
    }
}

// Arrival-only read, before any DBI/DPI client owns the bus. IDF 6.0.2's HAL
// generic read spins indefinitely on missing RX data, so use bounded LL polls.
// Linux atomic_enable uses chipone_readb/regmap_read for each ID byte. Match
// that path: MRPS=1 and Generic Read(reg, 1), never a configuration write.
static bool bsp_dsi_read_id_byte(void *context, uint8_t reg, uint8_t *value)
{
    esp_lcd_dsi_bus_handle_t bus = context;
    if (!bus || !value || reg > 3) return false;
    dsi_host_dev_t *host = bus->hal.host;
    const int64_t started = esp_timer_get_time();
    const int64_t deadline = started + BSP_DSI_ID_TIMEOUT_US;
    const char *stage = "initial-state";
    esp_err_t result = ESP_ERR_INVALID_STATE;
    uint32_t errors0 = host->int_st0.val;
    uint32_t errors1 = host->int_st1.val;
    *value = 0;
    if (!mipi_dsi_host_ll_gen_is_cmd_fifo_empty(host) ||
        mipi_dsi_host_ll_gen_is_read_cmd_busy(host) ||
        !mipi_dsi_host_ll_gen_is_read_fifo_empty(host) || errors0 || errors1) {
        goto done; // Never interpret stale FIFO data as an ID.
    }

    mipi_dsi_host_ll_enable_video_mode(host, false);
    mipi_dsi_host_ll_enable_cmd_ack(host, false);
    mipi_dsi_host_ll_set_mrps_speed_mode(host, MIPI_DSI_LL_TRANS_SPEED_LP);
    mipi_dsi_host_ll_set_gen_short_rd_speed_mode(host, 2, MIPI_DSI_LL_TRANS_SPEED_LP);
    mipi_dsi_host_ll_gen_set_rx_vcid(host, 0);
    mipi_dsi_host_ll_enable_bta(host, true);

    stage = "maximum-return-size";
    result = ESP_ERR_TIMEOUT;
    while (mipi_dsi_host_ll_gen_is_cmd_fifo_full(host)) {
        if (esp_timer_get_time() >= deadline) goto done;
        vTaskDelay(1);
    }
    mipi_dsi_host_ll_gen_set_packet_header(host, 0, MIPI_DSI_DT_SET_MAXIMUM_RETURN_PKT, 0, 1);
    // Wait for MRPS to leave the command FIFO before issuing the read request.
    while (!mipi_dsi_host_ll_gen_is_cmd_fifo_empty(host)) {
        if (esp_timer_get_time() >= deadline) goto done;
        vTaskDelay(1);
    }

    stage = "generic-read-response";
    mipi_dsi_host_ll_gen_set_packet_header(host, 0, MIPI_DSI_DT_GENERIC_READ_REQUEST_2, 1, reg);
    while (mipi_dsi_host_ll_gen_is_read_cmd_busy(host) ||
           mipi_dsi_host_ll_gen_is_read_fifo_empty(host)) {
        errors0 |= host->int_st0.val;
        errors1 |= host->int_st1.val;
        if (errors0 || errors1) {
            result = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
        if (esp_timer_get_time() >= deadline) goto done;
        vTaskDelay(1);
    }
    // One requested byte occupies a FIFO word. The LL exposes payload words,
    // not the response header/length: upper bytes are not additional IDs.
    // Extra FIFO data or transport errors fail closed.
    const uint32_t payload = mipi_dsi_host_ll_gen_read_payload_fifo(host);
    *value = (uint8_t)payload;
    result = mipi_dsi_host_ll_gen_is_read_fifo_empty(host) ? ESP_OK : ESP_ERR_INVALID_SIZE;
    stage = "response-received";

done:
    errors0 |= host->int_st0.val;
    errors1 |= host->int_st1.val;
    if (result == ESP_OK && (errors0 || errors1)) result = ESP_ERR_INVALID_RESPONSE;
    ESP_LOGW(TAG, "DSI ID byte reg=0x%02X: %s stage=%s elapsed=%lld us status=%08lX phy=%08lX err=%08lX/%08lX",
             reg, esp_err_to_name(result), stage, (long long)(esp_timer_get_time() - started),
             (unsigned long)host->cmd_pkt_status.val, (unsigned long)host->phy_status.val,
             (unsigned long)errors0, (unsigned long)errors1);
    if (errors0 & 0xffffu) {
        ESP_LOGW(TAG, "DSI peripheral ACK/error flags=0x%04lX; response is not an ID",
                 (unsigned long)(errors0 & 0xffffu));
    }
    return result == ESP_OK;
}

static void bsp_display_dsi_identify(esp_lcd_dsi_bus_handle_t bus)
{
    uint8_t id[4] = {0};
    ESP_LOGW(TAG, "DSI candidate ID probe: individual LP reads {reg,1}, 200 ms deadline per byte");
    bool verified = bsp_dsi_id_probe(bsp_dsi_read_id_byte, bus, id);
    ESP_LOGW(TAG, "DSI candidate ID bytes (may be partial)=%02X %02X %02X rev=%02X -> %s (no configuration writes)",
             id[0], id[1], id[2], id[3],
             verified ? "ICN6211 identity matched twice" : "UNKNOWN");
}

static void bsp_dsi_log_clock(esp_lcd_dsi_bus_handle_t bus, const char *phase)
{
    // Readback proves the host request/state, not the waveform at the panel.
    dsi_host_dev_t *host = bus->hal.host;
    ESP_LOGW(TAG, "DSI clock [%s] t=%lld us request_hs=%u auto=%u lpclk=%08lX phy=%08lX",
             phase, (long long)esp_timer_get_time(),
             (unsigned)host->lpclk_ctrl.phy_txrequestclkhs,
             (unsigned)host->lpclk_ctrl.auto_clklane_ctrl,
             (unsigned long)host->lpclk_ctrl.val, (unsigned long)host->phy_status.val);
}

_Static_assert(BSP_LCD_FRAMEBUFFER_COUNT == 1u,
               "the current LVGL/PPA backend supports exactly one DPI framebuffer");

esp_err_t bsp_display_init(void)
{
    // ── 1. Put the 0x45 panel controller in a deterministic off state ───────
    ESP_RETURN_ON_ERROR(bsp_i2c_bus_init(), TAG, "display I2C bus init failed");
    ESP_RETURN_ON_ERROR(panel_ctrl_init(), TAG, "display controller init failed");
    bsp_display_i2c_scan("power-off");

    // ── 2. Power up MIPI DSI PHY via internal LDO ───────────────────────────
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = BSP_MIPI_LDO_CHAN,
        .voltage_mv = BSP_MIPI_LDO_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &s_mipi_ldo));
    ESP_LOGI(TAG, "MIPI DSI PHY powered (LDO VO%d @ %d mV)", BSP_MIPI_LDO_CHAN, BSP_MIPI_LDO_MV);

    // ── 3. Create the single-lane DSI bus ───────────────────────────────────
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = BSP_DSI_LANE_NUM,
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = BSP_DSI_LANE_MBPS,
        // Isolated arrival experiment: continuous clock before the existing
        // software POWERON edge. Does not assert a 100-ms physical-boot window.
        .flags = {
            .clock_lane_force_hs = true,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus));
    bsp_dsi_log_clock(dsi_bus, "before-power-on");

    // ── 4. Identify the powered bridge, without selecting a vendor driver ──
    ESP_RETURN_ON_ERROR(panel_ctrl_power_on(), TAG, "display bridge power-on failed");
    bsp_dsi_log_clock(dsi_bus, "after-power-on");
    bsp_display_i2c_scan("power-on-early");
    vTaskDelay(pdMS_TO_TICKS(250));
    bsp_display_i2c_scan("power-on-settled");
    panel_ctrl_log_ports("power-on-settled");
    bsp_display_bridge_identify();
    bsp_display_dsi_identify(dsi_bus);
    // Always reset the host after the read (success OR timeout). A stuck BTA,
    // pending response, or command-mode options must not leak into DPI startup.
    // No DBI/DPI clients have been created yet, so no other handles are invalidated.
    mipi_dsi_hal_deinit(&dsi_bus->hal);
    ESP_ERROR_CHECK(esp_lcd_del_dsi_bus(dsi_bus));
    dsi_bus = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus));
    // The existing ID-probe recovery interrupts the clock briefly here.
    // Reapply the same forced-HS request; do not call this gap-free operation.
    bsp_dsi_log_clock(dsi_bus, "after-host-recreate");
    ESP_LOGW(TAG, "DSI host recreated after ID probe; continuing diagnostic video");
    ESP_LOGW(TAG, "DSI diagnostic: vendor bridge configuration withheld pending identification");

    // ── 5. DPI panel config (RGB888 memory AND wire on P4 rev1.3) ───────────
    // rev < 3.0 has one shared input/output format field: no RGB565->RGB888
    // conversion here. PPA converts the RGB565 UI source before scanout.
    _Static_assert(BSP_SCANOUT_BYTES_PER_PIXEL == 3u, "DPI RGB888 requires packed 3-byte pixels");
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel    = 0,
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = BSP_DPI_CLK_MHZ,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        .in_color_format    = LCD_COLOR_FMT_RGB888,
        .out_color_format   = LCD_COLOR_FMT_RGB888,
#else
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB888,
#endif
        .num_fbs            = BSP_LCD_FRAMEBUFFER_COUNT,
        .video_timing = {
            .h_size            = BSP_LCD_H_RES,
            .v_size            = BSP_LCD_V_RES,
            .hsync_pulse_width = BSP_LCD_HSYNC,
            .hsync_back_porch  = BSP_LCD_HBP,
            .hsync_front_porch = BSP_LCD_HFP,
            .vsync_pulse_width = BSP_LCD_VSYNC,
            .vsync_back_porch  = BSP_LCD_VBP,
            .vsync_front_porch = BSP_LCD_VFP,
        },
    };

    // ── 6. Create and start the DPI video stream ────────────────────────────
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(dsi_bus, &dpi_cfg, &s_panel));

    // The numbered framebuffer probe showed a one-block cyclic shift:
    // physical order 7,0,1,2,3,4,5,6 for 100-pixel blocks. The 100-pixel
    // phase closely matches the 106-pixel horizontal blanking interval. The
    // IDF default burst packetization removed the wrap on hardware while all
    // timing stayed unchanged, so it is the accepted mode for this module.
    mipi_dsi_host_ll_dpi_set_video_burst_type(dsi_bus->hal.host,
                                               MIPI_DSI_LL_VIDEO_BURST_WITH_SYNC_PULSES);
    mipi_dsi_host_ll_dpi_enable_frame_ack(dsi_bus->hal.host, false);
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    bsp_dsi_log_clock(dsi_bus, "video-started");
    ESP_LOGI(TAG, "DSI host stream started (%dx%d, %.4f MHz, RGB888->RGB888, burst/no-ACK)",
             BSP_LCD_H_RES, BSP_LCD_V_RES, (double)BSP_DPI_CLK_MHZ);

    // ── 7. Enable the panel and its factory-controlled backlight ─────────────
    ESP_RETURN_ON_ERROR(panel_ctrl_write(BSP_PANEL_REG_PORTA, (1u << 2)),
                        TAG, "panel orientation/enable failed");
    bsp_display_set_backlight(BSP_BL_DEFAULT_PCT);

    return ESP_OK;
}

void bsp_display_set_backlight(uint8_t pct)
{
    if (pct > 100) pct = 100;
    if (!s_panel_ctrl) {
        ESP_LOGW(TAG, "Backlight request ignored before panel controller init");
        return;
    }
    const uint8_t value = (uint8_t)((255u * pct) / 100u);
    esp_err_t err = panel_ctrl_write(BSP_PANEL_REG_PWM, value);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Backlight update to %u%% failed: %s", pct, esp_err_to_name(err));
    }
}

esp_err_t bsp_touch_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_bus_init(), TAG, "I2C bus init failed");

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    // Keep the component default. The attached DYL0023 module ACKs address
    // probes at either rate, but its register reads are not reliable at the
    // old 400 kHz pre-arrival assumption over the display FFC.
    tp_io_cfg.scl_speed_hz = BSP_TOUCH_I2C_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io),
                        TAG, "FT5426 panel IO failed");

    // Native panel is 800x480 landscape. Touch controller maps 1:1 without swap/mirror.
    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = BSP_LCD_H_RES,   // 800
        .y_max        = BSP_LCD_V_RES,   // 480
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = 0,
            .mirror_x = 1,
            .mirror_y = 1,
        },
    };
    esp_err_t err = esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FT5426 touch controller not detected (no screen attached?): %s", esp_err_to_name(err));
        s_touch = NULL;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "FT5426 touch ready (I2C SDA=%d SCL=%d, addr=0x38, %u Hz, 800x480 landscape)",
             BSP_I2C_SDA_GPIO, BSP_I2C_SCL_GPIO, BSP_TOUCH_I2C_HZ);
    return ESP_OK;
}

esp_codec_dev_handle_t bsp_audio_get_codec_dev(void)
{
    return s_codec;
}

i2s_chan_handle_t bsp_audio_get_main_i2s_tx(void)
{
    return s_i2s_tx_pcm5102;
}

esp_err_t bsp_audio_force_safe_boot_state(void)
{
    if (!s_audio_pa_gpio_ready) {
        /* Load the output latch low before enabling the pad so the retired
         * onboard amplifier cannot pulse during boot. */
        ESP_RETURN_ON_ERROR(gpio_set_level(BSP_AUDIO_PA_GPIO, 0), TAG,
                            "speaker PA safe latch failed");
        gpio_config_t pa_cfg = {
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << BSP_AUDIO_PA_GPIO,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&pa_cfg), TAG,
                            "speaker PA gpio config failed");
        s_audio_pa_gpio_ready = true;
    }
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_AUDIO_PA_GPIO, 0), TAG,
                        "speaker PA safe level failed");
    s_speaker_pa_enabled = false;
    s_audio_out = BSP_AUDIO_OUT_RCA;
    s_monitor_route = BSP_MONITOR_ROUTE_HEADPHONES;
    return ESP_OK;
}

static DRAM_ATTR uint32_t s_main_dma_completions;
static DRAM_ATTR uint32_t s_main_dma_last_us;
static DRAM_ATTR uint32_t s_main_dma_max_gap_us;
static DRAM_ATTR uint32_t s_main_dma_queue_overflows;
static DRAM_ATTR uint32_t s_main_dma_interrupted_priority;
static DRAM_ATTR char s_main_dma_interrupted_task[16];

#if CONFIG_BSP_PCM5102A_MAIN_OUT
static bool IRAM_ATTR main_dma_sent(i2s_chan_handle_t chan, i2s_event_data_t *event, void *user)
{
    (void)chan; (void)event; (void)user;
    uint32_t now = (uint32_t)esp_timer_get_time();
    uint32_t last = s_main_dma_last_us;
    s_main_dma_last_us = now;
    uint32_t gap = now - last;
    if (last && gap > __atomic_load_n(&s_main_dma_max_gap_us, __ATOMIC_RELAXED))
        __atomic_store_n(&s_main_dma_max_gap_us, gap, __ATOMIC_RELAXED);
    __atomic_add_fetch(&s_main_dma_completions, 1u, __ATOMIC_RELAXED);
    uint32_t priority = (uint32_t)uxTaskPriorityGetFromISR(NULL);
    if (priority > s_main_dma_interrupted_priority) {
        const char *name = pcTaskGetName(NULL);
        unsigned i = 0;
        for (; i < sizeof(s_main_dma_interrupted_task) - 1u && name[i]; ++i)
            s_main_dma_interrupted_task[i] = name[i];
        s_main_dma_interrupted_task[i] = '\0';
        s_main_dma_interrupted_priority = priority;
    }
    return false;
}

static bool IRAM_ATTR main_dma_overflow(i2s_chan_handle_t chan, i2s_event_data_t *event, void *user)
{
    (void)chan; (void)event; (void)user;
    __atomic_add_fetch(&s_main_dma_queue_overflows, 1u, __ATOMIC_RELAXED);
    return false;
}
#endif

void bsp_audio_main_dma_snapshot(bsp_main_dma_diag_t *out, bool reset_gap)
{
    if (!out) return;
    out->completions = __atomic_load_n(&s_main_dma_completions, __ATOMIC_RELAXED);
    out->queue_overflows = __atomic_load_n(&s_main_dma_queue_overflows, __ATOMIC_RELAXED);
    out->max_gap_us = reset_gap
        ? __atomic_exchange_n(&s_main_dma_max_gap_us, 0u, __ATOMIC_RELAXED)
        : __atomic_load_n(&s_main_dma_max_gap_us, __ATOMIC_RELAXED);
    out->interrupted_priority = s_main_dma_interrupted_priority;
    memcpy(out->interrupted_task, s_main_dma_interrupted_task, sizeof(out->interrupted_task));
    out->interrupted_task[sizeof(out->interrupted_task) - 1u] = '\0';
    if (reset_gap) s_main_dma_interrupted_priority = 0u;
}

static esp_err_t bsp_audio_init_i2s_pcm5102(void)
{
#if CONFIG_BSP_PCM5102A_MAIN_OUT
    if (s_i2s_tx_pcm5102) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_PCM5102_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_frame_num = AUDIO_OUTPUT_BLOCK_FRAMES;
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx_pcm5102, NULL), TAG, "pcm5102 i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_PCM5102_MCLK_GPIO,
            .bclk = BSP_PCM5102_BCLK_GPIO,
            .ws = BSP_PCM5102_WS_GPIO,
            .dout = BSP_PCM5102_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx_pcm5102, &std_cfg), TAG, "pcm5102 i2s std init failed");
    i2s_event_callbacks_t callbacks = {.on_sent = main_dma_sent, .on_send_q_ovf = main_dma_overflow};
    ESP_RETURN_ON_ERROR(i2s_channel_register_event_callback(s_i2s_tx_pcm5102, &callbacks, NULL), TAG, "pcm5102 callbacks failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx_pcm5102), TAG, "pcm5102 i2s enable failed");
    s_i2s_tx_pcm5102_enabled = true;
    ESP_LOGI(TAG, "PCM5102A main out ready: BCLK=%d WS=%d DOUT=%d",
             BSP_PCM5102_BCLK_GPIO, BSP_PCM5102_WS_GPIO, BSP_PCM5102_DOUT_GPIO);
#endif
    return ESP_OK;
}

esp_err_t bsp_audio_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_audio_force_safe_boot_state(), TAG,
                        "audio safe boot state failed");
    ESP_RETURN_ON_ERROR(bsp_audio_init_i2s_pcm5102(), TAG, "PCM5102A init failed");
    return ESP_OK;
}

esp_err_t bsp_audio_set_speaker_pa_enabled(bool enabled)
{
    ESP_RETURN_ON_ERROR(bsp_audio_force_safe_boot_state(), TAG,
                        "speaker PA safe state failed");
#if BSP_SPEAKER_ROUTE_RETIRED
    if (enabled) {
        ESP_LOGE(TAG, "speaker PA route is unavailable in the PCM5102A product configuration");
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    return ESP_OK;
}

bool bsp_audio_get_speaker_pa_enabled(void)
{
    return s_speaker_pa_enabled;
}

esp_err_t bsp_audio_set_monitor_route(bsp_monitor_route_t route)
{
    switch (route) {
    case BSP_MONITOR_ROUTE_HEADPHONES:
        ESP_RETURN_ON_ERROR(bsp_audio_set_speaker_pa_enabled(false), TAG,
                            "speaker PA off failed");
        s_monitor_route = route;
        return ESP_OK;
    case BSP_MONITOR_ROUTE_SPEAKER:
        (void)bsp_audio_set_speaker_pa_enabled(false);
        return ESP_ERR_NOT_SUPPORTED;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

bsp_monitor_route_t bsp_audio_get_monitor_route(void)
{
    return s_monitor_route;
}

esp_err_t bsp_audio_set_output(bsp_audio_out_t out)
{
    if (out != BSP_AUDIO_OUT_SPEAKER && out != BSP_AUDIO_OUT_RCA) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out == BSP_AUDIO_OUT_SPEAKER) {
        (void)bsp_audio_set_speaker_pa_enabled(false);
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_RETURN_ON_ERROR(bsp_audio_set_monitor_route(BSP_MONITOR_ROUTE_HEADPHONES), TAG,
                        "headphones monitor route failed");
    s_audio_out = out;
    return ESP_OK;
}

bsp_audio_out_t bsp_audio_get_output(void)
{
    return s_audio_out;
}

esp_err_t bsp_audio_main_i2s_set_sample_rate(uint32_t sample_rate)
{
#if CONFIG_BSP_PCM5102A_MAIN_OUT
    if (!s_i2s_tx_pcm5102 || sample_rate == 0u) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_i2s_tx_pcm5102_enabled) {
        ESP_RETURN_ON_ERROR(i2s_channel_disable(s_i2s_tx_pcm5102), TAG,
                            "pcm5102 disable failed");
        s_i2s_tx_pcm5102_enabled = false;
    }
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_i2s_tx_pcm5102, &clk_cfg), TAG, "pcm5102 clock reconfig failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx_pcm5102), TAG, "pcm5102 enable failed");
    s_i2s_tx_pcm5102_enabled = true;
    return ESP_OK;
#else
    (void)sample_rate;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t bsp_audio_main_i2s_abort_write(void)
{
#if CONFIG_BSP_PCM5102A_MAIN_OUT
    if (!s_i2s_tx_pcm5102) return ESP_ERR_INVALID_STATE;
    if (!s_i2s_tx_pcm5102_enabled) return ESP_OK;
    esp_err_t rc = i2s_channel_disable(s_i2s_tx_pcm5102);
    if (rc == ESP_OK) s_i2s_tx_pcm5102_enabled = false;
    return rc;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t bsp_sd_init(void)
{
    if (s_sd_card) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
#if defined(CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE) && \
    ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    host.init = bsp_sdmmc_host_already_initialized;
#endif
    if (!s_sd_pwr) {
        sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = BSP_SD_LDO_CHAN,
        };
        esp_err_t pwr_rc = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_sd_pwr);
        if (pwr_rc != ESP_OK) {
            ESP_LOGW(TAG, "SD LDO%d power control unavailable (%s) — /sd unavailable",
                     BSP_SD_LDO_CHAN, esp_err_to_name(pwr_rc));
            return ESP_OK;
        }
    }
    host.pwr_ctrl_handle = s_sd_pwr;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = GPIO_NUM_43;
    slot_config.cmd = GPIO_NUM_44;
    slot_config.d0  = GPIO_NUM_39;
    slot_config.d1  = GPIO_NUM_40;
    slot_config.d2  = GPIO_NUM_41;
    slot_config.d3  = GPIO_NUM_42;
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "mounting SD at /sd (slot=%d width=%d clk=%d cmd=%d d0=%d d1=%d d2=%d d3=%d)",
             host.slot, slot_config.width, slot_config.clk, slot_config.cmd,
             slot_config.d0, slot_config.d1, slot_config.d2, slot_config.d3);

    esp_err_t rc = ESP_FAIL;
    for (int attempt = 1; attempt <= BSP_SD_MOUNT_ATTEMPTS; attempt++) {
        rc = esp_vfs_fat_sdmmc_mount("/sd", &host, &slot_config, &mount_config, &s_sd_card);
        if (rc == ESP_OK) {
            break;
        }
        s_sd_card = NULL;
        ESP_LOGW(TAG, "SD mount attempt %d/%d failed (%s)%s",
                 attempt, BSP_SD_MOUNT_ATTEMPTS, esp_err_to_name(rc),
                 attempt < BSP_SD_MOUNT_ATTEMPTS ? " — retrying" : "");
        if (attempt < BSP_SD_MOUNT_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(BSP_SD_MOUNT_RETRY_DELAY_MS));
        }
    }
    if (rc != ESP_OK) {
        s_sd_card = NULL;
        ESP_LOGW(TAG, "SD mount skipped (%s) — /sd unavailable; USB media path continues",
                 esp_err_to_name(rc));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "SD card mounted at /sd");
    sdmmc_card_print_info(stdout, s_sd_card);
    return ESP_OK;
}

bool bsp_sd_is_mounted(void)
{
    return s_sd_card != NULL;
}

esp_err_t bsp_sd_get_status(bsp_sd_status_t *out_status)
{
    ESP_RETURN_ON_FALSE(out_status, ESP_ERR_INVALID_ARG, TAG, "missing SD status output");
    memset(out_status, 0, sizeof(*out_status));

    if (!s_sd_card) {
        return ESP_ERR_NOT_FOUND;
    }

    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    if (f_getfree("/sd", &free_clusters, &fs) != FR_OK || !fs) {
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t sector_size = 512u;
#if FF_MAX_SS != FF_MIN_SS
    sector_size = fs->ssize;
#endif
    uint64_t cluster_bytes = (uint64_t)fs->csize * (uint64_t)sector_size;
    uint64_t total_clusters = fs->n_fatent > 2 ? (uint64_t)(fs->n_fatent - 2) : 0;

    out_status->mounted = true;
    out_status->sector_size = sector_size;
    out_status->free_bytes = (uint64_t)free_clusters * cluster_bytes;
    out_status->total_bytes = total_clusters * cluster_bytes;
    return ESP_OK;
}

/* P4-only reset does not reset the C6. Quiesce it until Hosted owns EN. */
esp_err_t bsp_wifi_quiesce(void)
{
    const gpio_num_t pin = CONFIG_ESP_HOSTED_GPIO_SLAVE_RESET_SLAVE;
    gpio_config_t config = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "C6 EN configure");
    return gpio_set_level(pin, 0);
}
esp_err_t bsp_wifi_release_hosted(void) { return esp_hosted_deinit(); }
