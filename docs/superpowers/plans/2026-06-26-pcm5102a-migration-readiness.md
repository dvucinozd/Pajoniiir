# PCM5102A Migration Readiness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prepare the ESP32-P4 audio architecture for a PCM5102A stereo MAIN OUT while keeping ES8311 as a monitor/headphone/speaker path.

**Architecture:** P4 remains the authoritative audio engine. Before adding a second DAC, the existing single-output ES8311 path must be cleaned into two logical output buffers: `master_out[]` and `hp_out[]`. PCM5102A hardware work is gated by a P4 pinout inventory; S3 pinout data must not be used for P4 audio wiring.

**Tech Stack:** ESP-IDF v5.5, ESP32-P4 JC4880P443C_I_W, ESP32-S3 DDJ-FLX4 USB MIDI host, C, FreeRTOS, I2S STD driver, `esp_codec_dev`, host GCC tests.

---

## Review Findings Validation Matrix

| Finding | Status | Evidence in current `master` | Action |
| --- | --- | --- | --- |
| Do not use S3 pinout for PCM5102A | Confirmed | Only `firmware/control-board-s3/PINOUT.md` exists; P4 BSP uses GPIO23 for LCD backlight, GPIO9/10/12/13/48 for ES8311 I2S, GPIO39-44 for SDMMC | Create P4 pinout inventory before wiring |
| GPIO23 conflicts with LCD backlight | Confirmed | `BSP_LCD_BL_GPIO GPIO_NUM_23` in `firmware/main-deck-p4/components/bsp_jc4880/bsp_jc4880.c` | Never use GPIO23 for PCM5102A |
| Blocking codec write plus manual delay slows playback | Mostly outdated | `audio_output_remaining_delay_ms()` currently returns `0`, so no extra real delay is applied after `esp_codec_dev_write()` | Remove dead timing API or keep only diagnostics |
| Possible duplicate output task | Dormant risk | `task_plan.start_output` still exists, but `audio_fw_task_plan_for_deck()` always returns `.start_output = false`; shared output starts via `audio_output_service_ensure_started()` | Remove dormant per-deck output task fields |
| Codec open ignores sample-rate mismatch | Confirmed | `audio_output_service_open_codec(sample_rate)` returns `ESP_OK` if `s_output_codec_open` is true without checking `sample_rate` | Use fixed output sample rate and resample decks into it |
| Global `s_tasks_done` can consume another deck's exit signal | Confirmed risk | `s_tasks_done` is global, while loader/decode tasks are per deck | Move task completion into deck-local runtime |
| Full-file MP3 preload consumes PSRAM | Confirmed | Loader allocates `heap_caps_malloc((size_t)fsz, MALLOC_CAP_SPIRAM)` | Add max track size before PCM5102A work |
| Decode stack in PSRAM may be oversized/slow | Confirmed as optimization target | Decode task stack is `49152` with `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` | Add high-watermark logging, then reduce only with evidence |
| Output stack may be too small for dual output | Confirmed future risk | Output task stack is `4096`; current stack buffer is one `out[AE_OUT_FRAMES * 2]` | Use static/internal output buffers or raise stack |
| Cue/PFL modifies physical output buffer | Confirmed for future dual-DAC architecture | Split mono writes PFL into the same `out[]` sent to ES8311 | Split logical master and headphone buffers before PCM5102A |
| No clipping protection | Outdated | `audio_mixer_limit_master_sample()` and limiter tests exist | Monitor limiter telemetry; do not add blind -6 dB attenuation yet |
| `bsp_audio_init()` not idempotent | Confirmed | No guard around `s_codec`/`s_i2s_tx` before `i2s_new_channel()` | Add init-once guard |
| `bsp_audio_set_output()` assumes PA GPIO configured | Confirmed API risk | Direct `gpio_set_level(BSP_AUDIO_PA_GPIO, ...)` | Add PA GPIO init-once helper |
| `bsp_sd_init()` hides mount failure | Confirmed and intentional but unclear | Returns `ESP_OK` on SD power/mount failure because USB path continues | Rename semantics in docs/API or require `bsp_sd_is_mounted()` |
| `bsp_display_init()` uses `ESP_ERROR_CHECK()` despite returning `esp_err_t` | Confirmed | Multiple `ESP_ERROR_CHECK()` calls in `bsp_display_init()` | Convert to `ESP_RETURN_ON_ERROR()` |
| UART checksum is weak | Confirmed but not blocking | 7-byte `0xA5` protocol uses XOR | Keep for MVP; CRC8 belongs to protocol v2 |
| 115200 baud can bottleneck | Plausible, not proven | UART is 115200, but high-rate events are coalesced | Measure before changing baud |
| Internal pull-ups/button noise | Legacy-only | Applies to S3 legacy panel path, not DDJ-FLX4 USB host mode | No PCM5102A blocker |
| Pitch fader lacks filtering | Partially outdated, legacy-only | Legacy panel reads every 10 ms and applies threshold `8`; FLX4 tempo is USB MIDI | No PCM5102A blocker |

