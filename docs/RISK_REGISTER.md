# Risk Register

| Risk | Impact | Mitigation |
| --- | --- | --- |
| S3 USB host cannot reliably power or enumerate DDJ-FLX4 | Blocks controller input | Test powered hub and external VBUS early; keep raw MIDI spike as Phase 1 |
| Mixxx XML differs from actual hardware messages | Wrong mappings and unstable controls | Verify every MVP control with raw MIDI capture before implementing behavior |
| Analog controls generate noisy high-rate events | UART queue pressure and jitter | Threshold/debounce on S3; coalesce MSB/LSB; send only meaningful value changes |
| Existing 7-byte control frame becomes too tight | Protocol churn | Use deck-aware ID namespace for MVP; only version frame if a real blocker appears |
| Dual MP3 decode exceeds CPU or memory budget | Audio dropouts/watchdog resets | Measure with two real tracks before adding UI complexity; keep master-only mixer first |
| Dual output clocking or I2S routing is unstable | Cue/master output not usable | Bring up master output first; add cue after mixer is stable |
| ES8311/speaker amplifier path is unsafe for headphone/line output | Hardware damage | Inspect schematic and bench-measure before wiring headphones or RCA |
| LED feedback mapping requires script behavior not present in XML controls | FLX4 LEDs may not respond as expected | Inspect matching Mixxx JS script or capture MIDI output from Mixxx; implement only verified LEDs |
| UI work starts before engine is stable | Wasted effort | Do dual-deck UI after S3 input and P4 audio/mixer path are proven |
| Upstream code changes are mixed with DDJ-specific changes without notes | Hard to reason about port | Keep DDJ docs explicit and update development plan after each phase |
