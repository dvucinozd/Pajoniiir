# PCM5102A Main Out and ES8311 Monitor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add PCM5102A as the stereo MAIN OUT path and keep ES8311 as the monitor/headphones/onboard-speaker path without leaking cue/PFL into the main output.

**Architecture:** The P4 audio engine produces two logical buffers per output block: `master_out[]` for MAIN OUT and `hp_out[]` for monitor/CUE. PCM5102A uses a second P4 I2S TX channel on the JP1 candidate pins GPIO50/GPIO52/GPIO51 only after bench verification. ES8311 remains on the existing I2S0 pins and continues to drive the board monitor path through GPIO11-controlled speaker PA.

**Tech Stack:** ESP-IDF v5.5, ESP32-P4 JC4880P443C_I_W, PCM5102A I2S stereo DAC, ES8311 codec via `esp_codec_dev`, FreeRTOS, C, host GCC tests under `tests/`, firmware build with `idf.py build`.

---

## Decisions From `DAC.md` Verification

| Item | Decision | Engineering rule |
| --- | --- | --- |
| ES8311 pinout | Accepted | Keep GPIO13 MCLK, GPIO12 BCLK, GPIO10 WS, GPIO9 DOUT, GPIO48 DIN, GPIO11 PA. |
| PCM5102A GPIO50/GPIO52/GPIO51 | Accepted as candidate set | Use BCLK GPIO50, WS/LRCK GPIO52, DOUT-to-DIN GPIO51 only after JP1 bench verification. |
| PCM5102A GPIO22/GPIO23/GPIO24/GPIO25 | Rejected | GPIO23 is LCD backlight PWM and must never be used for PCM5102A. |
| MAIN OUT semantics | Accepted | Cue/PFL must not modify `master_out[]`. |
| ES8311 monitor semantics | Accepted with mono caution | Treat ES8311 monitor as mono-safe; duplicate mono PFL to both PCM channels sent to ES8311. |
| Settings audio switch | Accepted with rename | Re-purpose from `Audio output: Speaker/RCA` to `Built-in monitor speaker`; it controls GPIO11 PA only. |
| Dual I2S clock sync | Deferred | First bring up independent I2S1. Add shared clock routing only if bench tests show drift/jitter. |

---

## File Responsibilities

| File | Responsibility |
| --- | --- |
| `firmware/main-deck-p4/PINOUT_P4.md` | P4 pin source of truth and PCM5102A GPIO approval record. |
| `firmware/main-deck-p4/components/bsp_jc4880/include/bsp_jc4880.h` | Public BSP APIs for ES8311 codec, PCM5102A I2S handle, and monitor speaker route. |
| `firmware/main-deck-p4/components/bsp_jc4880/bsp_jc4880.c` | P4 board pin ownership, I2S0 ES8311 init, I2S1 PCM5102A init, PA GPIO init-once, monitor route. |
| `firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h` | Testable master/headphone mix result types. |
| `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c` | Produces master and monitor frames without hardware dependencies. |
| `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h` | Public monitor mode and monitor route API. |
| `firmware/main-deck-p4/components/audio_engine/audio_engine.c` | Output task writes `master_out[]` to PCM5102A when enabled and `hp_out[]` to ES8311. |
| `firmware/main-deck-p4/components/app_settings/include/app_settings.h` | Persisted setting name migration from audio output to monitor speaker. |
| `firmware/main-deck-p4/components/app_settings/app_settings.c` | Settings default and setter for monitor speaker. |
| `firmware/main-deck-p4/components/ui/ui_settings.c` | Settings UI label and switch behavior. |
| `tests/audio_output_mixer/test_audio_output_mixer.c` | Host tests proving MAIN OUT remains stereo master while monitor gets cue/PFL. |
| `docs/DEVELOPMENT_PLAN.md` | Development roadmap link to this implementation plan. |
| `docs/HARDWARE_WIRING.md` | Final wiring notes after bench verification. |
| `docs/RISK_REGISTER.md` | Track dual-I2S clock drift and ES8311 mono monitor limitations. |

---

## Implementation Tasks

### Task 1: Create the P4 Pinout Gate for PCM5102A

**Files:**
- Create: `firmware/main-deck-p4/PINOUT_P4.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`

- [ ] **Step 1: Create the pinout file**

Create `firmware/main-deck-p4/PINOUT_P4.md` with this content:

```markdown
# ESP32-P4 Pinout Inventory for JC4880P443C_I_W

This file is the source of truth for P4-side peripheral wiring.
Do not use `firmware/control-board-s3/PINOUT.md` for P4 peripherals.

## Occupied P4 pins in current firmware

| GPIO | Owner | Evidence | PCM5102A use |
| --- | --- | --- | --- |
| GPIO5 | LCD reset | `BSP_LCD_RST_GPIO` | Forbidden |
| GPIO23 | LCD backlight PWM | `BSP_LCD_BL_GPIO` | Forbidden |
| GPIO7 | Shared I2C SDA | `BSP_I2C_SDA_GPIO` | Forbidden |
| GPIO8 | Shared I2C SCL | `BSP_I2C_SCL_GPIO` | Forbidden |
| GPIO13 | ES8311 I2S MCLK | `BSP_I2S_MCLK_GPIO` | Forbidden |
| GPIO12 | ES8311 I2S BCLK | `BSP_I2S_BCLK_GPIO` | Forbidden |
| GPIO10 | ES8311 I2S WS/LRCK | `BSP_I2S_WS_GPIO` | Forbidden |
| GPIO9 | ES8311 I2S DOUT | `BSP_I2S_DOUT_GPIO` | Forbidden |
| GPIO48 | ES8311 I2S DIN | `BSP_I2S_DIN_GPIO` | Forbidden |
| GPIO11 | Speaker PA enable | `BSP_AUDIO_PA_GPIO` | Forbidden |
| GPIO39 | SDMMC D0 | `slot_config.d0` | Forbidden |
| GPIO40 | SDMMC D1 | `slot_config.d1` | Forbidden |
| GPIO41 | SDMMC D2 | `slot_config.d2` | Forbidden |
| GPIO42 | SDMMC D3 | `slot_config.d3` | Forbidden |
| GPIO43 | SDMMC CLK | `slot_config.clk` | Forbidden |
| GPIO44 | SDMMC CMD | `slot_config.cmd` | Forbidden |

## JP1 candidate pins from board analysis

| JP1 pin | GPIO | Candidate use | Status |
| --- | --- | --- | --- |
| 5 | GPIO52 | PCM5102A WS/LRCK | Candidate, requires bench verification |
| 7 | GPIO51 | PCM5102A DIN from P4 DOUT | Candidate, requires bench verification |
| 9 | GPIO50 | PCM5102A BCLK | Candidate, requires bench verification |
| 11 | GPIO49 | Optional PCM5102A SCK/MCLK | Not used in first bring-up |

## Rejected DAC pin proposals

| GPIO | Reason |
| --- | --- |
| GPIO22 | Not confirmed on the JC4880 expansion header in current repo docs |
| GPIO23 | Already used by LCD backlight PWM |
| GPIO24 | Not confirmed on the JC4880 expansion header in current repo docs |
| GPIO25 | Not confirmed on the JC4880 expansion header in current repo docs |

## PCM5102A wiring target after bench verification

| PCM5102A signal | P4 GPIO | JP1 pin | Firmware define |
| --- | --- | --- | --- |
| BCK/BCLK | GPIO50 | 9 | `BSP_PCM5102_BCLK_GPIO` |
| LRCK/WS | GPIO52 | 5 | `BSP_PCM5102_WS_GPIO` |
| DIN | GPIO51 | 7 | `BSP_PCM5102_DOUT_GPIO` |
| SCK/MCLK | not connected | not connected | `I2S_GPIO_UNUSED` |
| VIN/VCC | 3.3 V | 1 or 16 | board power |
| GND | GND | 3, 4, or 14 | board ground |

## Bench verification record

Before enabling PCM5102A firmware, verify continuity from JP1 to the DAC module wiring and verify that LCD backlight, touch, SD, USB media, and ES8311 playback still work with the DAC connected but idle.

Record the result in this table:

| Date | Check | Result |
| --- | --- | --- |
| 2026-06-26 | GPIO50/GPIO52/GPIO51 assigned as PCM5102A candidate set | Not bench-verified |
```

- [ ] **Step 2: Link the execution plan from `docs/DEVELOPMENT_PLAN.md`**

Under the Phase 4 audio worklist block, add this bullet after the existing PCM5102A readiness-plan bullet:

```markdown
- Execute
  `docs/superpowers/plans/2026-06-26-pcm5102a-main-out-es8311-monitor.md`
  after GPIO50/GPIO52/GPIO51 are bench-verified on JP1. This plan rejects the
  GPIO22/GPIO23/GPIO24/GPIO25 DAC proposal because GPIO23 is LCD backlight PWM.
```

- [ ] **Step 3: Verify documentation syntax**

Run:

```powershell
git diff --check
git status --short
```

Expected:

```text
M docs/DEVELOPMENT_PLAN.md
A firmware/main-deck-p4/PINOUT_P4.md
```

- [ ] **Step 4: Commit**

```powershell
git add firmware/main-deck-p4/PINOUT_P4.md docs/DEVELOPMENT_PLAN.md
git commit -m "docs(p4): define pcm5102a pinout gate"
```

---

### Task 2: Harden BSP Audio Init and Rename Output Routing to Monitor Routing

**Files:**
- Modify: `firmware/main-deck-p4/components/bsp_jc4880/include/bsp_jc4880.h`
- Modify: `firmware/main-deck-p4/components/bsp_jc4880/bsp_jc4880.c`
- Modify: `firmware/main-deck-p4/main/app_main.c`
- Modify: `firmware/main-deck-p4/components/ui/ui_settings.c`
- Modify: `firmware/main-deck-p4/components/app_settings/include/app_settings.h`
- Modify: `firmware/main-deck-p4/components/app_settings/app_settings.c`

- [ ] **Step 1: Add monitor-route BSP API**

In `bsp_jc4880.h`, keep `bsp_audio_out_t` for compatibility and add this enum/API below it:

```c
typedef enum {
    BSP_MONITOR_ROUTE_HEADPHONES = 0,
    BSP_MONITOR_ROUTE_SPEAKER,
} bsp_monitor_route_t;

esp_err_t bsp_audio_set_monitor_route(bsp_monitor_route_t route);
bsp_monitor_route_t bsp_audio_get_monitor_route(void);
esp_err_t bsp_audio_set_speaker_pa_enabled(bool enabled);
bool bsp_audio_get_speaker_pa_enabled(void);
```

- [ ] **Step 2: Add PA init-once state in `bsp_jc4880.c`**

Near the current audio globals, add:

```c
static bool s_audio_pa_gpio_ready = false;
static bool s_speaker_pa_enabled = false;
static bsp_monitor_route_t s_monitor_route = BSP_MONITOR_ROUTE_SPEAKER;
```

Add this helper near `bsp_audio_get_codec_dev()`:

```c
static esp_err_t bsp_audio_pa_gpio_init_once(void)
{
    if (s_audio_pa_gpio_ready) {
        return ESP_OK;
    }
    gpio_config_t pa_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BSP_AUDIO_PA_GPIO,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa_cfg), TAG, "speaker PA gpio config failed");
    s_audio_pa_gpio_ready = true;
    return ESP_OK;
}
```

- [ ] **Step 3: Make `bsp_audio_init()` idempotent**

At the start of `bsp_audio_init()` after I2C init succeeds, add:

```c
    if (s_codec && s_i2s_tx) {
        ESP_RETURN_ON_ERROR(bsp_audio_pa_gpio_init_once(), TAG, "speaker PA gpio init failed");
        ESP_RETURN_ON_ERROR(bsp_audio_set_monitor_route(s_monitor_route), TAG, "monitor route restore failed");
        return ESP_OK;
    }
```

Replace the local PA `gpio_config_t` block with:

```c
    ESP_RETURN_ON_ERROR(bsp_audio_pa_gpio_init_once(), TAG, "speaker PA gpio init failed");
    ESP_RETURN_ON_ERROR(bsp_audio_set_monitor_route(s_monitor_route), TAG, "monitor route init failed");
```

- [ ] **Step 4: Implement monitor route wrappers**

Replace `bsp_audio_set_output()` with a compatibility wrapper and add the new functions:

```c
esp_err_t bsp_audio_set_speaker_pa_enabled(bool enabled)
{
    ESP_RETURN_ON_ERROR(bsp_audio_pa_gpio_init_once(), TAG, "speaker PA gpio init failed");
    gpio_set_level(BSP_AUDIO_PA_GPIO, enabled ? 1 : 0);
    s_speaker_pa_enabled = enabled;
    ESP_LOGI(TAG, "monitor speaker PA %s", enabled ? "on" : "off");
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
        ESP_RETURN_ON_ERROR(bsp_audio_set_speaker_pa_enabled(false), TAG, "speaker PA off failed");
        s_monitor_route = route;
        return ESP_OK;
    case BSP_MONITOR_ROUTE_SPEAKER:
        ESP_RETURN_ON_ERROR(bsp_audio_set_speaker_pa_enabled(true), TAG, "speaker PA on failed");
        s_monitor_route = route;
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

bsp_monitor_route_t bsp_audio_get_monitor_route(void)
{
    return s_monitor_route;
}

void bsp_audio_set_output(bsp_audio_out_t out)
{
    s_audio_out = out;
    (void)bsp_audio_set_monitor_route(out == BSP_AUDIO_OUT_SPEAKER
                                      ? BSP_MONITOR_ROUTE_SPEAKER
                                      : BSP_MONITOR_ROUTE_HEADPHONES);
}
```

