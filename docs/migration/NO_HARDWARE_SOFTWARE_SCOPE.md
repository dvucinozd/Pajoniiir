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
- transport-neutral S3CP v2 parser and controller-profile runtime copied from
  the accepted S3 implementation;
- local P4 profile activation, reconnect replay and LED-packet mapping tests;
- ESP32-P4-only integration harness combining host manager, MSC, MIDI, local
  semantic translation, profile selection and a separate dispatch task;
- portable USB-MIDI codec, event-buffer, controller-runtime and profile tests;
- locked ESP-IDF and USB component versions;
- serial-log acceptance validator with automated tests;
- deterministic physical-test runbook.

The production P4 startup and existing `usb_storage` are deliberately not
switched without physical regression hardware. The ESP32-S3, UART control path,
physical LED feedback and FLX4 USB Audio cue remain the known-good product path.

Remaining physical gates:

- dual enumeration on the intended connectors;
- continuous MSC plus MIDI load;
- both insertion orders and boot with both devices attached;
- independent reconnect behavior;
- 30-minute dual-active soak;
- VBUS/current stability;
- latency and throughput measurements;
- direct local dispatch into `deck_core` under load;
- physical MIDI LED feedback;
- later direct P4 USB Audio verification.

For a device connected directly to a P4 root controller,
`usb_device_info_t.parent.dev_hdl == NULL` and `parent.port_num` provides the
root port index. External hubs remain out of scope.

Selective per-port power/recovery is being validated in the
`dvucinozd/esp-usb` fork. It is not yet enabled in Pajoniiir dependencies and
must not be treated as hardware-accepted until the dual-port fault matrix passes.