---

## Implementation Tasks

### Task 1: Create P4 Pinout Inventory Gate

**Files:**
- Create: `firmware/main-deck-p4/PINOUT_P4.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`

- [ ] **Step 1: Create `PINOUT_P4.md` with occupied pins**

Create the file with this content:

```markdown
# ESP32-P4 Pinout Inventory for JC4880P443C_I_W

This document is the source of truth for P4-side expansion work.
Do not use `firmware/control-board-s3/PINOUT.md` for P4 peripherals.

## Occupied pins from current firmware

| GPIO | Owner | File | Notes |
| --- | --- | --- | --- |
| GPIO5 | LCD reset | `components/bsp_jc4880/bsp_jc4880.c` | `BSP_LCD_RST_GPIO` |
| GPIO23 | LCD backlight PWM | `components/bsp_jc4880/bsp_jc4880.c` | `BSP_LCD_BL_GPIO`; unavailable for PCM5102A |
| GPIO7 | I2C SDA | `components/bsp_jc4880/bsp_jc4880.c` | Shared GT911 + ES8311 |
| GPIO8 | I2C SCL | `components/bsp_jc4880/bsp_jc4880.c` | Shared GT911 + ES8311 |
| GPIO13 | ES8311 I2S MCLK | `components/bsp_jc4880/bsp_jc4880.c` | `BSP_I2S_MCLK_GPIO` |
| GPIO12 | ES8311 I2S BCLK | `components/bsp_jc4880/bsp_jc4880.c` | `BSP_I2S_BCLK_GPIO` |
| GPIO10 | ES8311 I2S WS/LRCK | `components/bsp_jc4880/bsp_jc4880.c` | `BSP_I2S_WS_GPIO` |
| GPIO9 | ES8311 I2S DOUT | `components/bsp_jc4880/bsp_jc4880.c` | ESP to codec DAC |
| GPIO48 | ES8311 I2S DIN | `components/bsp_jc4880/bsp_jc4880.c` | codec ADC to ESP; unused for playback |
| GPIO11 | Speaker PA enable | `components/bsp_jc4880/bsp_jc4880.c` | Do not reuse while ES8311 monitor speaker route exists |
| GPIO39 | SDMMC D0 | `components/bsp_jc4880/bsp_jc4880.c` | `/sd` optional storage |
| GPIO40 | SDMMC D1 | `components/bsp_jc4880/bsp_jc4880.c` | `/sd` optional storage |
| GPIO41 | SDMMC D2 | `components/bsp_jc4880/bsp_jc4880.c` | `/sd` optional storage |
| GPIO42 | SDMMC D3 | `components/bsp_jc4880/bsp_jc4880.c` | `/sd` optional storage |
| GPIO43 | SDMMC CLK | `components/bsp_jc4880/bsp_jc4880.c` | `/sd` optional storage |
| GPIO44 | SDMMC CMD | `components/bsp_jc4880/bsp_jc4880.c` | `/sd` optional storage |
| GPIO28 | Control link UART RX | `components/control_link/control_link_uart.c` | S3 TX to P4 RX |
| GPIO29 | Control link UART TX | `components/control_link/control_link_uart.c` | P4 TX to S3 RX |

## PCM5102A candidate selection gate

PCM5102A pins are not selected in firmware until these checks are complete:

1. Confirm candidate GPIOs against ESP32-P4 strapping/reserved pin rules.
2. Confirm candidate GPIOs are physically accessible on the JC4880P443C_I_W board.
3. Confirm candidate GPIOs do not conflict with display, SDMMC, I2C, ES8311, UART, USB, or PSRAM wiring.
4. Record the selected pins in this document before editing BSP code.

## Explicitly forbidden for PCM5102A

- GPIO23: LCD backlight PWM.
- GPIO5: LCD reset.
- GPIO7/GPIO8: shared I2C.
- GPIO9/GPIO10/GPIO12/GPIO13/GPIO48: current ES8311 I2S0 path.
- GPIO11: speaker PA enable.
- GPIO28/GPIO29: S3/P4 control link UART.
- GPIO39/GPIO40/GPIO41/GPIO42/GPIO43/GPIO44: SDMMC.
```

