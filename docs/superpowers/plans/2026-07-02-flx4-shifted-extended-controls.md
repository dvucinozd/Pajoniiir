# FLX4 Shifted Extended Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the remaining DDJ-FLX4 shifted/extended controls: Browse+Shift press/rotate, Beat Sync master, Reloop/Exit+Shift stop, Loop Adjust In/Out, Quantize, and Play+Shift Censor.

**Architecture:** Keep the existing 7-byte `0xA5` control_link frame and preserve every existing semantic ID. Browser controls get one additional browser ID, while rare deck-local extended actions use one packed `CTRL_DECK_CTL_EXT_ACTION` deck control with an action enum in `value`; this avoids exhausting the 32-ID deck namespace.

**Tech Stack:** ESP-IDF C firmware, S3 USB MIDI host mapping, P4 `deck_core`, host C tests under `tests/`.

---

## File Structure

- `firmware/control-board-s3/components/control_link/include/control_link.h`
  - Add shared deck extension action packing macros and `CTRL_ID_BROWSE_SHIFT_PRESS`.
- `firmware/main-deck-p4/components/control_link/include/control_link.h`
  - Mirror S3 protocol additions exactly.
- `firmware/main-deck-p4/components/control_link/control_link_uart.c`
  - Route `CTRL_ID_BROWSE_SHIFT_DELTA` as `CTRL_EV_BROWSE`.
- `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
  - Map XML-derived MIDI addresses to semantic events.
- `firmware/main-deck-p4/components/deck_core/include/deck_core.h`
  - Expose new deck state fields for quantize, sync master, and censor state.
- `firmware/main-deck-p4/components/deck_core/deck_core.c`
  - Implement P4-owned behavior.
- `tests/control_link_protocol/{s3_constants.c,p4_constants.c,test_control_link_protocol.c}`
  - Lock shared IDs and packed values.
- `tests/flx4_midi_host/test_flx4_map.c`
  - Host-test all S3 MIDI mappings.
- `tests/deck_core_dual/test_deck_core_dual.c`
  - Host-test P4 behavior.
- `docs/DDJ_FLX4_MIDI_MAP.md`, `docs/CONTROL_LINK_PROTOCOL.md`, `docs/DEVELOPMENT_PLAN.md`, `docs/STARTUP_CHECKLIST.md`
  - Update status, semantics, and verification notes.

## Behavioral Decisions

- `Browse+Shift rotate`: accelerated navigation. In Library, call `ui_library_select_delta(delta * 10)`. In Overview, call `ui_overview_zoom_delta(delta * 4)`.
- `Browse+Shift press`: force Library view with `ui_show_library()` on press; release is ignored.
- `Beat Sync master`: set the pressed deck as sync reference. The master deck is not a follower; normal Beat Sync on the other deck references the selected master.
- `Reloop/Exit+Shift stop`: clear active loop, pending Loop In marker, remembered last loop, beat-loop LED state, and shifted roll shadow for that deck.
- `Loop Adjust In/Out`: on press, set the active loop start/end to the current deck position if the new boundary is valid. If Quantize is enabled and a beatgrid exists, snap the boundary to the nearest beat.
- `Quantize`: deck-local toggle. This slice applies it to Loop In, Loop Out, and Loop Adjust In/Out.
- `Play+Shift / Censor`: MVP slip-censor behavior without adding reverse MP3 decode. On press, store the audible position and seek back by `1000 ms`; playback continues forward. On release, seek to the stored position plus held duration if the deck was playing, or to the stored position if paused.

---

### Task 1: Shared Protocol IDs And Packing

**Files:**
- Modify: `firmware/control-board-s3/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/include/control_link.h`
- Modify: `firmware/main-deck-p4/components/control_link/control_link_uart.c`
- Modify: `tests/control_link_protocol/s3_constants.c`
- Modify: `tests/control_link_protocol/p4_constants.c`
- Modify: `tests/control_link_protocol/test_control_link_protocol.c`

- [ ] **Step 1: Add failing protocol tests**

Add these externs to `tests/control_link_protocol/test_control_link_protocol.c`:

```c
int p4_ctrl_id_browse_shift_press(void);
int p4_ctrl_id_deck1_ext_action(void);
int p4_ctrl_id_deck2_ext_action(void);
int p4_ctrl_deck_ext_action_censor(void);
int p4_ctrl_deck_ext_action_sync_master(void);
int p4_ctrl_deck_ext_action_reloop_stop(void);
int p4_ctrl_deck_ext_action_loop_adjust_in(void);
int p4_ctrl_deck_ext_action_loop_adjust_out(void);
int p4_ctrl_deck_ext_action_quantize(void);

int s3_ctrl_id_browse_shift_press(void);
int s3_ctrl_id_deck1_ext_action(void);
int s3_ctrl_id_deck2_ext_action(void);
int s3_ctrl_deck_ext_action_censor(void);
int s3_ctrl_deck_ext_action_sync_master(void);
int s3_ctrl_deck_ext_action_reloop_stop(void);
int s3_ctrl_deck_ext_action_loop_adjust_in(void);
int s3_ctrl_deck_ext_action_loop_adjust_out(void);
int s3_ctrl_deck_ext_action_quantize(void);
```

Extend `test_encoder_ids_route_to_jog_and_browse()`:

```c
    build_frame(frame, CTRL_TYPE_ENCODER, CTRL_ID_BROWSE_SHIFT_DELTA, 4, 12);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_BROWSE);
    assert(ev.id == CTRL_ID_BROWSE_SHIFT_DELTA);
    assert(ev.value == 4);
```

Extend `test_firmware_decodes_deck_aware_flx4_ids()`:

```c
    build_frame(frame, CTRL_TYPE_BUTTON, CTRL_ID_DECK1_EXT_ACTION,
                CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_CENSOR, true), 28);
    assert(decode_p4_frame(frame, &ev));
    assert(ev.type == CTRL_EV_BUTTON);
    assert(ev.deck == CTRL_DECK_1);
    assert(ev.control == CTRL_DECK_CTL_EXT_ACTION);
    assert(CTRL_DECK_EXT_ACTION(ev.value) == CTRL_DECK_EXT_ACTION_CENSOR);
    assert(CTRL_DECK_EXT_PRESSED(ev.value));
