# Trim/Pregain DSP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement deck-local FLX4 Trim knobs as P4-owned pregain DSP controls instead of deferred/log-only mixer values.

**Architecture:** Keep S3 mapping unchanged: FLX4 Trim already arrives as `CTRL_ID_CH1_TRIM` / `CTRL_ID_CH2_TRIM`. Add a small P4 audio-engine pregain state parallel to channel volume, apply it in `audio_engine_get_output_gains()`, expose it in the mixer snapshot/status path, and route deck_core Trim events to the new API. Use a conservative gain curve so center is unity, left attenuates, and right allows limited boost while existing limiter telemetry remains the safety net.

**Tech Stack:** ESP-IDF C, existing `audio_engine`, `deck_core`, host tests in `tests/run_p4_host_tests.ps1`, P4 build via `idf.py build`.

---

## File map

- Modify `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
  - Add `pregain` raw/effective fields to `audio_engine_mixer_snapshot_t`.
  - Add `audio_engine_set_pregain()` / `audio_engine_get_pregain()` declarations.
- Modify `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
  - Store per-deck trim raw values.
  - Convert raw 14-bit FLX4 trim to a linear pregain scalar.
  - Multiply output gain by pregain.
  - Reset pregain on `audio_engine_init()`.
  - Include pregain in mixer snapshot.
- Modify `firmware/main-deck-p4/components/deck_core/deck_core.c`
  - Remove CH1/CH2 Trim from deferred mixer controls.
  - Route Trim events to `audio_engine_set_pregain(deck, raw)`.
- Modify `tests/audio_engine/test_audio_engine.c`
  - Add RED tests for default pregain, center unity, left attenuation, right boost clamp, snapshot fields, invalid deck guard.
- Modify `tests/deck_core_dual/stubs/audio_engine.h`
  - Add stub variables/function for `audio_engine_set_pregain()`.
- Modify `tests/deck_core_dual/test_deck_core_dual.c`
  - Add RED test that CH1/CH2 Trim events call `audio_engine_set_pregain()`.
  - Adjust deferred mixer logging test so Trim is no longer deferred; Headphones Mix remains deferred.
- Modify docs:
  - `README.md`
  - `docs/DDJ_FLX4_MIDI_MAP.md`
  - `docs/DEVELOPMENT_PLAN.md`
  - `docs/STARTUP_CHECKLIST.md`

---

## Gain curve decision

Use this initial curve:

- Raw `0` -> `0.25f` pregain (-12 dB).
- Raw `8192` / center -> `1.0f` pregain.
- Raw `16383` -> `2.0f` pregain (+6 dB).
- Clamp raw above `AUDIO_MIXER_CONTROL_MAX`.
- Invalid deck returns `ESP_ERR_INVALID_ARG`.

Rationale: this matches common DJ trim expectations better than a non-boosting control, but keeps boost bounded. Since master trim and post-sum limiter already exist, sustained limiter activity can be handled operationally by master trim while preserving usable trim behavior.

Implementation helper sketch:

```c
static float audio_engine_pregain_from_raw(uint16_t raw)
{
    if (raw > AUDIO_MIXER_CONTROL_MAX) {
        raw = AUDIO_MIXER_CONTROL_MAX;
    }
    if (raw <= AUDIO_MIXER_CONTROL_CENTER) {
        float t = (float)raw / (float)AUDIO_MIXER_CONTROL_CENTER;
        return 0.25f + (0.75f * t);
    }
    float t = (float)(raw - AUDIO_MIXER_CONTROL_CENTER) /
              (float)(AUDIO_MIXER_CONTROL_MAX - AUDIO_MIXER_CONTROL_CENTER);
    return 1.0f + t;
}
```

---

### Task 1: Audio engine pregain API and gain path

**Files:**
- Modify: `firmware/main-deck-p4/components/audio_engine/include/audio_engine.h`
- Modify: `firmware/main-deck-p4/components/audio_engine/audio_engine.c`
- Test: `tests/audio_engine/test_audio_engine.c`