- [ ] **Step 5: Rename settings semantics without changing stored binary layout**

In `app_settings.h`, change the comment for `audio_out` to:

```c
    uint8_t audio_out;      // legacy persisted field: 0 = monitor speaker PA on, 1 = monitor speaker PA off
```

In `ui_settings.c`, change visible labels:

```c
static const char *monitor_route_label(bool monitor_speaker_off)
{
    return monitor_speaker_off ? "HEADPHONES" : "BUILT-IN SPEAKER";
}
```

Use this label where the old code displayed `"RCA LINE-OUT"` or `"SPEAKER"`.

- [ ] **Step 6: Build P4**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: build exits `0`.

- [ ] **Step 7: Commit**

```powershell
git add firmware/main-deck-p4/components/bsp_jc4880 firmware/main-deck-p4/components/app_settings firmware/main-deck-p4/components/ui/ui_settings.c firmware/main-deck-p4/main/app_main.c
git commit -m "refactor(audio): route es8311 as monitor output"
```

---

### Task 3: Add Testable Master/Headphone Mix Result

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c`
- Modify: `tests/audio_output_mixer/test_audio_output_mixer.c`

- [ ] **Step 1: Extend the mixer header**

Add this enum and result type in `audio_output_mixer.h`:

```c
typedef enum {
    AUDIO_OUTPUT_HEADPHONE_MASTER_MONO = 0,
    AUDIO_OUTPUT_HEADPHONE_CUE_MONO,
    AUDIO_OUTPUT_HEADPHONE_SPLIT_MONO,
} audio_output_headphone_mode_t;

typedef struct {
    audio_mixer_frame_t master;
    audio_mixer_frame_t headphone;
} audio_output_mix_result_t;

audio_output_mix_result_t audio_output_mixer_next_full(const audio_output_mixer_deck_t *deck0,
                                                       const audio_output_mixer_deck_t *deck1,
                                                       bool deck0_pfl,
                                                       bool deck1_pfl,
                                                       audio_output_headphone_mode_t headphone_mode,
                                                       uint32_t *out_deck0_consumed,
                                                       uint32_t *out_deck1_consumed,
                                                       audio_mixer_limiter_stats_t *limiter_stats);
```

- [ ] **Step 2: Write host tests before implementation**

Add these tests to `tests/audio_output_mixer/test_audio_output_mixer.c` and call them from `main()`:

```c
static void test_full_mix_keeps_master_stereo_when_cue_is_enabled(void)
{
    audio_mixer_frame_t deck0_frames[] = { { .left = 1000, .right = 3000 }, { .left = 1000, .right = 3000 } };
    audio_mixer_frame_t deck1_frames[] = { { .left = 7000, .right = 9000 }, { .left = 7000, .right = 9000 } };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_output_mixer_deck_t deck1 = make_deck(&deck1_source, &deck1_resampler, 1.0f);

    prime_output_mixer(&deck0, &deck1);
    audio_output_mix_result_t out = audio_output_mixer_next_full(&deck0, &deck1,
                                                                 false, true,
                                                                 AUDIO_OUTPUT_HEADPHONE_CUE_MONO,
                                                                 NULL, NULL, NULL);

    assert(out.master.left == 8000);
    assert(out.master.right == 12000);
    assert(out.headphone.left == 8000);
    assert(out.headphone.right == 8000);
}

static void test_full_mix_split_monitor_uses_master_left_and_pfl_right(void)
{
    audio_mixer_frame_t deck0_frames[] = { { .left = 1000, .right = 3000 }, { .left = 1000, .right = 3000 } };
    audio_mixer_frame_t deck1_frames[] = { { .left = 7000, .right = 9000 }, { .left = 7000, .right = 9000 } };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_output_mixer_deck_t deck1 = make_deck(&deck1_source, &deck1_resampler, 1.0f);

    prime_output_mixer(&deck0, &deck1);
    audio_output_mix_result_t out = audio_output_mixer_next_full(&deck0, &deck1,
                                                                 false, true,
                                                                 AUDIO_OUTPUT_HEADPHONE_SPLIT_MONO,
                                                                 NULL, NULL, NULL);

    assert(out.master.left == 8000);
    assert(out.master.right == 12000);
    assert(out.headphone.left == 10000);
    assert(out.headphone.right == 8000);
}
```

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected before implementation: `audio_output_mixer` build fails because `audio_output_mixer_next_full` is undefined.

- [ ] **Step 3: Implement `audio_output_mixer_next_full()`**

Add this helper and function to `audio_output_mixer.c`:

```c
static int16_t mono_from_frame(audio_mixer_frame_t frame)
{
    return audio_mixer_mix_sample(frame.left, frame.right, 0.5f, 0.5f);
}

