# Risk Register

| Risk | Impact | Mitigation |
| --- | --- | --- |
| S3 USB host power or enumeration regresses | Blocks controller input | Enumeration and a 30-minute stability run passed on 2026-06-14; preserve raw logger diagnostics and recheck OTG port, powered hub orientation, 5 V VBUS, and shared ground after USB host changes |
| An extended Mixxx XML entry differs from actual hardware | Wrong mapping for a non-MVP control | MVP capture matched the XML exactly, so use XML addresses as implementation seeds; hardware-smoke each delivered control group and record any exception in `DDJ_FLX4_MIDI_MAP.md` |
| Mixxx `Script-Binding` names are mistaken for standalone behavior | Incorrect state ownership or controls that behave differently from Mixxx | Use script-bound XML entries only for MIDI addresses; define semantic events explicitly and keep runtime deck/mixer/pad/effect state on the P4 |
| Analog controls generate noisy high-rate events | UART queue pressure and jitter | S3 translator coalesces high-rate jog/tempo/fader values; legacy panel path accumulates pending jog/browse motion and caps compatibility MIDI bursts |
| Existing 7-byte control frame becomes too tight | Protocol churn | Use deck-aware ID namespace for MVP; only version frame if a real blocker appears |
| Dual MP3 decode exceeds CPU or memory budget | Audio dropouts/watchdog resets | P4 now has per-deck producer/mixer plumbing and shared output pacing; continue measuring with two real tracks before adding heavier DSP/UI work |
| Dual output summing clips with loud two-deck material | Audible distortion | Transparent post-sum limiter/soft-clip and telemetry are implemented; monitor limiter activity during hardware smoke tests and add explicit master trim later if sustained limiting is observed |
| MP3 preload/frame-index work briefly blocks audio/UI timing | Waveform or output timing spike | Split or narrow the seek-table/indexing critical section; prior probe showed healthy steady state but one transient block spike around preload completion |
| Dual output clocking or I2S routing is unstable | Cue/master output not usable | Master and cue/PFL routing are implemented for the current path; keep hardware smoke tests before adding heavier DSP |
| ES8311/speaker amplifier path is unsafe for headphone/line output | Hardware damage | Inspect schematic and bench-measure before wiring headphones or RCA |
| LED feedback mapping requires script behavior not present in XML controls | FLX4 LEDs may not respond as expected | MVP Play/Cue/PFL LEDs and reconnect resync are verified; inspect matching Mixxx JS script or capture MIDI output from Mixxx before expanding beyond verified LEDs |
| Live Overview waveform rendering regresses UI fluidity | Jitter or visible stalls while mixing | Keep expensive redraws behind `ui_overview_scheduler`; Deck 2 lower waveform uses the normal LVGL invalidation/flush path after the 2026-06-13 jitter fix; use host scheduler tests plus COM15 smoke logs after renderer changes |
| Upstream code changes are mixed with DDJ-specific changes without notes | Hard to reason about port | Keep DDJ docs explicit and update development plan after each phase |
