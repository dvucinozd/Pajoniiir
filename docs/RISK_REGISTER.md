# Risk Register

| Risk | Impact | Mitigation |
| --- | --- | --- |
| S3 USB host cannot reliably power or enumerate DDJ-FLX4 | Blocks controller input | Firmware raw logger boots, but 2026-06-08 capture saw no enumeration; next validation must verify S3 OTG port, powered hub orientation, 5 V VBUS, and shared ground |
| Mixxx XML differs from actual hardware messages | Wrong mappings and unstable controls | Keep S3 default firmware in raw logger mode; verify every MVP control with hardware capture before enabling translator mode by default |
| Analog controls generate noisy high-rate events | UART queue pressure and jitter | S3 translator coalesces high-rate jog/tempo/fader values; legacy panel path accumulates pending jog/browse motion and caps compatibility MIDI bursts |
| Existing 7-byte control frame becomes too tight | Protocol churn | Use deck-aware ID namespace for MVP; only version frame if a real blocker appears |
| Dual MP3 decode exceeds CPU or memory budget | Audio dropouts/watchdog resets | P4 now has per-deck producer/mixer plumbing; continue measuring with two real tracks before adding cue output or heavier UI work |
| Dual output clocking or I2S routing is unstable | Cue/master output not usable | Master output path is first; keep cue/PFL audio state-only until the mixer is stable on hardware |
| ES8311/speaker amplifier path is unsafe for headphone/line output | Hardware damage | Inspect schematic and bench-measure before wiring headphones or RCA |
| LED feedback mapping requires script behavior not present in XML controls | FLX4 LEDs may not respond as expected | Inspect matching Mixxx JS script or capture MIDI output from Mixxx; implement only verified LEDs |
| Live Overview waveform rendering regresses UI fluidity | Jitter or visible stalls while mixing | Keep expensive redraws behind `ui_overview_scheduler`; Deck 2 lower waveform uses the normal LVGL invalidation/flush path after the 2026-06-13 jitter fix; use host scheduler tests plus COM15 smoke logs after renderer changes |
| Upstream code changes are mixed with DDJ-specific changes without notes | Hard to reason about port | Keep DDJ docs explicit and update development plan after each phase |
