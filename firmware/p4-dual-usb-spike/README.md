# ESP32-P4 Dual USB Host Phase 1 Spike

This is an isolated ESP-IDF 6.0.2 hardware proof. It does not modify or start the
Pajoniiir production firmware.

## Purpose

Prove that one ESP32-P4 USB Host Library instance can operate both native host
controllers concurrently:

| Physical connection | Expected fixture | Spike activity |
| --- | --- | --- |
| P4 USB0 High-Speed | Rekordbox USB mass-storage drive | MSC enumeration and continuous bounded sector reads |
| P4 USB1 Full-Speed | Pioneer DDJ-FLX4 or USB-MIDI fixture | Descriptor validation, MIDIStreaming interface claim and raw MIDI IN |

The image deliberately does not mount a filesystem, parse Rekordbox data,
translate controls, send LEDs or stream USB Audio. Those belong to later phases.

## Architecture under test

- one call to `usb_host_install()`;
- `usb_host_config_t.peripheral_map = BIT(0) | BIT(1)`;
- both root ports initially unpowered so both class clients can register first;
- one Host Library daemon task;
- Espressif MSC class driver as the storage client;
- one project-local asynchronous client that probes every enumerated device and
  claims the first valid USB Audio/MIDIStreaming interface;
- no hub requirement;
- no production `usb_storage`, UI, audio engine or S3 code linked into the image.

The current public API reports `usb_device_info_t.parent.port_num`, which is a
USB topology parent-port field. It does not explicitly expose the P4 USB
peripheral/controller index. The spike records this field, device speed,
VID/PID, class detection and a direct-root mask so hardware logs can establish
whether the field distinguishes USB0 from USB1. If it does not, root-controller
identity remains an API gap for Phase 2/8.

The current root-port power API is global. Phase 1 uses it only for initial
bring-up. Independent recovery is intentionally not attempted here.

## Build

From an ESP-IDF v6.0.2 shell:

```powershell
cd firmware\p4-dual-usb-spike
Remove-Item -Recurse -Force build, managed_components -ErrorAction SilentlyContinue
Remove-Item sdkconfig, sdkconfig.old -ErrorAction SilentlyContinue
idf.py set-target esp32p4
idf.py build
```

Flash and monitor using the P4 serial port:

```powershell
idf.py -p COM_PORT flash monitor
```

Replace `COM_PORT` with the actual port.

## Expected log markers

Startup:

```text
one USB Host Library installed for P4 USB0+USB1
both root ports powered through the current global API
```

Storage:

```text
PROBE ... MSC=1 MIDI=0
MSC READY ...
MSC READ OK count=...
```

Controller:

```text
PROBE ... MSC=0 MIDI=1
MIDI READY ...
MIDI count=...
```

Combined status every ten seconds:

```text
PHASE1 STATUS ... dual=... MSC(active=1 ... read_ok=...) MIDI(active=1 ... packets=...)
```

After both devices have remained active for 30 minutes and both have generated
traffic:

```text
PHASE1 30-MINUTE DUAL-HOST SOAK REACHED
```

That marker does not by itself close Phase 1. The disconnect/reconnect matrix
must also be performed and the complete serial log archived.

## Hardware procedure

1. Flash the spike with neither USB fixture connected.
2. Connect the Rekordbox drive to USB0 HS.
3. Confirm `MSC READY` and increasing `MSC READ OK` count.
4. Connect the DDJ-FLX4 to USB1 FS.
5. Move controls and confirm increasing MIDI count.
6. Leave both connected and active for at least 30 minutes.
7. Remove the drive while continuing MIDI input. Confirm MIDI count still grows.
8. Reinsert the drive. Confirm a new `MSC READY` and resumed reads.
9. Remove the controller while sector reads continue.
10. Reinsert the controller. Confirm a new `MIDI READY` and resumed packets.
11. Repeat with controller-first and drive-second insertion order.
12. Boot once with both fixtures already connected.
13. Save the complete monitor log under `docs/validation/`.

## Phase 1 acceptance requirements

- one Host Library instance serves both controllers;
- both fixtures enumerate in one boot;
- continuous MSC reads and MIDI input overlap for 30 minutes;
- either device can be removed and reconnected without unregistering the other;
- no panic, watchdog reset, permanent transfer stall or queue overflow;
- logs record device speed, VID/PID, topology parent information and class;
- any inability to identify the physical controller is recorded as a public API
  gap rather than inferred from USB address;
- no production Pajoniiir behavior is changed.

## Host parser tests

The portable descriptor and USB-MIDI event parser is tested without ESP-IDF:

```powershell
.\tests\p4_dual_usb_spike\run_tests.ps1
```

The test covers valid bulk and interrupt MIDI endpoints, interface filtering,
truncated and malformed descriptors, missing endpoints and USB-MIDI CIN lengths.