audio_output_mix_result_t audio_output_mixer_next_full(const audio_output_mixer_deck_t *deck0,
                                                       const audio_output_mixer_deck_t *deck1,
                                                       bool deck0_pfl,
                                                       bool deck1_pfl,
                                                       audio_output_headphone_mode_t headphone_mode,
                                                       uint32_t *out_deck0_consumed,
                                                       uint32_t *out_deck1_consumed,
                                                       audio_mixer_limiter_stats_t *limiter_stats)
{
    uint32_t consumed0 = 0u;
    uint32_t consumed1 = 0u;
    audio_mixer_frame_t frame0 = next_deck_frame(deck0, &consumed0);
    audio_mixer_frame_t frame1 = next_deck_frame(deck1, &consumed1);

    if (out_deck0_consumed) *out_deck0_consumed = consumed0;
    if (out_deck1_consumed) *out_deck1_consumed = consumed1;

    float gain0 = deck0 ? clamp_gain(deck0->gain) : 0.0f;
    float gain1 = deck1 ? clamp_gain(deck1->gain) : 0.0f;
    int32_t master_left = round_to_i32(((float)frame0.left * gain0) + ((float)frame1.left * gain1));
    int32_t master_right = round_to_i32(((float)frame0.right * gain0) + ((float)frame1.right * gain1));

    audio_mixer_frame_t master = {
        .left = audio_mixer_limit_master_sample(master_left, limiter_stats),
        .right = audio_mixer_limit_master_sample(master_right, limiter_stats),
    };

    float pfl_gain0 = deck0_pfl ? 1.0f : 0.0f;
    float pfl_gain1 = deck1_pfl ? 1.0f : 0.0f;
    audio_mixer_frame_t pfl = {
        .left = audio_mixer_mix_sample(frame0.left, frame1.left, pfl_gain0, pfl_gain1),
        .right = audio_mixer_mix_sample(frame0.right, frame1.right, pfl_gain0, pfl_gain1),
    };

    int16_t master_mono = mono_from_frame(master);
    int16_t pfl_mono = mono_from_frame(pfl);
    audio_mixer_frame_t headphone = { .left = master_mono, .right = master_mono };

    if (headphone_mode == AUDIO_OUTPUT_HEADPHONE_CUE_MONO) {
        headphone.left = pfl_mono;
        headphone.right = pfl_mono;
    } else if (headphone_mode == AUDIO_OUTPUT_HEADPHONE_SPLIT_MONO) {
        headphone.left = master_mono;
        headphone.right = pfl_mono;
    }

    return (audio_output_mix_result_t) {
        .master = master,
        .headphone = headphone,
    };
}
```

- [ ] **Step 4: Re-run host tests**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
```

Expected: all P4 host tests pass.

- [ ] **Step 5: Commit**

```powershell
git add firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c tests/audio_output_mixer/test_audio_output_mixer.c
git commit -m "refactor(audio): split master and monitor mix frames"
```

---

### Task 4: Use Dual Logical Buffers in the Firmware Output Task

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`

- [ ] **Step 1: Add audio engine monitor mode API**

In `audio_engine.h`, add:

```c
typedef enum {
    AUDIO_HEADPHONE_MODE_MASTER_MONO = 0,
    AUDIO_HEADPHONE_MODE_CUE_MONO,
    AUDIO_HEADPHONE_MODE_SPLIT_MONO,
} audio_headphone_mode_t;