```

Extend `test_s3_and_p4_deck_aware_ids_match()`:

```c
    assert(s3_ctrl_id_browse_shift_press() == p4_ctrl_id_browse_shift_press());
    assert(s3_ctrl_id_browse_shift_press() == CTRL_ID_BROWSE_SHIFT_PRESS);
    assert(s3_ctrl_id_deck1_ext_action() == p4_ctrl_id_deck1_ext_action());
    assert(s3_ctrl_id_deck1_ext_action() == CTRL_ID_DECK1_EXT_ACTION);
    assert(s3_ctrl_id_deck2_ext_action() == p4_ctrl_id_deck2_ext_action());
    assert(s3_ctrl_id_deck2_ext_action() == CTRL_ID_DECK2_EXT_ACTION);
    assert(s3_ctrl_deck_ext_action_censor() == p4_ctrl_deck_ext_action_censor());
    assert(s3_ctrl_deck_ext_action_sync_master() == p4_ctrl_deck_ext_action_sync_master());
    assert(s3_ctrl_deck_ext_action_reloop_stop() == p4_ctrl_deck_ext_action_reloop_stop());
    assert(s3_ctrl_deck_ext_action_loop_adjust_in() == p4_ctrl_deck_ext_action_loop_adjust_in());
    assert(s3_ctrl_deck_ext_action_loop_adjust_out() == p4_ctrl_deck_ext_action_loop_adjust_out());
    assert(s3_ctrl_deck_ext_action_quantize() == p4_ctrl_deck_ext_action_quantize());
    assert(CTRL_DECK_EXT_ACTION(CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_QUANTIZE, true)) ==
           CTRL_DECK_EXT_ACTION_QUANTIZE);
    assert(CTRL_DECK_EXT_PRESSED(CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_QUANTIZE, true)));
    assert(!CTRL_DECK_EXT_PRESSED(CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_QUANTIZE, false)));
```

- [ ] **Step 2: Run protocol test and verify it fails**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\control_link_protocol clean all
```

Expected: compile failure for missing `CTRL_DECK_CTL_EXT_ACTION`, `CTRL_ID_BROWSE_SHIFT_PRESS`, and `CTRL_DECK_EXT_*` symbols.

- [ ] **Step 3: Add shared constants to both control_link headers**

In both S3 and P4 `control_link.h`, append one deck control and packed ext action definitions after `CTRL_DECK_CTL_JOG_SEARCH_TOUCH`:

```c
    CTRL_DECK_CTL_JOG_SEARCH_TOUCH,
    CTRL_DECK_CTL_EXT_ACTION,
} ctrl_deck_control_t;

typedef enum {
    CTRL_DECK_EXT_ACTION_CENSOR = 0,
    CTRL_DECK_EXT_ACTION_SYNC_MASTER = 1,
    CTRL_DECK_EXT_ACTION_RELOOP_STOP = 2,
    CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN = 3,
    CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT = 4,
    CTRL_DECK_EXT_ACTION_QUANTIZE = 5,
} ctrl_deck_ext_action_t;

#define CTRL_DECK_EXT_VALUE(action, pressed) \
    ((int16_t)(((action) & 0x7F) | ((pressed) ? 0x80 : 0x00)))
#define CTRL_DECK_EXT_ACTION(value) ((uint8_t)((value) & 0x7F))
#define CTRL_DECK_EXT_PRESSED(value) (((value) & 0x80) != 0)
```

Add IDs after `CTRL_ID_DECK*_JOG_SEARCH_TOUCH`:

```c
#define CTRL_ID_DECK1_EXT_ACTION           (CTRL_NS_DECK1 + CTRL_DECK_CTL_EXT_ACTION)
#define CTRL_ID_DECK2_EXT_ACTION           (CTRL_NS_DECK2 + CTRL_DECK_CTL_EXT_ACTION)
```

Add browser press ID after `CTRL_ID_BROWSE_SHIFT_DELTA`:

```c
#define CTRL_ID_BROWSE_SHIFT_PRESS (CTRL_NS_BROWSER | 0x05)
```

- [ ] **Step 4: Route shifted browse delta as browse**

In `firmware/main-deck-p4/components/control_link/control_link_uart.c`, update encoder dispatch:

```c
        } else if (ev.id == 1 ||
                   ev.id == CTRL_ID_BROWSE_DELTA ||
                   ev.id == CTRL_ID_BROWSE_SHIFT_DELTA) {
            ev.type = CTRL_EV_BROWSE;
```

Mirror the same decode logic in `tests/control_link_protocol/test_control_link_protocol.c`.

- [ ] **Step 5: Export new constants for tests**

Add to `tests/control_link_protocol/s3_constants.c` and `p4_constants.c`:

```c
int s3_ctrl_id_browse_shift_press(void) { return CTRL_ID_BROWSE_SHIFT_PRESS; }
int s3_ctrl_id_deck1_ext_action(void) { return CTRL_ID_DECK1_EXT_ACTION; }
int s3_ctrl_id_deck2_ext_action(void) { return CTRL_ID_DECK2_EXT_ACTION; }
int s3_ctrl_deck_ext_action_censor(void) { return CTRL_DECK_EXT_ACTION_CENSOR; }
int s3_ctrl_deck_ext_action_sync_master(void) { return CTRL_DECK_EXT_ACTION_SYNC_MASTER; }
int s3_ctrl_deck_ext_action_reloop_stop(void) { return CTRL_DECK_EXT_ACTION_RELOOP_STOP; }
int s3_ctrl_deck_ext_action_loop_adjust_in(void) { return CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN; }
int s3_ctrl_deck_ext_action_loop_adjust_out(void) { return CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT; }
int s3_ctrl_deck_ext_action_quantize(void) { return CTRL_DECK_EXT_ACTION_QUANTIZE; }
```

Use `p4_` prefixes in `p4_constants.c`.

