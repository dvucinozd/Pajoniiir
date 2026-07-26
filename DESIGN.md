# DESIGN.md - Pajoniiir Design & Visual Identity

This document serves as the single source of truth for the visual design system, UI layout architecture, and design implementation guidelines for the Pajoniiir standalone dual-deck DJ system.

---

## 1. Product Purpose & Shape

**Pajoniiir** is a standalone two-deck DJ player.
- **Operator Surface**: Pioneer DDJ-FLX4 controller.
- **Control Translation**: ESP32-S3 control board (`firmware/control-board-s3`) acts as a USB MIDI host and translates physical controller actions to semantic event frames (UART `control_link` protocol over `0xA5` sync).
- **Core Engine & Display**: ESP32-P4 JC4880P443C_I_W multimedia board (`firmware/main-deck-p4`) owns the authoritative playback state, audio engine (MP3 decode, I2S output, mixer), library parsing, and the graphical user interface.

### UI Architecture
The screen layout is organized around the native dual-deck state of a DJ performance:
- A landscape **800x480 canvas** rendered by LVGL.
- Rotated in hardware to a **480x800 ST7701S portrait panel** via the ESP32-P4 Pixel Processing Accelerator (PPA).
- A top header status bar (46px) and footer navigation bar (active screen selector).
- **Native Shape**: Organized as dual horizontal deck strips, a central beat-matching guide, and a scrolling library track catalog.

---

## 2. Visual Identity & Design System

The visual identity inherits the Pioneered/XDJ style dark theme. Ad-hoc colors and custom fonts must not be created unless explicitly specified by the system.

### Color Tokens (`ui_theme.h`)

| Token | Hex Value | Purpose |
| --- | --- | --- |
| **`COL_BG`** / **`COL_FOOTER`** | `#000000` | Root background & top/bottom navigation panels |
| **`COL_PANEL`** | `#32323C` | Pioneered panel, container header, and card fill grey |
| **`COL_SURFACE`** | `#050505` | Inactive tab and control fill |
| **`COL_TITLE_BLUE`** / **`COL_ACCENT_DK`** | `#112F5C` | Deck title strip background |
| **`COL_BORDER`** | `#32323C` | Default card/panel border |
| **`COL_BORDER_LT`** | `#5F5F5F` | Active or highlighted control border |
| **`COL_TEXT`** | `#E5E6EA` | Primary high-contrast text |
| **`COL_TEXT_MUTED`** | `#B8BAC2` | Secondary text, descriptors |
| **`COL_TEXT_DIM`** | `#7F838C` | Tertiary text, helper hints, inactive values |
| **`COL_ON_ACCENT`** | `#000000` | Text overlay on bright accent components |
| **`COL_ACCENT`** | `#2D85CD` | Pioneered waveform blue / UI active accent |
| **`COL_TAB_ACTIVE`** | `#C3D541` | Active navigation tab highlight |
| **`COL_GREEN`** | `#6EE128` | Play status, active loops |
| **`COL_AMBER`** | `#EB870F` | Cue status, paused state |
| **`COL_RED`** | `#D73535` | Waveform playhead line, clipper warnings, critical alerts |
| **`COL_PANEL_DK`** | `#050505` | Dense background areas (e.g. settings list) |

### Typography

- **Custom Brand Fonts**:
  - `Musieer_48` & `Musieer_80` (Musieer font): Used for high-visibility UI items like the splash screen logo, BPM indicators, fixed-segment timers, and diagnostics.
- **System Fonts**:
  - Montserrat (`lv_font_montserrat_*`): Used for secondary labels, title bars, and normal text details.

---

## 3. UI Screen Organization

The interface consists of a tabbed structure containing 7 screens:

1. **OVERVIEW**: Dual-deck deck strips, centered beat/phase indicator guides, zoomable waveform displays, and active deck status details.
2. **LIBRARY**: Track browser listing Rekordbox media catalogs, folders, track lists, and track loading actions.
3. **HOT CUES**: Grid of 8 performance pads per deck showing cue slot storage, positions, and deletion mappings.
4. **LOOP**: Manual and auto loop configuration, beat length indicators.
5. **BEAT JUMP**: Navigation pads to jump forwards/backwards in configured beat steps.
6. **KEY SHIFT**: Controls to adjust deck-local key offsets.
7. **SETTINGS**: Hardware mixer levels, master trim cycling (`0 dB`, `-3 dB`, `-6 dB` saved to NVS), output sample rates, and diagnostics.

---

## 4. Design Guidelines & Implementation Rules

### Rule 1: Real Content Only (No Placeholders)
- Never use mockup text, generic labels, or simulated parameters.
- Pull text, metadata, copy, and product names directly from [README.md](file:///c:/Users/klikn/Documents/DDJ-FLX4-ESP32/ESP32-DDJ-FLX4/README.md) or related project documents.
- Use actual system terms and configurations from source code headers (e.g. `CONFIG_BSP_PCM5102A_MAIN_OUT`, `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4`, `CLIP n`, `control_link`).

### Rule 2: Strict Visual System Inheritance
- All panels and widgets must utilize the styles defined in [ui.c](file:///c:/Users/klikn/Documents/DDJ-FLX4-ESP32/ESP32-DDJ-FLX4/firmware/main-deck-p4/components/ui/ui.c) (e.g. `s_style_root`, `s_style_panel_frame`, `s_style_btn_primary`).
- Never define custom inline color codes. Always map to `COL_*` tokens in [ui_theme.h](file:///c:/Users/klikn/Documents/DDJ-FLX4-ESP32/ESP32-DDJ-FLX4/firmware/main-deck-p4/components/ui/ui_theme.h).

### Rule 3: Native Shape Layout
- Do not organize screens into generic block grids.
- Let the application's core logic dictate the layout:
  - DJ performance is about timelines and matching -> the Overview screen is dominated by the horizontal waveform grids and the vertical beat-alignment guides.
  - Music curation is catalog-driven -> the Library screen is structured as an interactive list view.
  - Controls are deck-local -> performance tabs (Hot Cues, Loops, Beat Jump) must clearly separate Deck 1 and Deck 2 properties.

### Rule 4: Feature-Driven Visual Weight
- The primary feature of the standalone player (reproducing, mixing, and visual sync of tracks) has the highest visual weight.
- The active track waveforms, playing state indicators (Play/Cue green/amber colors), and the BPM/pitch guide must be immediately readable from a distance under performance lighting.
