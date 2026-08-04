# P4-only migration: software work possible without hardware

Status: software-preparation scope implemented

Completed without physical hardware:

- isolated Phase 1 dual-host spike;
- reusable `usb_host_manager` as the sole Host Library owner;
- reusable `controller_usb_host` with descriptor-safe MIDI IN and bounded
  MIDI OUT;
- P4-local `controller_runtime` using the existing validated FLX4 semantic map;
- bounded semantic-event buffering with FIFO discrete commands, pressure-only
  newest-value coalescing and saturating relative-jog accumulation;
- durable held-state reconciliation and disconnect release behavior;
- ESP32-P4-only integration harness combining host manager, MSC, MIDI, local
  semantic translation and a separate dispatch task;
- portable USB-MIDI codec, event-buffer and controller-runtime tests;
- locked ESP-IDF and USB component versions;
- serial-log acceptance validator with automated tests;
- deterministic physical-test runbook.

The production P4 startup and existing `usb_storage` are deliberately not
switched without physical regression hardware. The ESP32-S3, UART control path,
LED feedback and FLX4 USB Audio cue remain the known-good product path.

Remaining physical gates:

- dual enumeration on the intended connectors;
- continuous MSC plus MIDI load;
- both insertion orders and boot with both devices attached;
- independent reconnect behavior;
- 30-minute dual-active soak;
- VBUS/current stability;
- latency and throughput measurements;
- direct local dispatch into `deck_core` under load;
- later direct P4 USB Audio verification.

For a device connected directly to a P4 root controller,
`usb_device_info_t.parent.dev_hdl == NULL` and `parent.port_num` provides the
root port index. External hubs remain out of scope.

The remaining public API gap in `espressif/usb` 1.5.0 is selective per-port
power/recovery. `usb_host_lib_set_root_port_power()` controls all enabled root
ports. A narrowly scoped additive API is being prepared in
`dvucinozd/esp-usb` before independent USB0/USB1 recovery can be accepted.