- [ ] **Step 6: Run protocol test**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\control_link_protocol clean all
```

Expected: `control_link_protocol tests passed`.

- [ ] **Step 7: Commit**

```powershell
git add firmware/control-board-s3/components/control_link/include/control_link.h firmware/main-deck-p4/components/control_link/include/control_link.h firmware/main-deck-p4/components/control_link/control_link_uart.c tests/control_link_protocol
git commit -m "feat: add flx4 extended action protocol ids"
```

---

### Task 2: S3 MIDI Mapping

**Files:**
- Modify: `firmware/control-board-s3/components/flx4_midi_host/flx4_map.c`
- Modify: `tests/flx4_midi_host/test_flx4_map.c`

- [ ] **Step 1: Add failing S3 mapping tests**

Add this helper to `tests/flx4_midi_host/test_flx4_map.c`:

```c
static void expect_ext_action(const flx4_control_event_t *ev,
                              uint8_t id,
                              uint8_t action,
                              bool pressed)
{
    assert(ev->type == CTRL_TYPE_BUTTON);
    assert(ev->id == id);
    assert(CTRL_DECK_EXT_ACTION(ev->value) == action);
    assert(CTRL_DECK_EXT_PRESSED(ev->value) == pressed);
}
```

Add this test before `test_unsupported_messages_are_ignored()`:

```c
static void test_shifted_extended_controls(void)
{
    flx4_map_state_t state;
    flx4_control_event_t ev;
    flx4_map_init(&state);

    assert(flx4_map_message(&state, MSG(0xB6, 0x64, 0x01), &ev));
    expect_event(&ev, CTRL_TYPE_ENCODER, CTRL_ID_BROWSE_SHIFT_DELTA, 1);
    assert(flx4_map_message(&state, MSG(0xB6, 0x64, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_ENCODER, CTRL_ID_BROWSE_SHIFT_DELTA, -1);
    assert(!flx4_map_message(&state, MSG(0xB6, 0x64, 0x00), &ev));

    assert(flx4_map_message(&state, MSG(0x96, 0x42, 0x7F), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BROWSE_SHIFT_PRESS, 1);
    assert(flx4_map_message(&state, MSG(0x96, 0x42, 0x00), &ev));
    expect_event(&ev, CTRL_TYPE_BUTTON, CTRL_ID_BROWSE_SHIFT_PRESS, 0);

    assert(flx4_map_message(&state, MSG(0x90, 0x0E, 0x7F), &ev));
    expect_ext_action(&ev, CTRL_ID_DECK1_EXT_ACTION, CTRL_DECK_EXT_ACTION_CENSOR, true);
    assert(flx4_map_message(&state, MSG(0x91, 0x0E, 0x00), &ev));
    expect_ext_action(&ev, CTRL_ID_DECK2_EXT_ACTION, CTRL_DECK_EXT_ACTION_CENSOR, false);

    assert(flx4_map_message(&state, MSG(0x90, 0x5C, 0x7F), &ev));
    expect_ext_action(&ev, CTRL_ID_DECK1_EXT_ACTION, CTRL_DECK_EXT_ACTION_SYNC_MASTER, true);
    assert(flx4_map_message(&state, MSG(0x91, 0x50, 0x7F), &ev));
    expect_ext_action(&ev, CTRL_ID_DECK2_EXT_ACTION, CTRL_DECK_EXT_ACTION_RELOOP_STOP, true);
    assert(flx4_map_message(&state, MSG(0x90, 0x4C, 0x7F), &ev));
    expect_ext_action(&ev, CTRL_ID_DECK1_EXT_ACTION, CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN, true);
    assert(flx4_map_message(&state, MSG(0x91, 0x4E, 0x7F), &ev));
    expect_ext_action(&ev, CTRL_ID_DECK2_EXT_ACTION, CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT, true);
    assert(flx4_map_message(&state, MSG(0x90, 0x68, 0x7F), &ev));
    expect_ext_action(&ev, CTRL_ID_DECK1_EXT_ACTION, CTRL_DECK_EXT_ACTION_QUANTIZE, true);
}
```

Call it from `main()` and remove the old unsupported assertion for `0x90/0x0E`.

- [ ] **Step 2: Run S3 map test and verify it fails**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\flx4_midi_host clean all
```

Expected: failure for missing mappings.

- [ ] **Step 3: Add FLX4 MIDI constants**

In `flx4_map.c`, add button/CC defines near related controls:

```c
#define FLX4_BTN_CENSOR        0x0E
#define FLX4_BTN_SYNC_MASTER   0x5C
#define FLX4_BTN_RELOOP_STOP   0x50
#define FLX4_BTN_LOOP_ADJUST_IN 0x4C
#define FLX4_BTN_LOOP_ADJUST_OUT 0x4E
#define FLX4_BTN_QUANTIZE      0x68
#define FLX4_BTN_BROWSE_SHIFT_PRESS 0x42
#define FLX4_CC_BROWSE_SHIFT   0x64
```

- [ ] **Step 4: Add helper and deck-button mapping**

Add helper:

```c
static bool emit_deck_ext_action(flx4_control_event_t *out,
                                 bool deck1,
                                 uint8_t action,
                                 uint8_t pressed)
{
    out->type = CTRL_TYPE_BUTTON;
    out->id = deck1 ? CTRL_ID_DECK1_EXT_ACTION : CTRL_ID_DECK2_EXT_ACTION;
    out->value = CTRL_DECK_EXT_VALUE(action, pressed != 0);
    return true;
}
```

Add to `map_deck_button()`:

```c
    case FLX4_BTN_CENSOR:
        return emit_deck_ext_action(out, deck1, CTRL_DECK_EXT_ACTION_CENSOR, pressed);
    case FLX4_BTN_SYNC_MASTER:
        return emit_deck_ext_action(out, deck1, CTRL_DECK_EXT_ACTION_SYNC_MASTER, pressed);
    case FLX4_BTN_RELOOP_STOP:
        return emit_deck_ext_action(out, deck1, CTRL_DECK_EXT_ACTION_RELOOP_STOP, pressed);
    case FLX4_BTN_LOOP_ADJUST_IN:
        return emit_deck_ext_action(out, deck1, CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN, pressed);
    case FLX4_BTN_LOOP_ADJUST_OUT:
        return emit_deck_ext_action(out, deck1, CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT, pressed);
    case FLX4_BTN_QUANTIZE:
        return emit_deck_ext_action(out, deck1, CTRL_DECK_EXT_ACTION_QUANTIZE, pressed);
```

- [ ] **Step 5: Add browse shifted mapping**

In `map_master_cc()`:

```c
    case FLX4_CC_BROWSE_SHIFT:
        return emit_encoder(out, CTRL_ID_BROWSE_SHIFT_DELTA, relative_twos_complement_delta(data2));
```

In global button mapping:

```c
        if (msg->data1 == FLX4_BTN_BROWSE_SHIFT_PRESS) {
            return emit_button(out, CTRL_ID_BROWSE_SHIFT_PRESS, msg->data2 > 0 ? 1 : 0);
        }
```

- [ ] **Step 6: Run S3 map test**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\flx4_midi_host clean all
```

Expected: `flx4_map tests passed`, `flx4_midi_host tests passed`, and LED tests still pass.

- [ ] **Step 7: Commit**

```powershell
git add firmware/control-board-s3/components/flx4_midi_host/flx4_map.c tests/flx4_midi_host/test_flx4_map.c
git commit -m "feat: map flx4 shifted extended controls"
```

---

### Task 3: Browse+Shift P4 Behavior

**Files:**
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`

- [ ] **Step 1: Add failing browse behavior tests**

In `tests/deck_core_dual/test_deck_core_dual.c`, add global:

```c
static int s_show_library_calls;
```

Add stub:

```c
esp_err_t ui_show_library(void)
{
    s_show_library_calls++;
    s_ui_library_active = true;
    return ESP_OK;
}
```

Add helpers:

```c
static ctrl_event_t browse_shift_delta(int16_t delta)
{
    return (ctrl_event_t) {
        .type = CTRL_EV_BROWSE,
        .id = CTRL_ID_BROWSE_SHIFT_DELTA,
        .value = delta,
    };
}
```

Add tests:

```c
static void test_shift_browse_delta_accelerates_library_navigation(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_browse_delta = 0;
    s_overview_zoom_delta = 0;
    s_ui_library_active = true;

    ctrl_event_t browse = browse_shift_delta(2);
    deck_core_test_apply_event(&browse);

    assert(s_browse_delta == 20);
    assert(s_overview_zoom_delta == 0);
}

static void test_shift_browse_delta_accelerates_overview_zoom(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_browse_delta = 0;
    s_overview_zoom_delta = 0;
    s_ui_library_active = false;
    s_ui_overview_active = true;

    ctrl_event_t browse = browse_shift_delta(-2);
    deck_core_test_apply_event(&browse);

    assert(s_browse_delta == 0);
    assert(s_overview_zoom_delta == -8);
}

static void test_shift_browse_press_forces_library_view(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_show_library_calls = 0;
    s_toggle_library_view_calls = 0;
    s_ui_library_active = false;

    ctrl_event_t press = browser_button(CTRL_ID_BROWSE_SHIFT_PRESS);
    ctrl_event_t release = press;
    release.value = 0;
    deck_core_test_apply_event(&press);
    deck_core_test_apply_event(&release);

    assert(s_show_library_calls == 1);
    assert(s_toggle_library_view_calls == 0);
    assert(s_ui_library_active);
}
```

Call these tests from `main()`.

- [ ] **Step 2: Run deck_core test and verify it fails**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\deck_core_dual clean all
```

Expected: failures because shifted browse delta uses normal multiplier or button is ignored.

- [ ] **Step 3: Implement shifted browse helpers**

In `deck_core.c`, add constants:

```c
#define BROWSE_SHIFT_LIBRARY_MULTIPLIER 10
#define BROWSE_SHIFT_OVERVIEW_MULTIPLIER 4
```

Change `on_browse()` to branch on ID:

```c
static void on_browse_event(uint8_t id, int16_t delta)
{
    if (delta == 0) return;
    bool shifted = id == CTRL_ID_BROWSE_SHIFT_DELTA;
    bool library_active = !ui_is_library_active || ui_is_library_active();
    bool overview_active = ui_is_overview_active && ui_is_overview_active();
    if (library_active && ui_library_select_delta) {
        int scaled = shifted ? delta * BROWSE_SHIFT_LIBRARY_MULTIPLIER : delta;
        esp_err_t rc = ui_library_select_delta(scaled);
        ESP_LOGD(TAG, "browse %+d -> %s", scaled, esp_err_to_name(rc));
    } else if (!library_active && overview_active && ui_overview_zoom_delta) {
        int scaled = shifted ? delta * BROWSE_SHIFT_OVERVIEW_MULTIPLIER : delta;
        esp_err_t rc = ui_overview_zoom_delta(scaled);
        ESP_LOGD(TAG, "overview zoom %+d -> %s", scaled, esp_err_to_name(rc));
    } else {
        ESP_LOGW(TAG, "browse unsupported: UI API unavailable");
    }
}
```

Replace `on_browse(ev.value)` calls with `on_browse_event(ev.id, ev.value)`.

Add:

```c
static void on_browse_shift_press(void)
{
    if (ui_show_library) {
        esp_err_t rc = ui_show_library();
        ESP_LOGD(TAG, "browse shift press -> library: %s", esp_err_to_name(rc));
    } else if (ui_toggle_library_view) {
        esp_err_t rc = ui_toggle_library_view();
        ESP_LOGD(TAG, "browse shift press fallback -> toggle: %s", esp_err_to_name(rc));
    } else {
        ESP_LOGW(TAG, "browse shift press unsupported: UI API unavailable");
    }
}
```

Update `event_uses_ui_without_deck_state()`:

```c
    return ev->id == BTN_LOAD ||
           ev->id == BTN_TRACK_PREV ||
           ev->id == BTN_TRACK_NEXT ||
           ev->id == CTRL_ID_BROWSE_PRESS ||
           ev->id == CTRL_ID_BROWSE_SHIFT_PRESS;
```

In both task and test event paths, handle the button:

```c
            } else if (ev.id == CTRL_ID_BROWSE_SHIFT_PRESS) {
                if (ev.value != 0) on_browse_shift_press();
```

Use `ev->` in the PC-test equivalent.

- [ ] **Step 4: Run deck_core test**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\deck_core_dual clean all
```

Expected: `deck_core_dual tests passed`.

- [ ] **Step 5: Commit**

```powershell
git add firmware/main-deck-p4/components/deck_core/deck_core.c tests/deck_core_dual/test_deck_core_dual.c
git commit -m "feat: implement shifted browse navigation"
```

---

### Task 4: Beat Sync Master

**Files:**
- Modify: `firmware/main-deck-p4/components/deck_core/include/deck_core.h`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`

- [ ] **Step 1: Add failing sync-master tests**

Add helper:

```c
static ctrl_event_t deck_ext_action(uint8_t deck, uint8_t action, bool pressed)
{
    return (ctrl_event_t) {
        .type = CTRL_EV_BUTTON,
        .id = deck == CTRL_DECK_1 ? CTRL_ID_DECK1_EXT_ACTION : CTRL_ID_DECK2_EXT_ACTION,
        .value = CTRL_DECK_EXT_VALUE(action, pressed),
    };
}
```

Add tests:

```c
static void test_sync_master_marks_requested_deck_as_reference(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t master = deck_ext_action(CTRL_DECK_1, CTRL_DECK_EXT_ACTION_SYNC_MASTER, true);
    deck_core_test_apply_event(&master);

    assert(deck_core_test_get_deck_state(CTRL_DECK_1).sync_master);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_2).sync_master);
    assert(!deck_core_test_get_deck_state(CTRL_DECK_1).sync_enabled);
}

static void test_sync_uses_selected_master_deck_as_reference(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    s_loaded_bpm[CTRL_DECK_1] = 100;
    s_loaded_bpm[CTRL_DECK_2] = 125;
    audio_engine_stub_pitch_percent[CTRL_DECK_1] = 0.0f;

    ctrl_event_t master = deck_ext_action(CTRL_DECK_1, CTRL_DECK_EXT_ACTION_SYNC_MASTER, true);
    ctrl_event_t deck2_sync = deck_button(CTRL_ID_DECK2_SYNC);
    deck_core_test_apply_event(&master);
    deck_core_test_apply_event(&deck2_sync);

    assert(deck_core_test_get_deck_state(CTRL_DECK_2).sync_enabled);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_2] < -19.99f);
    assert(audio_engine_stub_pitch_percent[CTRL_DECK_2] > -20.01f);
}
```

- [ ] **Step 2: Run deck_core test and verify it fails**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\deck_core_dual clean all
```

