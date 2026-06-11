#pragma once
//
// ui_theme.h - centralised colour palette for the DJ UI.
//
// Single source of truth for the theme chrome colours (backgrounds, text,
// borders, primary accents). The current palette follows the Pioneered/XDJ
// reference skin used as the visual target for the P4 UI.
//
// NOTE: feature-specific accent colours (per-hot-cue palette, waveform greens,
// beat-jump reds) stay inline at their use sites — they are intentionally
// distinct and not part of the shared theme.
//
// Requires lvgl.h to be included before this header (for lv_color_hex).

// -- Backgrounds / surfaces ---------------------------------------------------
#define COL_BG          lv_color_hex(0x000000)  // screen / root background
#define COL_FOOTER      lv_color_hex(0x000000)  // top navigation bar
#define COL_PANEL       lv_color_hex(0x32323C)  // Pioneered panel/header grey
#define COL_SURFACE     lv_color_hex(0x050505)  // inactive tab / control fill
#define COL_TITLE_BLUE  lv_color_hex(0x112F5C)  // deck title strip

// -- Borders ------------------------------------------------------------------
#define COL_BORDER      lv_color_hex(0x32323C)  // default panel/card border
#define COL_BORDER_LT   lv_color_hex(0x5F5F5F)  // active/hover-style border

// -- Text ---------------------------------------------------------------------
#define COL_TEXT        lv_color_hex(0xE5E6EA)  // primary text
#define COL_TEXT_MUTED  lv_color_hex(0xB8BAC2)  // secondary text
#define COL_TEXT_DIM    lv_color_hex(0x7F838C)  // tertiary / hint text
#define COL_ON_ACCENT   lv_color_hex(0x000000)  // text on bright accent buttons

// -- Accents ------------------------------------------------------------------
#define COL_ACCENT      lv_color_hex(0x2D85CD)  // Pioneered waveform/UI blue
#define COL_ACCENT_DK   lv_color_hex(0x112F5C)  // deck title blue
#define COL_TAB_ACTIVE  lv_color_hex(0xC3D541)  // active top tab
#define COL_GREEN       lv_color_hex(0x6EE128)  // play / loop
#define COL_AMBER       lv_color_hex(0xEB870F)  // cue / paused
#define COL_RED         lv_color_hex(0xD73535)  // playhead / alerts
#define COL_PANEL_DK    lv_color_hex(0x050505)  // dense work panel background
#define COL_TABLE_ROW   lv_color_hex(0x000000)  // library row surface
#define COL_TABLE_ALT   lv_color_hex(0xE5E6EA)  // selected row surface
#define COL_DISABLED    lv_color_hex(0x32323C)  // disabled action fill
