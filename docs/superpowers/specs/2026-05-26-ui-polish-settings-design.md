# UI Polish: Settings

Document status (2026-07-13): historical and partially superseded. Current
Settings includes Wi-Fi/Debug AP, diagnostics and OTA-related firmware status.

Date: 2026-05-26
Status: Historical design; superseded for CDJ Link role controls

Superseded note, 2026-06-18: the Settings `link_mode` control, saved Wi-Fi
role cycle, and `JOIN PLAYER` user flow were removed from active DDJ-FFL4
firmware. P4 now starts the hosted web UI/captive portal AP directly. Treat the
CDJ Link role-control portions below as historical context only.

## Scope

This polish pass targets only the `SETTINGS` tab in the ESP32-P4 main deck UI.
The approved visual direction is option A from the browser mockups:
an operational two-column layout with controls on the left and live status on
the right.

This pass keeps all existing Settings behavior. It reorganizes the screen and
updates presentation only.

Out of scope:

- Changing NVS setting keys or persistence behavior.
- Changing Wi-Fi/ESP-Hosted startup logic.
- Changing CDJ Link discovery, cache, server or client behavior.
- Changing audio output routing behavior.
- Adding new diagnostics not already represented by the current UI.
- Polishing Beat Jump, Key Shift or footer navigation.

## Goals

- Make Settings readable at a glance on the 800x480 display.
- Separate operator controls from passive status information.
- Make CDJ Link mode and status clearer for the remote USB workflow.
- Keep touch targets large enough for hardware use.
- Reuse the restrained dark utility visual language from Overview, Library,
  Hot Cues and Loop.
- Preserve all existing callbacks and settings state.

## Layout Design

The screen becomes two columns inside the existing `SETTINGS` tab area.

Left column: operator controls.

- `DISPLAY`
  - Brightness label.
  - Existing brightness slider.
  - Current percentage value aligned near the slider.
- `AUDIO OUTPUT`
  - Existing speaker/RCA switch.
  - Current output label: `SPEAKER` or `RCA LINE-OUT`.
- `CDJ LINK ROLE`
  - Existing mode cycle button for `OFF`, `HOST USB`, and `JOIN PLAYER`.
  - Short helper/status text indicating that the saved Wi-Fi role applies on
    reboot.

Right column: live status.

- `SYSTEM STATUS`
  - S3 control link status.
  - CDJ Link live status.
  - USB media drive summary.
  - SD cache summary.
  - Board and firmware metadata as dim secondary text near the bottom.

The right column should read as one status surface, not as several nested cards.
The left column can use simple framed control sections, but should avoid
decorative nesting.

## Visual Language

Use existing theme tokens and shared styles:

- `COL_PANEL`, `COL_PANEL_DK`, `COL_SURFACE`, `COL_BORDER`,
  `COL_BORDER_LT`
- `COL_TEXT`, `COL_TEXT_MUTED`, `COL_TEXT_DIM`
- `COL_ACCENT`, `COL_GREEN`, `COL_AMBER`, `COL_RED`
- `s_style_panel_frame`, `s_style_btn_primary`, `s_style_btn_secondary`,
  `s_style_pressed`

Expected state color language:

- Green: healthy/active hardware status and current speaker/RCA state.
- Blue: primary interactive CDJ Link mode action and active link information.
- Amber: saved-but-reboot-required helper state.
- Dim text: static board, firmware and storage detail.

The Settings screen should feel like an operational configuration surface, not a
landing page or decorative dashboard.

## Component Boundaries

Implementation should stay inside:

- `firmware/main-deck-p4/components/ui/ui.c`

`ui_theme.h` should not need changes unless implementation finds a genuinely
reusable theme token missing from the current palette.

Recommended approach:

- Add small local helpers for repeated Settings section headers or framed
  control sections only if they reduce duplication.
- Keep the existing object handles:
  `s_slider_backlight`, `s_label_brightness_val`, `s_label_uart_status`,
  `s_label_audio_out`, `s_label_link_status`, and `s_label_link_mode`.
- Keep existing callbacks:
  `slider_brightness_event_cb`, `audio_out_event_cb`, and
  `link_mode_event_cb`.
- Keep `ui_update_link_status_label()` as the live CDJ Link status updater.

## Data Flow

Brightness:

- The slider continues reading initial value from `app_settings_get()`.
- Slider changes continue calling `bsp_display_set_backlight()` and
  `app_settings_set_backlight()`.

Audio output:

- The switch continues reading initial value from `app_settings_get()`.
- Switch changes continue calling `bsp_codec_set_pa()` and
  `app_settings_set_audio_out()`.

CDJ Link:

- The mode button continues cycling the saved mode through
  `link_mode_event_cb()`.
- `s_label_link_mode` shows the selected saved role.
- `s_label_link_status` continues being updated by `ui_update_link_status_label()`
  on firmware.

Status:

- S3 status continues using `s_label_uart_status`.
- Existing static USB/SD/board/firmware text may be restyled and split into
  separate labels for readability, but should not imply new runtime monitoring
  that does not exist yet.

## Error And Empty States

- Link mode saved but not yet applied should remain explicit:
  `Link mode saved; reboot applies Wi-Fi role`.
- Link off should remain low-urgency, not an error.
- Join scanning should be readable as a waiting state.
- Static USB/SD summaries must avoid implying exact live capacity if the current
  implementation still uses fixed descriptive text.

## Testing

Minimum verification:

- `idf.py build` from `firmware/main-deck-p4` using the project ESP-IDF
  environment.
- No new compiler warnings from `components/ui/ui.c`.
- `git diff --check` from the repository root.
- Visual/manual inspection of the Settings tab on the in-app browser or hardware
  when practical:
  - No text overlap at 800x480.
  - Brightness slider and audio switch remain reachable.
  - CDJ Link mode button still cycles text.
  - Status labels remain readable.

## Acceptance Criteria

- Settings is clearly split into controls and status.
- Existing Settings behavior is unchanged.
- CDJ Link role and live status are easier to understand.
- All text fits within the Settings tab at 800x480.
- Build passes with no new `ui.c` warnings.