- [ ] **Step 2: Link the pinout gate from `docs/DEVELOPMENT_PLAN.md`**

Add this sentence under the PCM5102A/audio TODO section:

```markdown
PCM5102A bring-up is gated by `firmware/main-deck-p4/PINOUT_P4.md`; do not use the S3 pinout for P4 audio wiring.
```

- [ ] **Step 3: Verify documentation**

Run:

```powershell
git diff --check
```

Expected: exit code `0`.

- [ ] **Step 4: Commit**

```powershell
git add firmware/main-deck-p4/PINOUT_P4.md docs/DEVELOPMENT_PLAN.md
git commit -m "docs(p4): add pcm5102a pinout gate"
```

---

### Task 2: Remove Dormant Per-Deck Output Task Path

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_fw_task_plan.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_fw_task_plan.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_fw_runtime.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_fw_runtime.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify tests in `tests/audio_fw_task_plan/`, `tests/audio_fw_runtime/`, and `tests/audio_fw_task_context/`

- [ ] **Step 1: Write failing tests**

Remove expectations around `start_output`, `output_task`, and `codec_owner` from host tests. Add this assertion to the task-plan test:

```c
audio_fw_task_plan_t plan =
    audio_fw_task_plan_for_deck(1, 0, true);
assert(plan.start_loader);
assert(plan.start_decode);
assert(plan.transport_supported);
assert(plan.expected_tasks == 2);
```

Expected failure before implementation: compiler errors because the old fields still exist in production/tests or stale assertions still reference them.

- [ ] **Step 2: Simplify task-plan type**

Change `audio_fw_task_plan_t` to:

```c
typedef struct {
    bool start_loader;
    bool start_decode;
    bool transport_supported;
    int expected_tasks;
} audio_fw_task_plan_t;
```

Change `audio_fw_task_plan_for_deck()` return value to:

```c
return (audio_fw_task_plan_t) {
    .start_loader = true,
    .start_decode = true,
    .transport_supported = true,
    .expected_tasks = 2,
};
```

- [ ] **Step 3: Simplify runtime type**

Change `audio_fw_runtime_t` to:

```c
typedef struct {
    void *loader_task;
    void *decode_task;
    volatile bool run;
    int tasks_started;
} audio_fw_runtime_t;
```

Remove all assignments to `runtime->output_task` and `runtime->codec_open`.

- [ ] **Step 4: Remove per-deck output creation block**

Delete this branch from `audio_engine_load_for_deck()`:

```c
if (task_plan.start_output) {
    if (xTaskCreate(ae_output_task, "ae_output", 4096, task_ctx, 6,
                    (TaskHandle_t *)&runtime->output_task) == pdPASS) {
        audio_fw_runtime_mark_task_started(runtime);
    } else {
        ESP_LOGE(TAG, "failed to create ae_output task");
    }
}
```

Keep this call:

```c
esp_err_t output_rc = audio_output_service_ensure_started();
```

- [ ] **Step 5: Run host tests**

```powershell
$env:PATH='C:\msys64\ucrt64\bin;' + $env:PATH
.\tests\run_p4_host_tests.ps1
```