esp_err_t audio_engine_set_headphone_mode(audio_headphone_mode_t mode);
audio_headphone_mode_t audio_engine_get_headphone_mode(void);
```

- [ ] **Step 2: Add firmware state**

In `audio_engine.c`, near `s_cue_mode`, add:

```c
static audio_headphone_mode_t s_headphone_mode = AUDIO_HEADPHONE_MODE_SPLIT_MONO;
```

Add this local mapper:

```c
static audio_output_headphone_mode_t output_headphone_mode(void)
{
    if (s_headphone_mode == AUDIO_HEADPHONE_MODE_MASTER_MONO) {
        return AUDIO_OUTPUT_HEADPHONE_MASTER_MONO;
    }
    if (s_headphone_mode == AUDIO_HEADPHONE_MODE_CUE_MONO) {
        return AUDIO_OUTPUT_HEADPHONE_CUE_MONO;
    }
    return AUDIO_OUTPUT_HEADPHONE_SPLIT_MONO;
}
```

- [ ] **Step 3: Replace single `out[]` with `master_out[]` and `hp_out[]`**

In `ae_output_task()`, replace:

```c
    int16_t out[AE_OUT_FRAMES * 2];
```

with:

```c
    int16_t master_out[AE_OUT_FRAMES * 2];
    int16_t hp_out[AE_OUT_FRAMES * 2];
```

Inside the frame loop, replace the manual split-mono block with:

```c
            audio_output_mix_result_t mix = audio_output_mixer_next_full(
                &deck0,
                &deck1,
                s_pfl_enabled[deck0_index],
                s_pfl_enabled[deck1_index],
                output_headphone_mode(),
                &frame_consumed0,
                &frame_consumed1,
                &block_limiter_stats);

            master_out[i * 2] = mix.master.left;
            master_out[i * 2 + 1] = mix.master.right;
            hp_out[i * 2] = mix.headphone.left;
            hp_out[i * 2 + 1] = mix.headphone.right;
```

Remove the direct `audio_resampler_next()` calls from that loop because `audio_output_mixer_next_full()` owns source consumption.

- [ ] **Step 4: Keep ES8311 write behavior stable before PCM5102A is enabled**

Before the write call, choose the ES8311 buffer:

```c
        const int16_t *es8311_out = (s_headphone_mode == AUDIO_HEADPHONE_MODE_MASTER_MONO)
            ? master_out
            : hp_out;
