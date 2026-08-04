# P4-only migration: software work possible without hardware

Status: software-preparation scope implemented

Completed without physical hardware:

- isolated Phase 1 dual-host spike;
- reusable `usb_host_manager` as the sole Host Library owner;
- reusable `controller_usb_host` with descriptor-safe MIDI IN and bounded
  MIDI OUT;
- ESP32-P4-only integration harness combining host manager, MSC and MIDI;
- portable USB-MIDI codec tests;
- locked ESP-IDF and USB component versions;
- serial-log acceptance validator with automated tests;
- deterministic physical-test runbook.

The production P4 startup and existing `usb_storage` are deliberately not
switched without physical regression hardware. The ESP32-S3, UART control path,
LED feedback and FLX4 USB Audio cue remain the known-good product path.

Remaining physical gates:

- dual enumeration on the intended connectors;
- reliable connector/root-controller identity;
- continuous MSC plus MIDI load;
- both insertion orders and boot with both devices attached;
- independent reconnect behavior;
- 30-minute dual-active soak;
- VBUS/current stability;
- latency and throughput measurements;
- later direct P4 USB Audio verification.

Confirmed public API gaps in `espressif/usb` 1.5.0:

- root-port power control is global;
- public device information does not expose a stable P4 USB-peripheral ID.

Independent USB0/USB1 recovery therefore requires a narrowly scoped additive
API in `dvucinozd/esp-usb` before production migration can be accepted.