Expected: `P4 host tests passed.`

- [ ] **Step 6: Commit**

```powershell
git add firmware/main-deck-p4/components/audio_engine tests
git commit -m "refactor(audio): remove dormant per-deck output task path"
```

---

### Task 3: Fix Output Sample-Rate Strategy

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_resampler.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_resampler.c`
- Modify: `tests/audio_resampler/test_audio_resampler.c`
- Modify: `tests/audio_output_mixer/test_audio_output_mixer.c` if mixer API is touched

- [ ] **Step 1: Add failing resampler step test**

Add to `tests/audio_resampler/test_audio_resampler.c`:

```c
static void test_sample_rate_ratio_is_applied_to_pitch_step(void)
{
    double step = audio_resampler_step(48000u, 44100u, 1.0f);
    assert(step > 1.0884);
    assert(step < 1.0885);

    step = audio_resampler_step(44100u, 44100u, 0.5f);
    assert(step > 0.4999);
    assert(step < 0.5001);
}
```

Expected RED: compiler error because `audio_resampler_step()` does not exist.

- [ ] **Step 2: Add resampler step helper**

Add to `audio_resampler.h`:

```c
double audio_resampler_step(uint32_t source_rate,
                            uint32_t output_rate,
                            float pitch_factor);
```

Add to `audio_resampler.c`:

```c
double audio_resampler_step(uint32_t source_rate,
                            uint32_t output_rate,
                            float pitch_factor)
{
    if (source_rate == 0u || output_rate == 0u) {
        return (double)pitch_factor;
    }
    return ((double)source_rate / (double)output_rate) * (double)pitch_factor;
}
```

- [ ] **Step 3: Fix firmware output rate**

At the top of the firmware output section in `audio_engine.c`, add:

```c
#define AUDIO_OUTPUT_SAMPLE_RATE_HZ 44100u
```

Change `audio_output_service_open_codec()` so it opens only at `AUDIO_OUTPUT_SAMPLE_RATE_HZ` and ignores per-track sample rate for codec configuration:

```c
static esp_err_t audio_output_service_open_codec(uint32_t sample_rate)
{
    (void)sample_rate;

    AE_LOCK();
    if (s_output_codec_open) {
        AE_UNLOCK();
        return ESP_OK;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = 2,
        .sample_rate     = AUDIO_OUTPUT_SAMPLE_RATE_HZ,
    };
    if (esp_codec_dev_open(s_codec, &fs) != 0) {
        AE_UNLOCK();
        return ESP_FAIL;
    }
    s_output_codec_open = true;
    s_output_sample_rate = AUDIO_OUTPUT_SAMPLE_RATE_HZ;
    ESP_LOGI(TAG, "shared codec open @ %u Hz", (unsigned)AUDIO_OUTPUT_SAMPLE_RATE_HZ);
    AE_UNLOCK();
    return ESP_OK;
}
```

- [ ] **Step 4: Apply source/output rate in output task**

Change resampler calls from:

```c
frame0 = audio_resampler_next(deck0.resampler,
                              deck0.pitch_factor,
                              deck0.pop_source,
                              deck0.source_ctx,
                              &frame_consumed0);
```

to:

```c
double deck0_step = audio_resampler_step(s_engines[deck0_index].sample_rate,
                                         s_output_sample_rate,
                                         deck0.pitch_factor);
frame0 = audio_resampler_next(deck0.resampler,
                              (float)deck0_step,
                              deck0.pop_source,
                              deck0.source_ctx,
                              &frame_consumed0);
```

Apply the same pattern for Deck 2.

- [ ] **Step 5: Run tests and build**

```powershell
$env:PATH='C:\msys64\ucrt64\bin;' + $env:PATH
.\tests\run_p4_host_tests.ps1
$env:IDF_PATH='C:\Espressif\frameworks\esp-idf-v5.5\'
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: host tests pass and firmware build exits `0`.

- [ ] **Step 6: Commit**

```powershell
git add firmware/main-deck-p4/components/audio_engine tests/audio_resampler
git commit -m "fix(audio): use fixed output sample rate"
```