```

Replace the write with:

```c
        if (esp_codec_dev_write(s_codec, es8311_out, (int)(AE_OUT_FRAMES * 2 * sizeof(int16_t))) == ESP_OK) {
```

- [ ] **Step 5: Implement mode API**

Near `audio_engine_set_cue_mode()`, add:

```c
esp_err_t audio_engine_set_headphone_mode(audio_headphone_mode_t mode)
{
    if (mode > AUDIO_HEADPHONE_MODE_SPLIT_MONO) {
        return ESP_ERR_INVALID_ARG;
    }
    s_headphone_mode = mode;
    return ESP_OK;
}

audio_headphone_mode_t audio_engine_get_headphone_mode(void)
{
    return s_headphone_mode;
}
```

Keep `audio_engine_set_cue_mode(uint8_t mode)` as compatibility:

```c
esp_err_t audio_engine_set_cue_mode(uint8_t mode)
{
    if (mode > 1u) return ESP_ERR_INVALID_ARG;
    s_cue_mode = mode;
    s_headphone_mode = mode ? AUDIO_HEADPHONE_MODE_SPLIT_MONO : AUDIO_HEADPHONE_MODE_MASTER_MONO;
    return ESP_OK;
}
```

- [ ] **Step 6: Run host tests and P4 build**

Run:

```powershell
.\tests\run_p4_host_tests.ps1
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: host tests pass and firmware build exits `0`.

- [ ] **Step 7: Commit**

```powershell
git add firmware/main-deck-p4/components/audio_engine
git commit -m "refactor(audio): use separate master and monitor buffers"
```

---

### Task 5: Add PCM5102A I2S1 BSP Support Behind a Compile-Time Gate

**Files:**
- Modify: `firmware/main-deck-p4/components/bsp_jc4880/include/bsp_jc4880.h`
- Modify: `firmware/main-deck-p4/components/bsp_jc4880/bsp_jc4880.c`
- Modify: `docs/HARDWARE_WIRING.md`

- [ ] **Step 1: Add PCM5102A pin defines**

In `bsp_jc4880.c`, add after the ES8311 defines:

```c
#define BSP_PCM5102_I2S_NUM        I2S_NUM_1
#define BSP_PCM5102_BCLK_GPIO      GPIO_NUM_50
#define BSP_PCM5102_WS_GPIO        GPIO_NUM_52
#define BSP_PCM5102_DOUT_GPIO      GPIO_NUM_51
#define BSP_PCM5102_MCLK_GPIO      I2S_GPIO_UNUSED
```

Do not add GPIO22, GPIO23, GPIO24, or GPIO25.

- [ ] **Step 2: Add handles and public getters**

In `bsp_jc4880.c`, add:

```c
static i2s_chan_handle_t s_i2s_tx_pcm5102 = NULL;
```

In `bsp_jc4880.h`, include the I2S type and add getters:

```c
#include "driver/i2s_types.h"

i2s_chan_handle_t bsp_audio_get_main_i2s_tx(void);
esp_err_t bsp_audio_main_i2s_set_sample_rate(uint32_t sample_rate);
```

Implement:

```c
i2s_chan_handle_t bsp_audio_get_main_i2s_tx(void)
{
    return s_i2s_tx_pcm5102;
}
```

- [ ] **Step 3: Add gated PCM5102A init**

Add this function:

```c
static esp_err_t bsp_audio_init_i2s_pcm5102(void)
{
#if CONFIG_BSP_PCM5102A_MAIN_OUT
    if (s_i2s_tx_pcm5102) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_PCM5102_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx_pcm5102, NULL), TAG, "pcm5102 i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
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
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx_pcm5102), TAG, "pcm5102 i2s enable failed");
    ESP_LOGI(TAG, "PCM5102A main out ready: BCLK=%d WS=%d DOUT=%d",
             BSP_PCM5102_BCLK_GPIO, BSP_PCM5102_WS_GPIO, BSP_PCM5102_DOUT_GPIO);
#endif
    return ESP_OK;
}
```

Call it at the end of `bsp_audio_init()`:

```c
    ESP_RETURN_ON_ERROR(bsp_audio_init_i2s_pcm5102(), TAG, "PCM5102A init failed");
```

- [ ] **Step 4: Add sample-rate reconfiguration helper**

Add:

```c
esp_err_t bsp_audio_main_i2s_set_sample_rate(uint32_t sample_rate)
{
#if CONFIG_BSP_PCM5102A_MAIN_OUT
    if (!s_i2s_tx_pcm5102 || sample_rate == 0u) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_i2s_tx_pcm5102), TAG, "pcm5102 disable failed");
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_i2s_tx_pcm5102, &clk_cfg), TAG, "pcm5102 clock reconfig failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx_pcm5102), TAG, "pcm5102 enable failed");
    return ESP_OK;
#else
    (void)sample_rate;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
```

- [ ] **Step 5: Build with default disabled**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: build exits `0`; PCM5102A code compiles as disabled because undefined `CONFIG_BSP_PCM5102A_MAIN_OUT` evaluates to `0`.

- [ ] **Step 6: Commit**

```powershell
git add firmware/main-deck-p4/components/bsp_jc4880 docs/HARDWARE_WIRING.md
git commit -m "feat(bsp): add gated pcm5102a main i2s path"
```

---

### Task 6: Write MAIN OUT to PCM5102A and Monitor to ES8311

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`

- [ ] **Step 1: Add main I2S handle**

Near `s_codec`, add:

```c
static i2s_chan_handle_t s_main_i2s_tx = NULL;
```

Include firmware-only I2S header:

```c
#if !defined(AUDIO_ENGINE_PC_TEST)
#include "driver/i2s_common.h"
#endif
```

- [ ] **Step 2: Capture the handle in `audio_engine_init()`**

After `s_codec = bsp_audio_get_codec_dev();`, add:

```c
    s_main_i2s_tx = bsp_audio_get_main_i2s_tx();
```

- [ ] **Step 3: Add write helper**

Add:

```c
static esp_err_t audio_output_write_main(const int16_t *frames, size_t bytes)
{
#if CONFIG_BSP_PCM5102A_MAIN_OUT
    if (!s_main_i2s_tx) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t written = 0;
    esp_err_t rc = i2s_channel_write(s_main_i2s_tx, frames, bytes, &written, portMAX_DELAY);
    if (rc != ESP_OK || written != bytes) {
        return rc == ESP_OK ? ESP_FAIL : rc;
    }
    return ESP_OK;
#else
    (void)frames;
    (void)bytes;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
```

- [ ] **Step 4: Write both outputs in `ae_output_task()`**

After `master_out[]` and `hp_out[]` are filled, replace the single success condition with:

```c
        esp_err_t main_rc = audio_output_write_main(master_out, AE_OUT_FRAMES * 2 * sizeof(int16_t));
        esp_err_t hp_rc = esp_codec_dev_write(s_codec, hp_out, (int)(AE_OUT_FRAMES * 2 * sizeof(int16_t)));

        if (hp_rc == ESP_OK || main_rc == ESP_OK || main_rc == ESP_ERR_NOT_SUPPORTED) {
```

This keeps current ES8311-only behavior working while PCM5102A is disabled.

- [ ] **Step 5: Build P4**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: build exits `0`.

- [ ] **Step 6: Commit**

```powershell
git add firmware/main-deck-p4/components/audio_engine/audio_engine.c
git commit -m "feat(audio): write main and monitor outputs separately"
```

---

### Task 7: Hardware Bring-Up and Acceptance

**Files:**
- Modify after test: `firmware/main-deck-p4/PINOUT_P4.md`
- Modify after test: `docs/HARDWARE_WIRING.md`
- Modify after test: `docs/STARTUP_CHECKLIST.md`
- Modify after test: `docs/RISK_REGISTER.md`

- [ ] **Step 1: Bench-verify JP1 candidate pins before enabling DAC**

Record in `PINOUT_P4.md`:

```markdown
| 2026-06-26 | GPIO50/GPIO52/GPIO51 continuity to PCM5102A wiring | Pass |
```

If the result is not `Pass`, stop this plan and do not enable `CONFIG_BSP_PCM5102A_MAIN_OUT`.

- [ ] **Step 2: Enable the compile-time gate for local firmware smoke**

Add this line to local `firmware/main-deck-p4/sdkconfig.defaults` only if the project wants the feature enabled by default:

```text
CONFIG_BSP_PCM5102A_MAIN_OUT=y
```

If this setting should remain local for the bench only, use `idf.py menuconfig` or local `sdkconfig` and do not commit `sdkconfig`.

- [ ] **Step 3: Build and flash P4**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
idf.py -p COM15 flash monitor
```

Expected log contains:

```text
PCM5102A main out ready
```

- [ ] **Step 4: Hardware audio checks**

Run this checklist:

```text
1. One deck playing: PCM5102A MAIN OUT has clean stereo audio.
2. One deck playing: ES8311 monitor path has audio.
3. Deck PFL enabled: cue is heard in monitor path.
4. Deck PFL enabled: cue does not change MAIN OUT.
5. Two decks playing: tempo does not slow down.
6. Two decks playing: no persistent underrun, popping, or UI freeze.
7. Settings switch OFF: onboard speaker PA is off.
8. Settings switch ON: onboard speaker PA is on.
9. LCD backlight still dims on GPIO23.
10. Touch, USB media, and DDJ-FLX4 control still respond.
```

- [ ] **Step 5: Update docs with measured result**

Add final wiring to `docs/HARDWARE_WIRING.md`:

```markdown
## PCM5102A MAIN OUT

| PCM5102A | ESP32-P4 JC4880 JP1 |
| --- | --- |
| VIN/VCC | 3.3 V |
| GND | GND |
| BCK | GPIO50 / JP1 pin 9 |
| LRCK/WS | GPIO52 / JP1 pin 5 |
| DIN | GPIO51 / JP1 pin 7 |
| SCK/MCLK | not connected |

PCM5102A is MAIN OUT. ES8311 remains monitor/headphones/onboard speaker.
GPIO22/GPIO23/GPIO24/GPIO25 are not used for this DAC.
```

- [ ] **Step 6: Commit docs and final enablement**

```powershell
git add firmware/main-deck-p4/PINOUT_P4.md docs/HARDWARE_WIRING.md docs/STARTUP_CHECKLIST.md docs/RISK_REGISTER.md firmware/main-deck-p4/sdkconfig.defaults
git commit -m "docs(audio): record pcm5102a hardware smoke"
```

If `sdkconfig.defaults` is not changed, remove it from the `git add` command.

---

## Validation Summary

Run these before claiming the branch is ready:

```powershell
.\tests\run_p4_host_tests.ps1
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
git diff --check
git status --short
```

Expected:

```text
P4 host tests pass.
P4 firmware build exits 0.
git diff --check exits 0.
Only intentional files are modified before each commit.
```

---

## Self-Review

- Spec coverage: includes verified `DAC.md` architecture, GPIO50/GPIO52/GPIO51 candidate path, GPIO22/GPIO23/GPIO24/GPIO25 rejection, dual output buffers, monitor/speaker UI route, BSP hardening, and hardware smoke.
- Substitute-pin scan: the plan uses fixed candidate pins and explicit stop conditions; it does not contain fake GPIO names.
- Type consistency: monitor BSP types are `bsp_monitor_route_t`; engine monitor types are `audio_headphone_mode_t`; pure mixer monitor types are `audio_output_headphone_mode_t`.
- Risk handling: I2S clock sync is explicitly deferred until bench evidence shows it is needed.