Expected: compile failure for `sync_master` or behavior failure.

- [ ] **Step 3: Add deck state and sync master state**

In `deck_core.h`, add to `deck_state_t` after `sync_enabled`:

```c
    bool          sync_master;
```

In `deck_core.c`, add global:

```c
static uint8_t s_sync_master_deck = CTRL_DECK_NONE;
```

Reset it in `deck_core_test_reset()` and when resetting all decks; in `deck_core_reset_deck()` clear only if the reset deck is master:

```c
    if (s_sync_master_deck == idx) {
        s_sync_master_deck = CTRL_DECK_NONE;
    }
```

- [ ] **Step 4: Implement reference selection and ext action**

Add:

```c
static uint8_t beat_sync_reference_deck(uint8_t deck)
{
    if (s_sync_master_deck < DECK_CORE_DECK_COUNT && s_sync_master_deck != deck) {
        return s_sync_master_deck;
    }
    return deck == CTRL_DECK_1 ? CTRL_DECK_2 : CTRL_DECK_1;
}

static void set_sync_master(uint8_t deck, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state) {
        return;
    }
    s_sync_master_deck = deck;
    for (uint8_t i = 0; i < DECK_CORE_DECK_COUNT; i++) {
        s_decks[i].sync_master = i == deck;
    }
    state->sync_enabled = false;
    ESP_LOGI(TAG, "deck %u sync master", (unsigned)deck + 1);
    publish_flx4_led_snapshot(false);
}
```