- [ ] **Step 1: Write the failing audio engine test**

Add to `test_mixer_state_api()` after default output gain assertions:

```c
EXPECT(audio_engine_get_pregain(0) == AUDIO_MIXER_CONTROL_CENTER,
       "deck 0 pregain defaults to center");
EXPECT(audio_engine_get_pregain(1) == AUDIO_MIXER_CONTROL_CENTER,
       "deck 1 pregain defaults to center");
EXPECT(audio_engine_set_pregain(2, AUDIO_MIXER_CONTROL_CENTER) == ESP_ERR_INVALID_ARG,
       "invalid pregain deck returns INVALID_ARG");
EXPECT(audio_engine_set_pregain(0, AUDIO_MIXER_CONTROL_CENTER) == ESP_OK,
       "deck 0 pregain accepts center raw value");
audio_engine_get_output_gains(&deck1, &deck2);
EXPECT(nearf(deck1, 1.0f), "center pregain leaves deck 0 gain at unity");
EXPECT(audio_engine_set_pregain(0, 0) == ESP_OK,
       "deck 0 pregain accepts minimum raw value");
audio_engine_get_output_gains(&deck1, &deck2);
EXPECT(deck1 > 0.24f && deck1 < 0.26f, "minimum pregain attenuates deck 0 to quarter gain");
EXPECT(audio_engine_set_pregain(0, AUDIO_MIXER_CONTROL_MAX) == ESP_OK,
       "deck 0 pregain accepts maximum raw value");
audio_engine_get_output_gains(&deck1, &deck2);
EXPECT(deck1 > 1.99f && deck1 < 2.01f, "maximum pregain boosts deck 0 to +6 dB scalar");
EXPECT(audio_engine_set_pregain(0, AUDIO_MIXER_CONTROL_CENTER) == ESP_OK,
       "deck 0 pregain restores center before other mixer tests");
```

Add snapshot assertions near existing channel volume snapshot checks:

```c
EXPECT(snapshot.pregain[0] == AUDIO_MIXER_CONTROL_CENTER,
       "snapshot captures deck 0 pregain raw value");
EXPECT(snapshot.pregain[1] == AUDIO_MIXER_CONTROL_CENTER,
       "snapshot captures deck 1 pregain raw value");
EXPECT(nearf(snapshot.pregain_gain[0], 1.0f),
       "snapshot captures deck 0 pregain scalar");
EXPECT(nearf(snapshot.pregain_gain[1], 1.0f),
       "snapshot captures deck 1 pregain scalar");
```

- [ ] **Step 2: Run RED test**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;C:\msys64\mingw64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: compile failure for missing `audio_engine_get_pregain`, `audio_engine_set_pregain`, or missing snapshot fields.

- [ ] **Step 3: Add public API fields and declarations**

In `audio_engine_mixer_snapshot_t`, add:

```c
uint16_t pregain[AUDIO_ENGINE_DECK_COUNT];
float pregain_gain[AUDIO_ENGINE_DECK_COUNT];
```

Near the existing channel/crossfader declarations, add:

```c
esp_err_t audio_engine_set_pregain(uint8_t deck, uint16_t raw_pregain);
uint16_t audio_engine_get_pregain(uint8_t deck);
```

- [ ] **Step 4: Implement pregain state**

In `audio_engine.c`, add static state beside `s_channel_volume`:

```c
static uint16_t s_pregain[AUDIO_ENGINE_DECK_COUNT] = {
    AUDIO_MIXER_CONTROL_CENTER,
    AUDIO_MIXER_CONTROL_CENTER,
};
```

In `audio_engine_init()`, reset each deck:

```c
s_pregain[i] = AUDIO_MIXER_CONTROL_CENTER;
```

Add helper and API functions:

