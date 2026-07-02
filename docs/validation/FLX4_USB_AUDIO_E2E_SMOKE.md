# FLX4 USB Audio End-to-End Smoke

Date: 2026-07-02
Branch: `codex/flx4-usb-audio-headphones`
Boards: P4 on COM15, S3 on COM3, DDJ-FLX4 on the S3 USB host.

Full path proven: P4 `audio_engine` cue/monitor mix (`hp_out`) -> `monitor_pcm_link`
I2S TX -> inter-board I2S -> S3 `p4_audio_link` ring -> `flx4_usb_audio` UAC OUT
transfers -> DDJ-FLX4 headphones jack.

## Step 1 — cue audible in FLX4 headphones (PASS)

Config: ES8311 paces on I2S unit 0, monitor link on unit 1, PCM5102A off
(`sdkconfig.flx4_hp_e2e` Step 1 topology, commit `0c06516`).

| Check | Result |
| --- | --- |
| Deck cue audible in FLX4 headphones | PASS (confirmed by operator) |
| PFL / Headphones Mix follow P4 monitor DSP | PASS |
| FLX4 MIDI controls responsive while streaming | PASS |
| Sample-rate match (44.1 / 48 kHz tracks) | Ring autostart matches FLX4 endpoint rate to the P4 output rate |
| S3 / P4 stability | No reboot |

## Step 2 — RCA MAIN + FLX4 cue simultaneously (PASS)

Config (product topology, commit `c9bf679`): PCM5102A RCA MAIN on I2S unit 1
(paces the output loop), monitor link on unit 0, ES8311 disabled
(`CONFIG_BSP_ES8311_MONITOR=n`). Physical link wiring unchanged from Step 1;
only the internal I2S unit moved.

| Check | Result |
| --- | --- |
| MAIN mix on PCM5102A RCA | PASS (confirmed by operator) |
| Cue on FLX4 headphones, simultaneously | PASS |
| PFL / Headphones Mix behaviour unchanged from Step 1 | PASS |
| No audible stutter (loop paces on the PCM5102A blocking write) | PASS |
| Dual-deck playback stability | No reboot on either board |

## Result

FLX4 USB headphones (CUE/MONITOR) and PCM5102A RCA (MAIN OUT) run at the same
time from the two usable P4 I2S units. The eco2 I2S-unit-2 freeze is avoided;
ES8311 onboard monitor is dropped in the product build (not needed as a
simultaneous fallback per the 2026-07-02 decision). The end-to-end acceptance
criteria in the plan are met.