Replace the reference selection inside `apply_beat_sync()`:

```c
    uint8_t reference_deck = beat_sync_reference_deck(deck);
```

In the ext action handler added in Task 5, route `CTRL_DECK_EXT_ACTION_SYNC_MASTER` to `set_sync_master(deck, state)` on press.

- [ ] **Step 5: Run deck_core test**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\deck_core_dual clean all
```

Expected: `deck_core_dual tests passed`.

- [ ] **Step 6: Commit**

```powershell
git add firmware/main-deck-p4/components/deck_core/include/deck_core.h firmware/main-deck-p4/components/deck_core/deck_core.c tests/deck_core_dual/test_deck_core_dual.c
git commit -m "feat: implement sync master control"
```

---

### Task 5: Loop Stop, Loop Adjust, And Quantize

**Files:**
- Modify: `firmware/main-deck-p4/components/deck_core/include/deck_core.h`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`

- [ ] **Step 1: Add failing loop and quantize tests**

Add tests:

```c
static void test_quantize_toggle_updates_requested_deck_only(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();

    ctrl_event_t quantize = deck_ext_action(CTRL_DECK_2, CTRL_DECK_EXT_ACTION_QUANTIZE, true);
    ctrl_event_t release = deck_ext_action(CTRL_DECK_2, CTRL_DECK_EXT_ACTION_QUANTIZE, false);
    deck_core_test_apply_event(&quantize);
    deck_core_test_apply_event(&release);

    assert(!deck_core_test_get_deck_state(CTRL_DECK_1).quantize_enabled);
    assert(deck_core_test_get_deck_state(CTRL_DECK_2).quantize_enabled);
}

static void test_reloop_shift_stop_clears_active_and_remembered_loop(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 1000;

    ctrl_event_t loop_in = deck_button(CTRL_ID_DECK1_LOOP_IN);
    ctrl_event_t loop_out = deck_button(CTRL_ID_DECK1_LOOP_OUT);
    ctrl_event_t stop = deck_ext_action(CTRL_DECK_1, CTRL_DECK_EXT_ACTION_RELOOP_STOP, true);
    ctrl_event_t reloop = deck_button(CTRL_ID_DECK1_RELOOP_EXIT);

    deck_core_test_apply_event(&loop_in);
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 4000;
    deck_core_test_apply_event(&loop_out);
    assert(audio_engine_stub_loop_active[CTRL_DECK_1]);

    deck_core_test_apply_event(&stop);
    assert(!audio_engine_stub_loop_active[CTRL_DECK_1]);
    assert(audio_engine_stub_loop_clear_count[CTRL_DECK_1] == 1);

    deck_core_test_apply_event(&reloop);
    assert(!audio_engine_stub_loop_active[CTRL_DECK_1]);
}

static void test_loop_adjust_in_and_out_update_active_loop_boundaries(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    audio_engine_stub_loop_active[CTRL_DECK_2] = true;
    audio_engine_stub_loop_start_ms[CTRL_DECK_2] = 1000;
    audio_engine_stub_loop_end_ms[CTRL_DECK_2] = 5000;

    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 2000;
    ctrl_event_t adjust_in = deck_ext_action(CTRL_DECK_2, CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN, true);
    deck_core_test_apply_event(&adjust_in);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_2] == 2000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_2] == 5000);

    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 7000;
    ctrl_event_t adjust_out = deck_ext_action(CTRL_DECK_2, CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT, true);
    deck_core_test_apply_event(&adjust_out);
    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_2] == 2000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_2] == 7000);
}

static void test_quantized_loop_in_out_snaps_to_nearest_beat(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    static anlz_metadata_t meta;
    meta = beat_jump_meta();
    s_loaded_anlz[CTRL_DECK_1] = &meta;

    ctrl_event_t quantize = deck_ext_action(CTRL_DECK_1, CTRL_DECK_EXT_ACTION_QUANTIZE, true);
    deck_core_test_apply_event(&quantize);

    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 1850;
    ctrl_event_t loop_in = deck_button(CTRL_ID_DECK1_LOOP_IN);
    deck_core_test_apply_event(&loop_in);

    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 4230;
    ctrl_event_t loop_out = deck_button(CTRL_ID_DECK1_LOOP_OUT);
    deck_core_test_apply_event(&loop_out);

    assert(audio_engine_stub_loop_start_ms[CTRL_DECK_1] == 2000);
    assert(audio_engine_stub_loop_end_ms[CTRL_DECK_1] == 4000);
}
```

