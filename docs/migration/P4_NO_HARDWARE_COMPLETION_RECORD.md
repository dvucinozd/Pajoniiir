# P4 dual-USB no-hardware completion record

This record closes the work that can be proven without the physical ESP32-P4,
Rekordbox drive and DDJ-FLX4.

Hardware follow-up started on 2026-08-09. The initial wired smoke at `fc03034`
proved a clean feature-image boot and a 191-track USB0 Rekordbox-library load,
but did not exercise the USB1 FLX4 path. See
[`../validation/P4_DUAL_USB_INITIAL_WIRED_SMOKE_20260809.md`](../validation/P4_DUAL_USB_INITIAL_WIRED_SMOKE_20260809.md).
This remains the record of the earlier no-hardware closure, not a declaration
of hardware acceptance.

## Build variants

### Default product

The normal `firmware/main-deck-p4/sdkconfig.defaults` build remains the accepted
P4/S3 product. The P4-local controller option is disabled.

### Migration feature image

The experimental image is generated with:

```bash
cd firmware/main-deck-p4
idf.py \
  -B build-p4-local \
  -D SDKCONFIG=sdkconfig.p4-local \
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.p4_local_controller" \
  set-target esp32p4
idf.py \
  -B build-p4-local \
  -D SDKCONFIG=sdkconfig.p4-local \
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.p4_local_controller" \
  build
```

This image compiles:

- shared dual-controller Host Library ownership;
- mature storage logic routed through USB0-only recovery;
- direct USB1 MIDI interface ownership;
- local semantic controller runtime;
- local profile activation;
- local LED output;
- automatic S3 control fallback and A/B LED duplication.

It remains a migration image, not a release image.

## Host verification commands

```powershell
./tests/controller_usb_host/run_tests.ps1
./tests/controller_runtime/run_tests.ps1
./tests/controller_led_runtime/run_tests.ps1
./tests/p4_dual_usb_log_validator/run_tests.ps1
```

These cover descriptor parsing, MIDI packet decoding, MIDI OUT lifecycle,
bounded event behavior, held-state reconciliation, profile activation, LED
mapping, recovery arbitration, S3/P4 trace equivalence, hardware-log parsing,
evidence manifests and binary budgets.

## Acceptance evidence

After building or downloading an artifact, create a deterministic evidence
manifest:

```bash
python tools/create_p4_acceptance_manifest.py \
  --repo-root . \
  --commit <40-character-branch-commit> \
  --artifact firmware/main-deck-p4/build-p4-local/main-deck-p4.bin \
  --output p4-acceptance-manifest.json
```

Check the feature-image binary budget:

```bash
python tools/check_p4_binary_budget.py \
  --binary firmware/main-deck-p4/build-p4-local/main-deck-p4.bin \
  --max-bytes 0x380000 \
  --output p4-binary-budget.json
```

The 0x380000 software budget is stricter than the 0x400000 application
partition and preserves 512 KiB of partition margin. Passing it does not replace
runtime heap, DMA or performance measurements.

## Hardware handoff

Use `docs/migration/P4_DUAL_USB_HARDWARE_RUNBOOK.md` and archive the complete
serial output. Validate it with:

```bash
python tools/validate_p4_dual_usb_log.py <serial-log>
```

The generated manifest intentionally records `hardware_accepted: false`.
Hardware acceptance must be changed only by a later evidence record that
references a completed physical matrix and archived logs.

The 2026-08-09 initial wired smoke is deliberately insufficient to change that
flag: USB1 controller enumeration, MIDI/LED traffic, reconnect recovery and the
30-minute dual-active soak remain open.
