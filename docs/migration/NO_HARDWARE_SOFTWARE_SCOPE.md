# P4-only migration: software completion boundary without hardware

> **Superseded implementation record.** On 2026-08-27 the local-controller
> overlay became the sole P4 product configuration and the S3 fallback was
> retired. References below to an optional feature flag, A/B duplication and
> active S3 CI describe the earlier migration checkpoint only.

Status: **software preparation complete; partial physical bring-up recorded;
full acceptance still open**

This record describes what has been implemented and verified without access to
an ESP32-P4 board, Rekordbox storage device or DDJ-FLX4.

Follow-up hardware became available on 2026-08-09. The experimental image at
`fc03034` was wired-flashed, booted cleanly and loaded 191 Rekordbox tracks from
USB0. That focused run is recorded in
[`../validation/P4_DUAL_USB_INITIAL_WIRED_SMOKE_20260809.md`](../validation/P4_DUAL_USB_INITIAL_WIRED_SMOKE_20260809.md).
It did not exercise a DDJ-FLX4 on USB1, playback, reconnect recovery or the
required soak, so the completion boundary below remains in force.

## Completed software scope

- One reusable `usb_host_manager` owns the Host Library and supports both P4
  root controllers.
- Production P4 USB dependencies are pinned to the validated
  `dvucinozd/esp-usb` commit that exposes selective root-port power control.
- `controller_usb_host` provides descriptor-safe USB-MIDI input, bounded MIDI
  output, fixed USB1 routing in the migration build, disconnect-safe transfer
  teardown and a producer-quiescence gate that prevents stale LED packets from
  crossing connection generations.
- The mature storage implementation can be compiled through a narrow adapter
  that moves Host Library ownership to `usb_host_manager` and routes recovery
  only to USB0.
- `controller_runtime` maps raw MIDI locally, keeps discrete events FIFO,
  coalesces high-rate absolute values only under pressure, accumulates relative
  jog values with saturation and reconciles held controls after queue pressure
  or disconnect.
- S3CP v2 profiles activate locally on P4 with the built-in FLX4 map as safe
  fallback.
- Direct MIDI LED packet generation is available locally, while the migration
  build also retains the S3 LED path for A/B comparison.
- `CONFIG_PAJONIIIR_P4_LOCAL_CONTROLLER` compiles the complete local-controller
  path into the real `main-deck-p4` application. It defaults to `n`; the normal
  product image therefore remains on the accepted S3 path.
- The CI-only `sdkconfig.p4_local_controller` overlay builds the experimental
  production image and verifies its routed storage/MIDI sources and linker
  wrappers.
- A deterministic trace test feeds known vectors plus 100,000 generated MIDI
  messages through the S3 and P4 maps and compares every emitted event and
  reconnect snapshot.
- A production recovery task routes storage and controller faults through the
  two-port recovery arbiter, serializes simultaneous faults, coalesces duplicate
  requests, applies bounded exponential retry cadence and alternates fairly
  between USB0 and USB1.
- The pure USB Audio preparation layer contains the S3-equivalent UAC descriptor
  parser and packetizer plus a bounded, generation-aware PCM frame ring. It is
  compiled only in the software harness and is not an active isochronous
  transport.
- Serial-log validation, evidence-manifest generation and binary-budget tooling
  are host tested.

## Deliberately unchanged default behavior

- `sdkconfig.defaults` does not enable the P4-local controller.
- ESP32-S3 UART control, heartbeat, controller transport and cue-audio path remain
  the default product path.
- No S3 source or CI target has been removed.
- Direct P4 USB Audio streaming is not enabled.
- The branch must not be merged into `master` as a hardware-accepted migration.

## Physical gates still required

1. Enumerate Rekordbox MSC on USB0 and DDJ-FLX4 on USB1 in one boot. USB0-only
   boot and a 191-track library load passed on 2026-08-09; dual enumeration is
   still open.
2. Verify both insertion orders and boot with both devices attached.
3. Run simultaneous storage reads, MIDI input and MIDI LED output.
4. Remove/reconnect each device while the other remains active.
5. Prove selective USB0/USB1 recovery on the actual P4 root controllers.
6. Verify local semantic dispatch against real deck behavior and latency.
7. Verify physical FLX4 LEDs and authoritative reconnect snapshot.
8. Measure VBUS/current stability, heap, DMA heap and task stack margins.
9. Run the minimum 30-minute dual-active soak and archive the full serial log.
10. Later verify direct P4-to-FLX4 USB Audio at 44.1 and 48 kHz before S3
    retirement.

Until those gates pass, the correct status is **software-ready, not
hardware-accepted**.