```c
static float pregain_gain_from_raw(uint16_t raw)
{
    if (raw > AUDIO_MIXER_CONTROL_MAX) {
        raw = AUDIO_MIXER_CONTROL_MAX;
    }
    if (raw <= AUDIO_MIXER_CONTROL_CENTER) {
        float t = (float)raw / (float)AUDIO_MIXER_CONTROL_CENTER;
        return 0.25f + (0.75f * t);
    }
    float t = (float)(raw - AUDIO_MIXER_CONTROL_CENTER) /
              (float)(AUDIO_MIXER_CONTROL_MAX - AUDIO_MIXER_CONTROL_CENTER);
    return 1.0f + t;
}

esp_err_t audio_engine_set_pregain(uint8_t deck, uint16_t raw_pregain)
{
    if (!deck_is_valid(deck)) return ESP_ERR_INVALID_ARG;
    if (raw_pregain > AUDIO_MIXER_CONTROL_MAX) {
        raw_pregain = AUDIO_MIXER_CONTROL_MAX;
    }
    s_pregain[deck] = raw_pregain;
    return ESP_OK;
}

uint16_t audio_engine_get_pregain(uint8_t deck)
{
    if (!deck_is_valid(deck)) return AUDIO_MIXER_CONTROL_CENTER;
    return s_pregain[deck];
}
```

- [ ] **Step 5: Apply pregain in output gain path**

Update `audio_engine_get_output_gains()`:

```c
if (deck0_gain) {
    *deck0_gain = audio_mixer_fader_gain(s_channel_volume[0]) *
                  pregain_gain_from_raw(s_pregain[0]) *
                  xf0 *
                  s_master_trim;
}
if (deck1_gain) {
    *deck1_gain = audio_mixer_fader_gain(s_channel_volume[1]) *
                  pregain_gain_from_raw(s_pregain[1]) *
                  xf1 *
                  s_master_trim;
}
```

Update `audio_engine_get_mixer_snapshot()`:

```c
out_snapshot->pregain[0] = s_pregain[0];
out_snapshot->pregain[1] = s_pregain[1];
out_snapshot->pregain_gain[0] = pregain_gain_from_raw(s_pregain[0]);
out_snapshot->pregain_gain[1] = pregain_gain_from_raw(s_pregain[1]);
```

- [ ] **Step 6: Run audio engine host tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;C:\msys64\mingw64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `audio_engine tests passed`; if later tests fail because of changed output gain assumptions, reset pregain to center before those sections.

---

### Task 2: Route deck_core Trim controls to pregain API

**Files:**
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/deck_core_dual/stubs/audio_engine.h`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`

- [ ] **Step 1: Add failing deck_core route test**

Extend test globals near `audio_engine_stub_channel_volume`:

```c
int audio_engine_stub_pregain[DECK_CORE_DECK_COUNT];
```

In `reset_audio_engine_stub()`, reset:

```c
for (int deck = 0; deck < DECK_CORE_DECK_COUNT; deck++) {
    audio_engine_stub_pregain[deck] = -1;
}
```

Add test:

```c
static void test_mixer_namespace_routes_trim_to_pregain(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t trim1 = mixer_value(CTRL_ID_CH1_TRIM, 6000);
    ctrl_event_t trim2 = mixer_value(CTRL_ID_CH2_TRIM, 12000);

    deck_core_test_apply_event(&trim1);
    deck_core_test_apply_event(&trim2);

    assert(audio_engine_stub_pregain[CTRL_DECK_1] == 6000);
    assert(audio_engine_stub_pregain[CTRL_DECK_2] == 12000);
}
```

Call it in `main()` after `test_mixer_namespace_routes_volume_and_crossfader();`.

- [ ] **Step 2: Add failing stub declaration**

In `tests/deck_core_dual/stubs/audio_engine.h`, add:

```c
extern int audio_engine_stub_pregain[2];

static inline esp_err_t audio_engine_set_pregain(uint8_t deck, uint16_t raw_pregain)
{
    if (deck >= 2) return ESP_ERR_INVALID_ARG;
    audio_engine_stub_pregain[deck] = raw_pregain;
    return ESP_OK;
}
```

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;C:\msys64\mingw64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `deck_core_dual` assertion fails because Trim is still deferred/log-only.

- [ ] **Step 3: Route Trim in deck_core**

In `is_deferred_mixer_control()`, remove:

```c
case CTRL_ID_CH1_TRIM:
case CTRL_ID_CH2_TRIM:
```

In `deferred_mixer_control_name()`, remove Trim name cases or leave unreachable only if compiler does not warn.

In `on_mixer_control()`, replace Trim deferred block with:

```c
case CTRL_ID_CH1_TRIM:
    audio_engine_set_pregain(CTRL_DECK_1, value);
    break;
case CTRL_ID_CH2_TRIM:
    audio_engine_set_pregain(CTRL_DECK_2, value);
    break;
case CTRL_ID_HEADPHONE_MIX:
    if (should_log_deferred_mixer_value(id, value)) {
        ESP_LOGI(TAG, "mixer control %s raw=%u (DSP behavior deferred)",
                 deferred_mixer_control_name(id), (unsigned)value);
    }
    break;
```

- [ ] **Step 4: Update deferred logging test**

Replace the Trim-specific deferred logging assertions with Headphones Mix:

```c
assert(deck_core_test_should_log_deferred_mixer_value(CTRL_ID_HEADPHONE_MIX, 0));
assert(!deck_core_test_should_log_deferred_mixer_value(CTRL_ID_HEADPHONE_MIX, 100));
assert(!deck_core_test_should_log_deferred_mixer_value(CTRL_ID_HEADPHONE_MIX, 512));
assert(!deck_core_test_should_log_deferred_mixer_value(CTRL_ID_HEADPHONE_MIX, 1024));
assert(deck_core_test_should_log_deferred_mixer_value(CTRL_ID_HEADPHONE_MIX, 2048));
assert(!deck_core_test_should_log_deferred_mixer_value(CTRL_ID_HEADPHONE_MIX, 3000));
assert(deck_core_test_should_log_deferred_mixer_value(CTRL_ID_HEADPHONE_MIX, 4096));
```

Add explicit assertions that Trim is not deferred:

```c
assert(!deck_core_test_should_log_deferred_mixer_value(CTRL_ID_CH1_TRIM, 0));
assert(!deck_core_test_should_log_deferred_mixer_value(CTRL_ID_CH2_TRIM, 0));
```

- [ ] **Step 5: Run full P4 host tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;C:\msys64\mingw64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `P4 host tests passed.`

Clean generated artifacts if present:

```powershell
Remove-Item -LiteralPath "D:\Documents\DDJ-FFL4\tests\audio_engine\dummy_diag_audio.mp3" -ErrorAction SilentlyContinue
Remove-Item -LiteralPath "D:\Documents\DDJ-FFL4\tests\anlz\test_unicode_ppth.dat" -ErrorAction SilentlyContinue
```

---

### Task 3: Status/UI exposure check

**Files:**
- Inspect: `firmware/main-deck-p4/components/web_server/web_server.c`
- Inspect: `firmware/main-deck-p4/components/ui/ui_mixer_view.c`
- Inspect: `firmware/main-deck-p4/components/ui/ui_overview.c`
- Optional modify only if current display/status needs pregain raw/scalar.

- [ ] **Step 1: Verify web status snapshot remains valid**

Inspect `web_server.c` JSON formatting around mixer fields. If it serializes `audio_engine_mixer_snapshot_t` field-by-field and ignores pregain, decide whether to expose pregain in `/api/status`.

Minimal addition if exposing:

```c
"\"pregain\":[%u,%u],"
"\"pregain_gain\":[%.3f,%.3f],"
```

using:

```c
mixer.pregain[0], mixer.pregain[1],
(double)mixer.pregain_gain[0], (double)mixer.pregain_gain[1]
```

- [ ] **Step 2: Do not change Overview mixer UI unless necessary**

Current Overview uses channel fader raw and final `output_gain`. Since pregain affects `output_gain`, the existing visible output level already reflects Trim. Avoid adding another UI label in this iteration.

- [ ] **Step 3: Run relevant UI/status host tests**

Run full host suite:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;C:\msys64\mingw64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: `ui_status`, `web_api_helpers`, and full suite pass.

---

### Task 4: Documentation and firmware build

**Files:**
- Modify: `README.md`
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`
- Modify: `docs/STARTUP_CHECKLIST.md`

- [ ] **Step 1: Update MIDI map row**

In `docs/DDJ_FLX4_MIDI_MAP.md`, update Trim row from:

```markdown
| Trim / pregain | ... | audio mixer | Mapped only | Verified 2026-06-21; DSP behavior deferred |
```

to:

```markdown
| Trim / pregain | ... | audio mixer | Implemented | Verified 2026-06-21; P4 pregain DSP host-tested; hardware smoke pending |
```

- [ ] **Step 2: Update project status docs**

Add concise text to README / DEVELOPMENT_PLAN / STARTUP_CHECKLIST:

```markdown
Trim/pregain now routes the FLX4 deck-local Trim knobs into P4 audio output gain
as a bounded pregain scalar: center is unity, left attenuates, and right boosts
up to +6 dB before the existing post-sum limiter.
```

- [ ] **Step 3: Run final checks**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;C:\msys64\mingw64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
git diff --check
git status --short
```

Expected:

- `P4 host tests passed.`
- `git diff --check` has exit code 0.
- Only intended source/docs files are modified.

- [ ] **Step 4: Build P4**

Run:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Expected: `Project build complete.`

- [ ] **Step 5: Commit**

Run:

```powershell
cd D:\Documents\DDJ-FFL4
git add README.md docs\DDJ_FLX4_MIDI_MAP.md docs\DEVELOPMENT_PLAN.md docs\STARTUP_CHECKLIST.md firmware\main-deck-p4\components\audio_engine\include\audio_engine.h firmware\main-deck-p4\components\audio_engine\audio_engine.c firmware\main-deck-p4\components\deck_core\deck_core.c tests\audio_engine\test_audio_engine.c tests\deck_core_dual\stubs\audio_engine.h tests\deck_core_dual\test_deck_core_dual.c
git commit -m "feat(audio): add deck trim pregain dsp"
```

---

## Hardware smoke checklist

After flashing P4:

- [ ] CH1 Trim center: Deck 1 loudness unchanged.
- [ ] CH1 Trim left: Deck 1 attenuates smoothly.
- [ ] CH1 Trim right: Deck 1 boosts smoothly; no immediate harsh clipping.
- [ ] CH2 Trim center/left/right same behavior.
- [ ] Channel faders still work after Trim changes.
- [ ] Crossfader still works after Trim changes.
- [ ] Two-deck playback audio remains stable.
- [ ] CLIP/limiter indicator is acceptable; if sustained clipping appears, lower master trim rather than reducing pregain range immediately.

Suggested report format:

```text
CH1 Trim: OK/problem
CH2 Trim: OK/problem
Faders/crossfader: OK/problem
Two-deck audio: OK/problem
CLIP indicator: OK/problem
Waveform: OK/problem
```

---

## Self-review

- Spec coverage: S3 mapping is already present; plan covers P4 audio engine, deck_core route, tests, docs, build, and hardware smoke.
- Placeholder scan: no `TBD`/`TODO` placeholders remain; behavior and commands are explicit.
- Type consistency: proposed API names are `audio_engine_set_pregain()` and `audio_engine_get_pregain()` in header, implementation, and tests; snapshot fields are `pregain[]` and `pregain_gain[]`.
