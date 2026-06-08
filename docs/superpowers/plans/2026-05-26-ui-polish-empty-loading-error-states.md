# UI Polish Empty Loading Error States Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Normalize short empty/loading/busy/success/error messages and status colors across the existing Library, header status, Settings link state and remote cache flow.

**Architecture:** Keep the current UI layout and media behavior intact. Add small local helpers in `firmware/main-deck-p4/components/ui/ui.c` for header status text/color and short-lived status overrides, update the existing Library/Join strings, and rename two user-facing remote cache statuses in `firmware/main-deck-p4/components/remote_cache/remote_cache.c`.

**Tech Stack:** ESP-IDF C firmware, LVGL, existing `components/ui` theme tokens, existing `remote_cache_status()` flow, PowerShell, `idf.py build`.

---

## Files

- Modify: `firmware/main-deck-p4/components/ui/ui.c`
  - Add a small header status helper.
  - Add a cache-status-to-color helper.
  - Add a short status override timer so status messages are not immediately replaced by the periodic `PAUSE`/`PLAYING` update.
  - Update Library hint/load/join/status strings to the approved vocabulary.
  - Keep existing load, join and audio flow unchanged.
- Modify: `firmware/main-deck-p4/components/remote_cache/remote_cache.c`
  - Change user-facing cache status `NO PEER` to `JOIN OFFLINE`.
  - Change user-facing cache completion `READY` to `CACHE READY`.
- Modify: `docs/development-plan.md`
  - Mark Empty/loading/error states polish complete under the UI polish checklist after implementation and verification.

## Validation Commands

Run from `D:\AI\CDJ-XXX\repo` unless the command changes directory.

Patch hygiene:

```powershell
git diff --check
```

Expected result: no output and exit code `0`.

Firmware build:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: exit code `0`, output includes `Project build complete`, and no warnings mention `components/ui/ui.c` or `components/remote_cache/remote_cache.c`.

---

### Task 1: Add Header Status Helpers

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Add status override state**

Near the existing header globals:

```c
static lv_obj_t *s_label_status_indicator = NULL;
```

insert:

```c
static uint32_t  s_status_override_until_ms = 0;
```

so the block becomes:

```c
static lv_obj_t *s_label_status_indicator = NULL;
static uint32_t  s_status_override_until_ms = 0;
```

- [ ] **Step 2: Add helper functions**

After `ui_label_set_small_caps()` and before `ui_library_set_load_busy()`, insert:

```c
static void ui_status_indicator_set(const char *text, lv_color_t color)
{
    if (!s_label_status_indicator) {
        return;
    }
    lv_label_set_text(s_label_status_indicator, text ? text : "LOAD ERR");
    lv_obj_set_style_text_color(s_label_status_indicator, color, LV_PART_MAIN);
}

static void ui_status_indicator_hold(const char *text, lv_color_t color, uint32_t hold_ms)
{
    s_status_override_until_ms = lv_tick_get() + hold_ms;
    ui_status_indicator_set(text, color);
}

static bool ui_status_indicator_has_override(void)
{
    return (int32_t)(s_status_override_until_ms - lv_tick_get()) > 0;
}

static lv_color_t ui_status_color_for_text(const char *status)
{
    if (!status || status[0] == '\0') {
        return COL_RED;
    }
    if (strcmp(status, "HOST BUSY") == 0) {
        return COL_AMBER;
    }
    if (strcmp(status, "JOIN OFFLINE") == 0 ||
        strcmp(status, "JOIN FAILED") == 0 ||
        strcmp(status, "MANIFEST ERR") == 0 ||
        strcmp(status, "DAT ERR") == 0 ||
        strcmp(status, "AUDIO ERR") == 0 ||
        strcmp(status, "LOAD ERR") == 0) {
        return COL_RED;
    }
    if (strcmp(status, "JOINED") == 0 ||
        strcmp(status, "CACHE READY") == 0 ||
        strcmp(status, "TRACK LOADED") == 0) {
        return COL_GREEN;
    }
    if (strcmp(status, "LOADING") == 0 ||
        strcmp(status, "CACHE START") == 0 ||
        strcmp(status, "MANIFEST") == 0 ||
        strcmp(status, "ANLZ0000.DAT") == 0 ||
        strcmp(status, "ANLZ0000.EXT") == 0 ||
        strcmp(status, "audio.mp3") == 0) {
        return COL_ACCENT;
    }
    return COL_TEXT_DIM;
}
```

`ui.c` already includes `<string.h>`, so `strcmp()` is available.

- [ ] **Step 3: Update default Library hint**

Inside `ui_library_set_load_busy()`, replace:

```c
        lv_label_set_text(s_label_library_hint, hint ? hint : "Select row\nthen LOAD");
```

with:

```c
        lv_label_set_text(s_label_library_hint, hint ? hint : "SELECT TRACK\nPRESS LOAD");
```

This keeps the current two-line hint layout while changing only wording and casing.

---

### Task 2: Apply Vocabulary To Library And Join Flow

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Update Settings JOIN scanning text**

