# Hardware Wiring

Status: **active P4-only wiring, reconciled 2026-09-20**.

## Product connections

| Function | P4 connection | Notes |
| --- | --- | --- |
| Rekordbox medium | USB0 host | Storage/MSC root port |
| DDJ-FLX4 | USB1 host | MIDI IN/OUT and four-channel UAC |
| MAIN audio | PCM5102A on the board I2S path | RCA/main program output |
| Headphone cue | FLX4 UAC channels 3/4 | Monitor/cue output |
| Service/debug | P4 native service connector or Wi-Fi | Do not share downstream host VBUS |

No inter-board data or audio wiring is part of the active product.

## Mandatory downstream VBUS design

The bench has demonstrated that the firmware can run USB0 storage and USB1
FLX4 together. On 2026-09-11 the operator confirmed that the current bench
wiring passed the requested continuity, isolation/backfeed, voltage, drop and
current checks. Raw numeric readings were not preserved, so this acceptance
applies only to the unchanged accepted enclosure configuration and must be
repeated after a wiring, supply or enclosure change. Feeding `VCC5V` on JP1 pin 2
powers the P4 board but must not by itself be assumed to provide safe or
adequate downstream VBUS.

Use one common regulated supply and an independently protected high-side output
for each downstream USB port:

```text
regulated 5 V supply
        |
        +-- fuse/eFuse -------------------- P4 VCC5V
        |
        +-- dual current-limited USB switch
                +-- OUT1 ------------------ USB0 device VBUS
                +-- OUT2 ------------------ USB1 device VBUS

common GND -------------------------------- P4 + both USB devices
```

For each port, keep D+, D-, ground and shield connected to the intended P4 root
port. Isolate the native P4-side VBUS conductor from device-side VBUS before
injecting the protected output. Never join two supplies with a passive Y-cable,
tie independent regulated outputs together or inject raw 5 V into USB-C pins.

The provisional target is a regulated 5 V / 3 A or better common supply and
approximately 0.8--1.0 A current limit per downstream port. These are design
starting points, not acceptance evidence.

## Electrical acceptance

Before connecting devices, verify:

- no short between 5 V and ground;
- no backfeed into either isolated P4-side VBUS conductor;
- correct D+/D-/ground continuity to each root port;
- 4.75--5.25 V at each downstream connector under load;
- independent current limiting and fault isolation for USB0 and USB1.

Then test in this order: P4 only, USB0 only, USB1 only, combined idle, combined
dual-deck playback, reconnect transients and sustained load. Record voltage at
the P4 and both downstream connectors, peak/steady current and reset/brownout
evidence. A basic multimeter may miss short startup dips; use reliable min/max
capture or an oscilloscope for final qualification.

Cable routing must be revalidated after enclosure installation. Keep power and
ground runs short and adequately sized, keep D+/D- in short shielded USB 2.0
cable, and verify cooling and RF behavior with the enclosure closed.

The accepted measurement record is
[`validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md`](validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md).
Superseded dual-board wiring remains available in Git history only.