Call them from `main()`.

- [ ] **Step 2: Run deck_core test and verify it fails**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\deck_core_dual clean all
```

Expected: compile failure for `quantize_enabled` or behavior failure.

- [ ] **Step 3: Add quantize deck state**

In `deck_core.h`, add to `deck_state_t`:

```c
    bool          quantize_enabled;
```

No explicit initialization is needed beyond existing zeroed `init_deck_state()`.

- [ ] **Step 4: Add quantize and loop helper functions**

In `deck_core.c`, add:

```c
static uint32_t nearest_beat_ms(uint8_t deck, uint32_t position_ms)
{
    const anlz_metadata_t *meta = loaded_anlz_for_deck(deck);
    if (!meta || !meta->beats || meta->beat_count == 0) {
        return position_ms;
    }

    uint32_t best_ms = meta->beats[0].time_ms;
    uint32_t best_delta = best_ms > position_ms ? best_ms - position_ms : position_ms - best_ms;
    for (uint16_t i = 1; i < meta->beat_count; i++) {
        uint32_t beat_ms = meta->beats[i].time_ms;
        uint32_t delta = beat_ms > position_ms ? beat_ms - position_ms : position_ms - beat_ms;
        if (delta < best_delta) {
            best_delta = delta;
            best_ms = beat_ms;
        }
    }
    return best_ms;
}

static uint32_t quantized_deck_position_ms(uint8_t deck, const deck_state_t *state)
{
    uint32_t position_ms = current_deck_position_ms(deck, state);
    if (!state || !state->quantize_enabled) {
        return position_ms;
    }
    return nearest_beat_ms(deck, position_ms);
}

static void stop_and_forget_loop(uint8_t deck)
{
    if (deck >= DECK_CORE_DECK_COUNT) {
        return;
    }
    (void)audio_engine_deck_clear_loop(deck);
    memset(&s_loop_shadow[deck], 0, sizeof(s_loop_shadow[deck]));
    memset(&s_shifted_loop_roll[deck], 0, sizeof(s_shifted_loop_roll[deck]));
    memset(&s_beat_loop_led[deck], 0, sizeof(s_beat_loop_led[deck]));
    ESP_LOGI(TAG, "deck %u loop stop", (unsigned)deck + 1);
    publish_flx4_led_snapshot(false);
}

static void adjust_loop_boundary(uint8_t deck, bool adjust_in, deck_state_t *state)
{
    bool active = false;
    uint32_t start_ms = 0;
    uint32_t end_ms = 0;
    if (!read_active_loop(deck, &active, &start_ms, &end_ms) || !active) {
        return;
    }

    uint32_t position_ms = quantized_deck_position_ms(deck, state);
    if (adjust_in) {
        if (position_ms < end_ms) {
            set_deck_loop(deck, position_ms, end_ms);
        }
    } else if (position_ms > start_ms) {
        set_deck_loop(deck, start_ms, position_ms);
    }
}
```

- [ ] **Step 5: Apply quantize to Loop In/Out**

In `on_loop_control()`, replace:

```c
    uint32_t position_ms = current_deck_position_ms(deck, state);
```

with:

```c
    uint32_t position_ms = quantized_deck_position_ms(deck, state);
```

- [ ] **Step 6: Add ext action handler**

Add:

```c
static bool on_deck_ext_action(const ctrl_event_t *ev, uint8_t deck, deck_state_t *state)
{
    if (!ev || deck >= DECK_CORE_DECK_COUNT || !state ||
        control_link_id_control(ev->id) != CTRL_DECK_CTL_EXT_ACTION) {
        return false;
    }

    uint8_t action = CTRL_DECK_EXT_ACTION(ev->value);
    bool pressed = CTRL_DECK_EXT_PRESSED(ev->value);
    if (!pressed && action != CTRL_DECK_EXT_ACTION_CENSOR) {
        return true;
    }

    switch (action) {
    case CTRL_DECK_EXT_ACTION_SYNC_MASTER:
        set_sync_master(deck, state);
        return true;
    case CTRL_DECK_EXT_ACTION_RELOOP_STOP:
        stop_and_forget_loop(deck);
        return true;
    case CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN:
        adjust_loop_boundary(deck, true, state);
        return true;
    case CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT:
        adjust_loop_boundary(deck, false, state);
        return true;
    case CTRL_DECK_EXT_ACTION_QUANTIZE:
        state->quantize_enabled = !state->quantize_enabled;
        ESP_LOGI(TAG, "deck %u quantize -> %s",
                 (unsigned)deck + 1,
                 state->quantize_enabled ? "ON" : "OFF");
        return true;
    case CTRL_DECK_EXT_ACTION_CENSOR:
        return false;
    default:
        return true;
    }
}
```

Call it before `on_deck_extension_button()` in both runtime and PC-test paths:

```c
        if (on_deck_ext_action(&ev, deck, &s_decks[normalize_deck(deck)])) {
            continue;
        }