---

### Task 4: Make Audio Task Completion Deck-Local

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_fw_runtime.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_fw_runtime.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify: `tests/audio_fw_runtime/test_audio_fw_runtime.c`

- [ ] **Step 1: Add runtime state test**

Add to runtime tests:

```c
static void test_runtime_tracks_its_own_task_count(void)
{
    audio_fw_runtime_t runtime;
    audio_fw_runtime_reset(&runtime);
    audio_fw_runtime_begin_load(&runtime);
    audio_fw_runtime_mark_task_started(&runtime);
    audio_fw_runtime_mark_task_started(&runtime);
    assert(runtime.tasks_started == 2);
}
```

- [ ] **Step 2: Add per-runtime completion handle**

For firmware builds, extend `audio_fw_runtime_t`:

```c
void *tasks_done;
```

In `audio_engine.c`, initialize each runtime's `tasks_done` as a counting semaphore in `audio_engine_init()`:

```c
for (uint8_t deck = 0; deck < AUDIO_ENGINE_DECK_COUNT; deck++) {
    if (!s_fw_runtimes[deck].tasks_done) {
        s_fw_runtimes[deck].tasks_done =
            xSemaphoreCreateCounting(2, 0);
        if (!s_fw_runtimes[deck].tasks_done) {
            return ESP_ERR_NO_MEM;
        }
    }
}
```

- [ ] **Step 3: Replace global task completion gives**

In loader/decode tasks, replace:

```c
xSemaphoreGive(s_tasks_done);
```

with:

```c
xSemaphoreGive((SemaphoreHandle_t)runtime->tasks_done);
```

Only do this after confirming `runtime` is available in that task branch. For invalid unbound task branches, keep a local safe exit path that does not signal another deck.

- [ ] **Step 4: Replace stop waits**

In `audio_engine_stop_for_deck()`, replace global waits with:

```c
SemaphoreHandle_t done = (SemaphoreHandle_t)runtime->tasks_done;
for (int i = 0; i < runtime->tasks_started; i++) {
    if (xSemaphoreTake(done, pdMS_TO_TICKS(1500)) == pdTRUE) {
        exited++;
    }
}
```

- [ ] **Step 5: Run P4 host tests and build**

```powershell
$env:PATH='C:\msys64\ucrt64\bin;' + $env:PATH
.\tests\run_p4_host_tests.ps1
$env:IDF_PATH='C:\Espressif\frameworks\esp-idf-v5.5\'
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: host tests pass and firmware build exits `0`.

- [ ] **Step 6: Commit**

```powershell
git add firmware/main-deck-p4/components/audio_engine tests/audio_fw_runtime
git commit -m "fix(audio): use deck-local task completion"
```

---

### Task 5: Split Logical Master and Headphone Buffers

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify: `tests/audio_output_mixer/test_audio_output_mixer.c`

- [ ] **Step 1: Add failing mixer result test**

Add a test that asserts master remains stereo while headphone split mode can differ:

```c
static void test_full_mix_keeps_master_independent_from_pfl(void)
{
    audio_mixer_frame_t deck0_frames[] = {
        { .left = 1000, .right = 2000 },
        { .left = 1000, .right = 2000 },
    };
    audio_mixer_frame_t deck1_frames[] = {
        { .left = 3000, .right = 4000 },
        { .left = 3000, .right = 4000 },
    };
    source_t deck0_source = { .frames = deck0_frames, .count = 2, .index = 0 };
    source_t deck1_source = { .frames = deck1_frames, .count = 2, .index = 0 };
    audio_resampler_state_t deck0_resampler;
    audio_resampler_state_t deck1_resampler;
    audio_output_mixer_deck_t deck0 = make_deck(&deck0_source, &deck0_resampler, 1.0f);
    audio_output_mixer_deck_t deck1 = make_deck(&deck1_source, &deck1_resampler, 1.0f);

    prime_output_mixer(&deck0, &deck1);
    audio_output_mix_result_t result =
        audio_output_mixer_next_full(&deck0, &deck1, 1.0f, 0.0f, NULL);

    assert(result.master.left == 4000);
    assert(result.master.right == 6000);
    assert(result.pfl.left == 1000);
    assert(result.pfl.right == 2000);
}
```

Expected RED: `audio_output_mix_result_t` and `audio_output_mixer_next_full()` do not exist.

- [ ] **Step 2: Add full mix result type**

Add to `audio_output_mixer.h`:

```c
typedef struct {
    audio_mixer_frame_t master;
    audio_mixer_frame_t pfl;
    uint32_t consumed0;
    uint32_t consumed1;
} audio_output_mix_result_t;

