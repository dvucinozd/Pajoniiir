# Wireless FLX4 over BLE-MIDI — feasibility note

Status: **hard / low-priority development option — not planned.** Recorded
2026-07-10 after investigating whether the ESP32 could talk to the DDJ-FLX4 over
Bluetooth instead of USB.

## The finding

- **The DDJ-FLX4's Bluetooth is Bluetooth Low Energy (BLE) MIDI, control only.**
  Confirmed from the official manual specifications ("Bluetooth® Section —
  Wireless system: Bluetooth Low Energy"), the on-unit "Bluetooth MIDI
  indicator", and "Sound does not output from the unit via Bluetooth." So over
  BT the FLX4 sends **MIDI control only** — no audio; the audio must come from
  the phone's own output.
- **ESP32-S3 and ESP32-C6 are BLE-only** (Bluetooth 5 LE, no Classic BR/EDR).
  The P4 has no native radio; its BLE would run through the **C6 co-processor**
  (ESP-Hosted, the same path as Wi-Fi). The S3 has native BLE but is the USB host.
- Radios therefore match: FLX4 = BLE, S3/C6 = BLE. So it is **not blocked by
  hardware.**

## Verdict: technically possible, but a step backward for this project

An ESP32-S3 (or C6) could act as a **BLE-MIDI central** and connect to the FLX4
like a phone does — BLE-MIDI is a standard GATT service (MIDI service UUID
`03B80E5A-EDE8-4B33-A751-6CE34EC4C700`) and ESP-IDF has a BLE stack (NimBLE).
The FLX4 MIDI mapping is already done (from the USB path); only the BLE transport
+ pairing would be new. **But** it is not worth doing because:

- **No audio over BT** — the current USB host carries control **and** USB audio
  (the FLX4 headphone cue/monitor). BLE gives control only, so the headphone cue
  is lost — a dealbreaker for DJ use.
- **Latency** — BLE-MIDI latency (connection-interval bound, ~7.5–30+ ms) is far
  worse than USB (~1 ms). The jog nudge and the planned vinyl/scratch work need
  low latency; BLE would feel sluggish.
- **Throughput/jitter** — the fast jog/fader streams jitter more easily on BLE.
- **Pairing** — needs reverse-engineering how the FLX4 enters BT pairing and
  what it advertises (likely standard BLE-MIDI, but unverified); a USB cable is
  more reliable.
- **Radio contention on P4** — the C6 already serves Wi-Fi (web UI); BLE-MIDI
  would compete for the same radio.

The current **USB host** (S3 hosts the FLX4 over USB) is strictly better:
control + bidirectional USB audio + ~1 ms latency. BLE-MIDI only makes sense for
a "wireless controller, audio elsewhere" scenario, which contradicts this
project's architecture (the P4 owns the audio + cue).

## If it were ever pursued — rough proof-of-concept

1. On an S3 (native BLE), put the FLX4 into Bluetooth pairing mode and run a
   BLE central scan; confirm it advertises the standard BLE-MIDI service and
   whether bonding/encryption is required.
2. Subscribe to the MIDI characteristic; verify the incoming MIDI matches the
   known FLX4 map (`docs/DDJ_FLX4_MIDI_MAP.md`).
3. Measure end-to-end latency vs USB before considering any integration.
4. Keep audio on USB (or accept control-only). Do **not** replace the USB host
   path — at most this is an alternate transport experiment.

## Sources

- DDJ-FLX4 official manual (Specifications → Bluetooth Section: "Bluetooth Low
  Energy"; "Bluetooth MIDI indicator"; "Sound does not output from the unit via
  Bluetooth").
- AlphaTheta support: connecting to the DDJ-FLX4 via Bluetooth; no sound from
  MASTER/headphone outputs over Bluetooth.