Inside `ui_update_link_status_label()`, replace:

```c
            lv_label_set_text(s_label_link_status, "Link: JOIN scanning");
```

with:

```c
            lv_label_set_text(s_label_link_status, "Link: JOIN SCANNING");
```

- [ ] **Step 2: Update duplicate/load-busy hint**

Inside `library_load_event_cb()`, replace:

```c
    if (s_track_load_busy) {
        return;
    }
    s_track_load_busy = true;
    ui_library_set_load_busy(true, "LOAD IN\nPROGRESS");
```

with:

```c
    if (s_track_load_busy) {
        ui_status_indicator_hold("LOAD BUSY", COL_AMBER, 1200);
        return;
    }
    s_track_load_busy = true;
    ui_library_set_load_busy(true, "LOAD BUSY");
```

This does not change the busy guard. It only shows the approved message if a duplicate load event reaches the callback.

- [ ] **Step 3: Update load-start header status**

In the firmware branch of `library_load_event_cb()`, replace:

```c
    lv_label_set_text(s_label_status_indicator,
                      media_catalog_get_source() == MEDIA_SOURCE_REMOTE_LINK ? "CACHING" : "LOADING");
```

with:

```c
    const bool remote_source = (media_catalog_get_source() == MEDIA_SOURCE_REMOTE_LINK);
    ui_status_indicator_hold(remote_source ? "CACHE START" : "LOADING", COL_ACCENT, 1500);
```

This introduces a local `remote_source` variable used by later steps.

- [ ] **Step 4: Update load failure status**

In the same firmware branch, replace the current load failure block:

```c
    if (rc != ESP_OK) {
        const char *status = remote_cache_status();
        lv_label_set_text_fmt(s_label_status_indicator, "%s", status ? status : "LOAD ERR");
        ESP_LOGW(TAG, "media_catalog_load(%d): %s", s_selected_track_idx, esp_err_to_name(rc));
        ui_library_set_load_busy(false, status ? status : "LOAD ERR");
        s_track_load_busy = false;
        return;
    }
```

with:

```c
    if (rc != ESP_OK) {
        const char *status = remote_source ? remote_cache_status() : NULL;
        const char *display = (status && status[0]) ? status : "LOAD ERR";
        ESP_LOGW(TAG, "media_catalog_load(%d): %s", s_selected_track_idx, esp_err_to_name(rc));
        ui_status_indicator_hold(display, ui_status_color_for_text(display), 3500);
        ui_library_set_load_busy(false, display);
        s_track_load_busy = false;
        return;
    }
```

Local load failures now use `LOAD ERR`; remote failures keep the most specific cache status.

- [ ] **Step 5: Preserve source check using the new variable**

Replace:

```c
    if (media_catalog_get_source() == MEDIA_SOURCE_LOCAL_USB) {
        mock_library_load_track_to_deck(s_selected_track_idx);
    }
```

with:

```c
    if (!remote_source) {
        mock_library_load_track_to_deck(s_selected_track_idx);
    }
```

This keeps the same local-only behavior and avoids a second source query.

- [ ] **Step 6: Update successful load message**

At the end of `library_load_event_cb()`, replace:

```c
    ui_library_set_load_busy(false, "TRACK\nLOADED");
```

with:

```c
    ui_status_indicator_hold("TRACK LOADED", COL_GREEN, 2000);
    ui_library_set_load_busy(false, "TRACK LOADED");
```

This uses the approved vocabulary and keeps success visible briefly in the header.

- [ ] **Step 7: Update JOIN source status messages**

Inside `library_source_joined_event_cb()`, replace:

```c
        lv_label_set_text(s_label_status_indicator, "JOINED");
```

with:

```c
        ui_status_indicator_hold("JOINED", COL_GREEN, 2000);
```

Then replace:

```c
        lv_label_set_text_fmt(s_label_status_indicator, "JOIN ERR");
```

with:

```c
        ui_status_indicator_hold("JOIN FAILED", COL_RED, 3500);
```

The join refresh behavior remains unchanged; only the message and color change.

- [ ] **Step 8: Update initial Library hint text**

Inside `create_screen_library()`, replace:

```c
    lv_label_set_text(s_label_library_hint, "Select row\nthen LOAD");
```

with:

```c
    lv_label_set_text(s_label_library_hint, "SELECT TRACK\nPRESS LOAD");
```

`ui_library_set_load_busy(false, NULL);` immediately follows this setup and should remain unchanged.

---

### Task 3: Respect Status Overrides In Periodic Header Updates

**Files:**
- Modify: `firmware/main-deck-p4/components/ui/ui.c`

- [ ] **Step 1: Replace direct status update block in `ui_timer_cb()`**

Inside `ui_timer_cb()`, replace:

```c
    // ─── 2. Update Play / Pause state label ───
    if (ae_loading) {
        lv_label_set_text_fmt(s_label_status_indicator, "LOADING %u%%", (unsigned)ae_load_pct);
        lv_obj_set_style_text_color(s_label_status_indicator, COL_ACCENT, LV_PART_MAIN);
    } else if (state.playing) {
        lv_label_set_text(s_label_status_indicator, "PLAYING");
        lv_obj_set_style_text_color(s_label_status_indicator, COL_GREEN, LV_PART_MAIN);
    } else {
        lv_label_set_text(s_label_status_indicator, "PAUSE");
        lv_obj_set_style_text_color(s_label_status_indicator, lv_color_hex(0xFFAB00), LV_PART_MAIN);
    }
```

with:

```c
    // ─── 2. Update Play / Pause state label ───
    if (ae_loading) {
        s_status_override_until_ms = 0;
        lv_label_set_text_fmt(s_label_status_indicator, "LOADING %u%%", (unsigned)ae_load_pct);
        lv_obj_set_style_text_color(s_label_status_indicator, COL_ACCENT, LV_PART_MAIN);
    } else if (!ui_status_indicator_has_override()) {
        if (state.playing) {
            ui_status_indicator_set("PLAYING", COL_GREEN);
        } else {
            ui_status_indicator_set("PAUSE", COL_AMBER);
        }
    }
```

Audio-engine preload status remains highest priority. Short-lived UI messages remain visible until their hold window expires.

---

### Task 4: Update Remote Cache Status Strings

**Files:**
- Modify: `firmware/main-deck-p4/components/remote_cache/remote_cache.c`

- [ ] **Step 1: Replace no-peer status**

Inside `remote_cache_prepare()`, replace:

```c
        set_status("NO PEER", 0);
```

with:

```c
        set_status("JOIN OFFLINE", 0);
```

- [ ] **Step 2: Replace cache completion status**

Inside `remote_cache_prepare()`, replace:

```c
    set_status("READY", 100);
```

with:

```c
    set_status("CACHE READY", 100);
```

- [ ] **Step 3: Verify old user-facing strings are gone**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
rg -n "\"NO PEER\"|\"READY\"|\"JOIN ERR\"|\"CACHING\"|\"LOAD IN\\nPROGRESS\"|\"Select row\\nthen LOAD\"|\"JOIN scanning\"" firmware/main-deck-p4/components/ui firmware/main-deck-p4/components/remote_cache
```

Expected result: no matches.

- [ ] **Step 4: Build status vocabulary polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: build exits `0`, output includes `Project build complete`, and no warnings mention `components/ui/ui.c` or `components/remote_cache/remote_cache.c`.

- [ ] **Step 5: Commit status vocabulary polish**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add firmware/main-deck-p4/components/ui/ui.c firmware/main-deck-p4/components/remote_cache/remote_cache.c
git commit -m "ui: polish status messages"
```

Expected result: one commit containing only the UI status vocabulary and remote cache status string changes.

---

### Task 5: Documentation And Final Verification

**Files:**
- Modify: `docs/development-plan.md`

- [ ] **Step 1: Update UI polish checklist**

In `docs/development-plan.md`, under the existing UI polish checklist, after the Footer tabs item, add:

```markdown
    - [x] Empty/loading/error states: normalized Library, JOIN and remote cache
      status vocabulary with consistent action, success, warning and error
      colors while preserving existing load/cache behavior.
```

- [ ] **Step 2: Run patch hygiene check**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git diff --check
```

Expected result: no output and exit code `0`.

- [ ] **Step 3: Run final firmware build**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo\firmware\main-deck-p4
$activate = & "C:\Espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe" "C:\Espressif\.espressif\v5.5.4\esp-idf\tools\activate.py" --export | Select-Object -First 1; . $activate; idf.py build
```

Expected result: build exits `0`, output includes `Project build complete`, and no warnings mention `components/ui/ui.c` or `components/remote_cache/remote_cache.c`.

- [ ] **Step 4: Commit documentation update**

Run:

```powershell
cd D:\AI\CDJ-XXX\repo
git add docs/development-plan.md
git commit -m "docs: mark status message polish complete"
```

Expected result: one documentation-only commit.

---

## Manual Visual Check

After the firmware build, inspect on simulator or hardware when practical:

- Library idle hint reads `SELECT TRACK` / `PRESS LOAD`.
- Remote source load start shows `CACHE START` in accent color.
- Join failure shows `JOIN FAILED` in red.
- Host busy or duplicate load shows amber warning text.
- Successful join and track load show green status text briefly.
- Normal playback status returns to `PLAYING` or `PAUSE` after the short status hold expires.

## Self-Review

- Spec coverage: Tasks 1-4 implement the approved vocabulary, header status colors, Library hint updates, JOIN text updates, remote cache status renames and override behavior needed to make transient status messages visible. Task 5 records the completed polish in the project plan.
- Type consistency: The plan uses existing LVGL APIs, existing `COL_*` theme tokens, existing `remote_cache_status()`, existing `s_label_status_indicator` and a new local `s_status_override_until_ms`.
- Verification coverage: The plan includes stale-string search, `git diff --check`, firmware build, compiler warning review and manual visual checks.
