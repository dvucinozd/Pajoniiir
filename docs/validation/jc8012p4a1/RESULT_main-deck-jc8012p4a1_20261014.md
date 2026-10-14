# JC8012P4A1 result, group A (display and boot), 2026-10-14

Image: `main-deck-jc8012p4a1` built from `jc8012p4a1-v3` (IDF 6.0.2 container), flashed through the CH340 port.
Observations by the operator on the unit; no photos yet.

| # | Check | Result |
| --- | --- | --- |
| A1 | Boots without panic | pass for the 10 s read (no panic); `board_id` line not checked |
| A2 | Landscape orientation (rotation 270), colours | pass |
| A3 | Backlight dims and restores | pass after the fix: first run failed (GPIO on/off, the LEDC driver faults on chip v3.2); the backlight now uses the MCPWM at 25 kHz and the dimmer works |
| A4 | UI layout at 1280x800 | pass (overview, library, settings: no cut-off or overlap reported) |
| A5 | Touch | pass with `swap_xy` only; the GSL3680 driver works |

Touch flags tried (rotation 270): swap_xy + mirror_x inverted; swap_xy + mirror_y mirrored left/right; all three rotated 180 degrees; **swap_xy alone is correct**.

Boot log notes: SD mount fails 3 times with 0x107 (no card inserted, not E9); `usb_host_mgr: recovering USB1 reason=1` loops with nothing plugged in (8 fast cycles, then every 30 s).