audio_output_mix_result_t audio_output_mixer_next_full(
    const audio_output_mixer_deck_t *deck0,
    const audio_output_mixer_deck_t *deck1,
    float pfl_gain0,
    float pfl_gain1,
    audio_mixer_limiter_stats_t *limiter_stats);
```

- [ ] **Step 3: Implement full mixer**

Implement `audio_output_mixer_next_full()` using the existing `next_deck_frame()` helper:

```c
audio_output_mix_result_t audio_output_mixer_next_full(
    const audio_output_mixer_deck_t *deck0,
    const audio_output_mixer_deck_t *deck1,
    float pfl_gain0,
    float pfl_gain1,
    audio_mixer_limiter_stats_t *limiter_stats)
{
    uint32_t consumed0 = 0u;
    uint32_t consumed1 = 0u;
    audio_mixer_frame_t frame0 = next_deck_frame(deck0, &consumed0);
    audio_mixer_frame_t frame1 = next_deck_frame(deck1, &consumed1);
    float gain0 = deck0 ? clamp_gain(deck0->gain) : 0.0f;
    float gain1 = deck1 ? clamp_gain(deck1->gain) : 0.0f;

    int32_t master_left = round_to_i32(((float)frame0.left * gain0) +
                                       ((float)frame1.left * gain1));
    int32_t master_right = round_to_i32(((float)frame0.right * gain0) +
                                        ((float)frame1.right * gain1));

    return (audio_output_mix_result_t) {
        .master = {
            .left = audio_mixer_limit_master_sample(master_left, limiter_stats),
            .right = audio_mixer_limit_master_sample(master_right, limiter_stats),
        },
        .pfl = {
            .left = audio_mixer_mix_sample(frame0.left, frame1.left, pfl_gain0, pfl_gain1),
            .right = audio_mixer_mix_sample(frame0.right, frame1.right, pfl_gain0, pfl_gain1),
        },
        .consumed0 = consumed0,
        .consumed1 = consumed1,
    };
}
```

- [ ] **Step 4: Use two logical buffers in output task**

In `ae_output_task`, replace:

```c
int16_t out[AE_OUT_FRAMES * 2];
```

with:

```c
static int16_t s_master_out[AE_OUT_FRAMES * 2];
static int16_t s_hp_out[AE_OUT_FRAMES * 2];
```

Use `s_master_out` for main output content. While there is only ES8311 hardware, write either `s_master_out` or `s_hp_out` according to the current monitor mode. Do not let PFL alter `s_master_out`.

- [ ] **Step 5: Run tests**

```powershell
$env:PATH='C:\msys64\ucrt64\bin;' + $env:PATH
.\tests\run_p4_host_tests.ps1
```

Expected: `audio_output_mixer tests passed` and `P4 host tests passed.`

- [ ] **Step 6: Commit**

```powershell
git add firmware/main-deck-p4/components/audio_engine tests/audio_output_mixer
git commit -m "refactor(audio): split master and monitor mix buffers"
```

---

### Task 6: Harden BSP Audio Init Before Adding PCM5102A

**Files:**
- Modify: `firmware/main-deck-p4/components/bsp_jc4880/bsp_jc4880.c`
- Modify: `firmware/main-deck-p4/components/bsp_jc4880/include/bsp_jc4880.h`

- [ ] **Step 1: Add idempotent guard**

At the start of `bsp_audio_init()` add:

```c
if (s_codec && s_i2s_tx) {
    return ESP_OK;
}
```

- [ ] **Step 2: Add PA init-once helper**

Add static state:

```c
static bool s_audio_pa_ready;
```

Add helper:

```c
static esp_err_t bsp_audio_pa_gpio_init_once(void)
{
    if (s_audio_pa_ready) {
        return ESP_OK;
    }

    gpio_config_t pa_cfg = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BSP_AUDIO_PA_GPIO,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa_cfg), TAG, "PA GPIO init failed");
    s_audio_pa_ready = true;
    return ESP_OK;
}
```

Use it from both `bsp_audio_init()` and `bsp_audio_set_output()`.

- [ ] **Step 3: Make `bsp_audio_set_output()` defensive**

Change it to:

```c
void bsp_audio_set_output(bsp_audio_out_t out)
{
    s_audio_out = out;
    if (bsp_audio_pa_gpio_init_once() != ESP_OK) {
        ESP_LOGW(TAG, "audio PA GPIO not ready");
        return;
    }
    gpio_set_level(BSP_AUDIO_PA_GPIO, out == BSP_AUDIO_OUT_SPEAKER ? 1 : 0);
    ESP_LOGI(TAG, "audio output → %s",
             out == BSP_AUDIO_OUT_SPEAKER ? "speaker (PA on)" : "RCA line-out (PA off)");
}
```

- [ ] **Step 4: Build P4**

```powershell
$env:IDF_PATH='C:\Espressif\frameworks\esp-idf-v5.5\'
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: build exits `0`.