```

Use `return;` in the PC-test function.

- [ ] **Step 7: Run deck_core test**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\deck_core_dual clean all
```

Expected: `deck_core_dual tests passed`.

- [ ] **Step 8: Commit**

```powershell
git add firmware/main-deck-p4/components/deck_core/include/deck_core.h firmware/main-deck-p4/components/deck_core/deck_core.c tests/deck_core_dual/test_deck_core_dual.c
git commit -m "feat: implement loop adjust and quantize controls"
```

---

### Task 6: Play+Shift Censor MVP

**Files:**
- Modify: `firmware/main-deck-p4/components/deck_core/include/deck_core.h`
- Modify: `firmware/main-deck-p4/components/deck_core/deck_core.c`
- Modify: `tests/deck_core_dual/test_deck_core_dual.c`

- [ ] **Step 1: Add failing Censor tests**

Add tests:

```c
static void test_censor_press_repeats_previous_audio_window(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    audio_engine_stub_deck_playing[CTRL_DECK_1] = true;
    audio_engine_stub_deck_position_ms[CTRL_DECK_1] = 5000;

    ctrl_event_t press = deck_ext_action(CTRL_DECK_1, CTRL_DECK_EXT_ACTION_CENSOR, true);
    deck_core_test_apply_event(&press);

    assert(deck_core_test_get_deck_state(CTRL_DECK_1).censor_active);
    assert(audio_engine_stub_deck_seek_count[CTRL_DECK_1] == 1);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_1] == 4000);
}

static void test_censor_release_returns_to_stored_position_when_paused(void)
{
    deck_core_test_reset();
    reset_audio_engine_stub();
    audio_engine_stub_deck_playing[CTRL_DECK_2] = false;
    audio_engine_stub_deck_position_ms[CTRL_DECK_2] = 3000;

    ctrl_event_t press = deck_ext_action(CTRL_DECK_2, CTRL_DECK_EXT_ACTION_CENSOR, true);
    ctrl_event_t release = deck_ext_action(CTRL_DECK_2, CTRL_DECK_EXT_ACTION_CENSOR, false);
    deck_core_test_apply_event(&press);
    deck_core_test_apply_event(&release);

    assert(!deck_core_test_get_deck_state(CTRL_DECK_2).censor_active);
    assert(audio_engine_stub_deck_position_ms[CTRL_DECK_2] == 3000);
}
```

Call them from `main()`.

- [ ] **Step 2: Run deck_core test and verify it fails**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\deck_core_dual clean all
```

Expected: compile failure for `censor_active` or behavior failure.

- [ ] **Step 3: Add Censor state**

In `deck_core.h`, add to `deck_state_t`:

```c
    bool          censor_active;
```

In `deck_core.c`, add:

```c
#define CENSOR_REPEAT_BACK_MS 1000u

typedef struct {
    bool active;
    bool was_playing;
    uint32_t origin_ms;
    TickType_t press_tick;
} deck_censor_shadow_t;

static deck_censor_shadow_t s_censor_shadow[DECK_CORE_DECK_COUNT];
```

Clear `s_censor_shadow` in reset paths.

- [ ] **Step 4: Implement Censor handler**

Add:

```c
static void handle_censor(uint8_t deck, bool pressed, deck_state_t *state)
{
    if (deck >= DECK_CORE_DECK_COUNT || !state) {
        return;
    }

    deck_censor_shadow_t *shadow = &s_censor_shadow[deck];
    if (pressed) {
        if (shadow->active) {
            return;
        }
        uint32_t origin = current_deck_position_ms(deck, state);
        uint32_t repeat = origin > CENSOR_REPEAT_BACK_MS ? origin - CENSOR_REPEAT_BACK_MS : 0u;
        shadow->active = true;
        shadow->was_playing = audio_engine_deck_is_playing(deck);
        shadow->origin_ms = origin;
        shadow->press_tick = xTaskGetTickCount();
        state->censor_active = true;
        if (audio_engine_deck_seek(deck, repeat) == ESP_OK) {
            state->position_ms = repeat;
        }
        ESP_LOGI(TAG, "deck %u censor press -> %lu ms",
                 (unsigned)deck + 1,
                 (unsigned long)repeat);
        return;
    }

    if (!shadow->active) {
        return;
    }

    uint32_t target = shadow->origin_ms;
    if (shadow->was_playing) {
        TickType_t elapsed_ticks = xTaskGetTickCount() - shadow->press_tick;
        uint32_t elapsed_ms = (uint32_t)(elapsed_ticks * portTICK_PERIOD_MS);
        target += elapsed_ms;
    }
    if (audio_engine_deck_seek(deck, target) == ESP_OK) {
        state->position_ms = target;
    }
    state->censor_active = false;
    memset(shadow, 0, sizeof(*shadow));
    ESP_LOGI(TAG, "deck %u censor release -> %lu ms",
             (unsigned)deck + 1,
             (unsigned long)target);
}
```

In `on_deck_ext_action()`, route Censor before the non-press release filter:

```c
    if (action == CTRL_DECK_EXT_ACTION_CENSOR) {
        handle_censor(deck, pressed, state);
        return true;
    }
```

- [ ] **Step 5: Run deck_core test**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
make -C tests\deck_core_dual clean all
```

Expected: `deck_core_dual tests passed`.

- [ ] **Step 6: Commit**

```powershell
git add firmware/main-deck-p4/components/deck_core/include/deck_core.h firmware/main-deck-p4/components/deck_core/deck_core.c tests/deck_core_dual/test_deck_core_dual.c
git commit -m "feat: implement flx4 censor control"
```

