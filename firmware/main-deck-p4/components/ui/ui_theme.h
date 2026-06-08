#pragma once
//
// ui_theme.h — centralised colour palette for the DJ UI.
//
// Single source of truth for the theme "chrome" colours (backgrounds, text,
// borders, primary accents). Values match the previous inline hexes, so this is
// a pure refactor — tweak a token here to restyle consistently everywhere.
//
// NOTE: feature-specific accent colours (per-hot-cue palette, waveform greens,
// beat-jump reds) stay inline at their use sites — they are intentionally
// distinct and not part of the shared theme.
//
// Requires lvgl.h to be included before this header (for lv_color_hex).

// ── Backgrounds / surfaces ───────────────────────────────────────────────────
#define COL_BG          lv_color_hex(0x0A0A0A)  // screen / root background
#define COL_FOOTER      lv_color_hex(0x000000)  // footer navigation bar (pure black)
#define COL_PANEL       lv_color_hex(0x141414)  // header bar, info panels
#define COL_SURFACE     lv_color_hex(0x1F1F1F)  // cards, tiles, inactive tab

// ── Borders ──────────────────────────────────────────────────────────────────
#define COL_BORDER      lv_color_hex(0x222222)  // default panel/card border
#define COL_BORDER_LT   lv_color_hex(0x333333)  // lighter border / dividers

// ── Text ─────────────────────────────────────────────────────────────────────
#define COL_TEXT        lv_color_hex(0xFFFFFF)  // primary text
#define COL_TEXT_MUTED  lv_color_hex(0xB0B0B0)  // secondary text
#define COL_TEXT_DIM    lv_color_hex(0x888888)  // tertiary / hint text
#define COL_ON_ACCENT   lv_color_hex(0x000000)  // text on bright accent buttons

// ── Accents ──────────────────────────────────────────────────────────────────
#define COL_ACCENT      lv_color_hex(0x00A3FF)  // primary blue (time, active highlight)
#define COL_ACCENT_DK   lv_color_hex(0x005BB5)  // active tab fill
#define COL_GREEN       lv_color_hex(0x00E676)  // positive / play / pitch
#define COL_AMBER       lv_color_hex(0xFFAB00)  // cue / paused / waiting state
#define COL_RED         lv_color_hex(0xFF1744)  // errors and playhead emphasis
#define COL_PANEL_DK    lv_color_hex(0x0F1114)  // dense work panel background
#define COL_TABLE_ROW   lv_color_hex(0x121417)  // library row surface
#define COL_TABLE_ALT   lv_color_hex(0x171B20)  // subtle selected/active surface
#define COL_DISABLED    lv_color_hex(0x2B3036)  // disabled action fill