- [ ] **Step 5: Commit**

```powershell
git add firmware/main-deck-p4/components/bsp_jc4880
git commit -m "fix(bsp): make p4 audio init idempotent"
```

---

### Task 7: Add Track Size Guard and Memory Budget Logging

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Modify: `docs/RISK_REGISTER.md`

- [ ] **Step 1: Add maximum track size**

Near the firmware preload section, add:

```c
#define AUDIO_MAX_TRACK_BYTES (40u * 1024u * 1024u)
```

In `ae_loader_task()`, after `fsz <= 0` validation, add:

```c
if ((uint64_t)fsz > AUDIO_MAX_TRACK_BYTES) {
    ESP_LOGE(TAG, "track too large: %ld B > %u B", fsz, AUDIO_MAX_TRACK_BYTES);
    fclose(src);
    media_io_gate_end();
    eng->last_error = ESP_ERR_NO_MEM;
    snprintf(eng->last_error_text, sizeof(eng->last_error_text), "TRACK TOO LARGE");
    goto park;
}
```

- [ ] **Step 2: Update risk register**

Add:

```markdown
| Full-file MP3 preload exceeds PSRAM budget | Load failure or heap fragmentation with long tracks | Firmware rejects tracks over 40 MB until chunked compressed-cache playback is implemented |
```

- [ ] **Step 3: Run P4 host tests and build**

```powershell
$env:PATH='C:\msys64\ucrt64\bin;' + $env:PATH
.\tests\run_p4_host_tests.ps1
$env:IDF_PATH='C:\Espressif\frameworks\esp-idf-v5.5\'
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: host tests pass and firmware build exits `0`.

- [ ] **Step 4: Commit**

```powershell
git add firmware/main-deck-p4/components/audio_engine/audio_engine.c docs/RISK_REGISTER.md
git commit -m "fix(audio): guard oversized preloads"
```

---

### Task 8: PCM5102A Bring-Up Gate

**Files:**
- Modify only after Task 1 has selected approved P4 GPIOs:
  - `firmware/main-deck-p4/PINOUT_P4.md`
  - `firmware/main-deck-p4/components/bsp_jc4880/bsp_jc4880.c`
  - `firmware/main-deck-p4/components/bsp_jc4880/include/bsp_jc4880.h`

- [ ] **Step 1: Stop for pin approval**

Before adding PCM5102A code, write the selected pins into `PINOUT_P4.md` only after physical board/pin confirmation. Use this exact section shape, with real GPIO numbers or the literal value `not used` for optional MCLK:

```markdown
## Approved PCM5102A pins