---

### Task 7: Documentation

**Files:**
- Modify: `docs/DDJ_FLX4_MIDI_MAP.md`
- Modify: `docs/CONTROL_LINK_PROTOCOL.md`
- Modify: `docs/DEVELOPMENT_PLAN.md`
- Modify: `docs/STARTUP_CHECKLIST.md`

- [ ] **Step 1: Update MIDI map statuses**

In `docs/DDJ_FLX4_MIDI_MAP.md`, change the affected rows:

```markdown
| Browse + Shift rotate | `0xB6/0x64` | relative encoder | shifted global | `CTRL_ID_BROWSE_SHIFT_DELTA` | UI Library / Overview accelerated navigation | Implemented | Host-tested from XML; hardware smoke pending |
| Browse + Shift press | `0x96/0x42` | press/release | shifted global | `CTRL_ID_BROWSE_SHIFT_PRESS` | UI Library force-open | Implemented | Host-tested from XML; hardware smoke pending |
| Play + Shift / Censor | `0x90/0x0E`, `0x91/0x0E` | press/release | shifted deck-local | `CTRL_ID_DECK*_EXT_ACTION` / `CTRL_DECK_EXT_ACTION_CENSOR` | `deck_core` slip-censor MVP | Implemented MVP | Host-tested from XML; hardware smoke pending |
| Beat Sync long press / master | `0x90/0x5C`, `0x91/0x5C` | press/release or long-press semantic | deck-local | `CTRL_ID_DECK*_EXT_ACTION` / `CTRL_DECK_EXT_ACTION_SYNC_MASTER` | beat/sync model | Implemented | Host-tested from XML; hardware smoke pending |
| Reloop/Exit + Shift | `0x90/0x50`, `0x91/0x50` | press/release | shifted deck-local | `CTRL_ID_DECK*_EXT_ACTION` / `CTRL_DECK_EXT_ACTION_RELOOP_STOP` | `deck_core` loop stop/forget | Implemented | Host-tested from XML; hardware smoke pending |
| Shift + Loop In adjust | `0x90/0x4C`, `0x91/0x4C` | press/release | shifted deck-local | `CTRL_ID_DECK*_EXT_ACTION` / `CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN` | loop boundary edit | Implemented | Host-tested from XML; hardware smoke pending |
| Shift + Loop Out adjust | XML: `0x90/0x4E`, `0x91/0x4E`; official list input says D2 `0x91/0x4F` but official output says D2 `0x91/0x4E` | press/release | shifted deck-local | `CTRL_ID_DECK*_EXT_ACTION` / `CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT` | loop boundary edit | Implemented from XML `0x4E` | Host-tested from XML; D2 `0x4E`/`0x4F` smoke pending |
| Shift + channel CUE / quantize | `0x90/0x68`, `0x91/0x68` | press/release | shifted deck-local | `CTRL_ID_DECK*_EXT_ACTION` / `CTRL_DECK_EXT_ACTION_QUANTIZE` | deck quantize state | Implemented for loop in/out/adjust | Host-tested from XML; hardware smoke pending |
```

- [ ] **Step 2: Update protocol docs**

In `docs/CONTROL_LINK_PROTOCOL.md`, add under MVP Semantic IDs:

```markdown
| `0x2C` | Deck 1 extended action | packed `CTRL_DECK_EXT_ACTION_*` plus press bit; Censor, Sync Master, Reloop Stop, Loop Adjust In/Out, Quantize |
| `0x4C` | Deck 2 extended action | same packed format as Deck 1 |
| `0x65` | Browse+Shift press | `0` release, `1` press; press forces Library view |
```

Add packed value description:

```markdown
Deck extended action values are packed into the signed 16-bit `value` field:

- bits `0..6`: `CTRL_DECK_EXT_ACTION_*`;
- bit `7`: pressed state.
```

- [ ] **Step 3: Update development/checklist docs**

In `docs/DEVELOPMENT_PLAN.md` and `docs/STARTUP_CHECKLIST.md`, mark this control group as implemented in firmware with hardware smoke pending. Keep Censor described as `slip-censor MVP`, not full reverse playback.

- [ ] **Step 4: Documentation checks**

Run:

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors; only intended docs/firmware/tests files modified.

- [ ] **Step 5: Commit**

```powershell
git add docs/DDJ_FLX4_MIDI_MAP.md docs/CONTROL_LINK_PROTOCOL.md docs/DEVELOPMENT_PLAN.md docs/STARTUP_CHECKLIST.md
git commit -m "docs: describe flx4 shifted extended controls"
```

---

### Task 8: Final Verification

**Files:**
- Verify only.

- [ ] **Step 1: Run S3 host tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_s3_host_tests.ps1
```

Expected: all S3 host tests pass.

- [ ] **Step 2: Run P4 host tests**

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
.\tests\run_p4_host_tests.ps1
```

Expected: all P4 host tests pass.

- [ ] **Step 3: Run firmware builds**

Run S3 build:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd firmware\control-board-s3
idf.py build
```

Run P4 build:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd firmware\main-deck-p4
idf.py build
```

Expected: both builds pass.

- [ ] **Step 4: Final git hygiene**

Run from repo root:

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors; no generated `build/`, `managed_components/`, `sdkconfig`, or `dependencies.lock` files staged.

- [ ] **Step 5: Commit final fixups if needed**

If verification required small fixes:

```powershell
git add <fixed-files>
git commit -m "fix: stabilize flx4 shifted extended controls"
```

If no fixes were needed, do not create an empty commit.

---

## Self-Review

- Spec coverage: all requested controls have a task and defined behavior: Browse+Shift press/rotate in Task 3, Beat Sync master in Task 4, Reloop/Exit+Shift stop in Task 5, Loop Adjust In/Out in Task 5, Quantize in Task 5, Play+Shift Censor in Task 6.
- Protocol stability: existing IDs are preserved; the deck namespace gets one packed extension control instead of six new deck controls.
- Test coverage: S3 mapping, shared protocol constants, P4 behavior, host test suites, and both firmware builds are covered.
- Known limitation: Censor is an MVP slip-censor/repeat behavior, not true reverse MP3 playback. Full reverse playback requires a separate audio-engine design.