| Signal | GPIO | Approval note |
| --- | --- | --- |
| BCLK | real P4 GPIO number | Confirmed free on P4 board and not a boot strap conflict |
| LRCK/WS | real P4 GPIO number | Confirmed free on P4 board and not a boot strap conflict |
| DIN | real P4 GPIO number | Confirmed free on P4 board and not a boot strap conflict |
| Optional MCLK | real P4 GPIO number or not used | PCM5102A can operate without MCLK if BCLK/LRCK are stable |
```

Do not add PCM5102A firmware until the section contains real approved P4 pin choices for BCLK, LRCK/WS, and DIN.

- [ ] **Step 2: Add BSP handles**

After pins are approved, add separate I2S handles:

```c
static i2s_chan_handle_t s_i2s_tx_es8311 = NULL;
static i2s_chan_handle_t s_i2s_tx_pcm5102 = NULL;
```

Rename the current `s_i2s_tx` use to `s_i2s_tx_es8311`.

- [ ] **Step 3: Add explicit output APIs**

In `bsp_jc4880.h`, introduce separate routing concepts:

```c
typedef enum {
    BSP_MAIN_OUT_PCM5102A = 0,
} bsp_main_out_t;

typedef enum {
    BSP_MONITOR_ROUTE_HEADPHONES = 0,
    BSP_MONITOR_ROUTE_SPEAKER,
} bsp_monitor_route_t;
```

Keep `bsp_audio_out_t` only as a compatibility wrapper until Settings UI is migrated.

- [ ] **Step 4: Hardware test PCM5102A with generated tone**

Add a temporary bring-up function guarded by a compile-time flag:

```c
#if CONFIG_DDJ_FFL4_PCM5102A_TONE_TEST
esp_err_t bsp_pcm5102a_write_test_tone(void);
#endif
```

The test tone must not be compiled into normal firmware unless the config flag is enabled.

- [ ] **Step 5: Build and flash only after tone path compiles**

```powershell
$env:IDF_PATH='C:\Espressif\frameworks\esp-idf-v5.5\'
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
idf.py -p COM15 flash
```

Expected: build and flash exit `0`; hardware tone is heard only on PCM5102A MAIN OUT.

- [ ] **Step 6: Commit**

```powershell
git add firmware/main-deck-p4/PINOUT_P4.md firmware/main-deck-p4/components/bsp_jc4880
git commit -m "feat(bsp): add gated pcm5102a bring-up"
```

---

## Recommended Execution Order

1. Task 1: P4 pinout gate.
2. Task 2: Remove dormant output task path.
3. Task 3: Fixed output sample rate.
4. Task 4: Deck-local task completion.
5. Task 5: Logical `master_out[]` / `hp_out[]` split.
6. Task 6: BSP audio init hardening.
7. Task 7: Track size guard and memory budget.
8. Task 8: PCM5102A bring-up only after pin approval.

Do not start Task 8 before Tasks 1, 3, 5, and 6 are complete.

## Validation Checklist Before PCM5102A Soldering

- [ ] `firmware/main-deck-p4/PINOUT_P4.md` exists.
- [ ] GPIO23 is explicitly marked forbidden for PCM5102A.
- [ ] Current ES8311 pins remain reserved.
- [ ] Fixed output sample rate is implemented.
- [ ] `master_out[]` does not contain cue/PFL signal.
- [ ] `hp_out[]` contains monitor/cue signal.
- [ ] P4 firmware builds with `idf.py build`.
- [ ] Two-deck audio smoke passes on existing ES8311 path.
- [ ] User approves final PCM5102A BCLK/LRCK/DIN pins before wiring.

## Self-Review

- Spec coverage: covers all verified PCM5102A-relevant review findings: P4 pinout, output timing cleanup, sample-rate strategy, task lifecycle, logical output split, BSP idempotency, preload memory guard, and PCM5102A gate.
- Placeholder scan: the only symbolic GPIO names are intentionally confined to the approval gate in Task 8; the plan explicitly forbids proceeding while those symbols remain.
- Type consistency: proposed types are `audio_output_mix_result_t`, `bsp_main_out_t`, and `bsp_monitor_route_t`; these names are used consistently across tasks.
