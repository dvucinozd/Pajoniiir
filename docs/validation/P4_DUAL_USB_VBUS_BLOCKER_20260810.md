# P4 dual-USB external-power VBUS blocker — 2026-08-10

Document status: **confirmed bench blocker; feature branch paused**

This record covers the first attempt to operate both P4 USB root ports while
COM15 was unavailable: the Rekordbox USB drive was attached to USB0, the
DDJ-FLX4 was attached to USB1, and the JC4880P443C_I_W was powered with 5 V
through JP1 instead of either USB-C connector.

## Candidate and bench arrangement

- Branch: `feat/p4-dual-usb-host`
- Final firmware-changing checkpoint before the hardware pause: `850ab3f`
- Firmware reported by the running P4: `RC2-54-gfc03034`
- P4 supply: regulated 5 V on JP1 pin 2 (`VCC5V`), ground on JP1
- USB0: Rekordbox drive
- USB1/former COM15 connector: DDJ-FLX4
- Diagnostics: Wi-Fi SoftAP `Pajoniiir`, P4 address `192.168.4.1`

Do not apply 5 V to JP1 pins 1 or 16; those pins are the 3.3 V rail.

## Observed result

The P4 itself booted and remained reachable over Wi-Fi, but neither downstream
USB device received usable VBUS:

- the USB drive activity/power LED remained off;
- `GET /api/status` returned HTTP 200 but reported
  `controller.present=false`, VID/PID zero and no local-controller traffic;
- `GET /api/diagnostic-log` for boot 148 contained `BOOT`, `SD_MOUNTED`,
  `WIFI_ENABLE_REQUESTED`, `WIFI_STARTED` and `CONTROL_LINK_OFFLINE`;
- that boot contained no `USB_MOUNTED` or `LIBRARY_LOADED` event;
- a second status read after eight seconds remained unchanged;
- no panic, watchdog, brownout or unexpected reboot was present.

Earlier boots 145 and 146, when the board was powered through USB, did contain
`USB_MOUNTED` and `LIBRARY_LOADED` for the same 191-track library. Combined
with the unlit drive LED, this isolates the current failure to downstream VBUS
availability in the JP1-powered arrangement rather than to the USB host
firmware or Rekordbox media parser.

This run does **not** prove that the board can safely source either USB port
from JP1, and it must not be repeated by injecting unprotected 5 V into a
USB-C connector or by joining independent 5 V sources with an unmodified
Y-cable.

## Required hardware before testing resumes

Provide a protected external 5 V path for each downstream device while keeping
USB data direct to its intended P4 root:

1. Implement the topology with one USB 2.0 data interposer per port or one
   internal power-distribution daughterboard. Keep D+, D-, ground and shield
   continuous; isolate the P4-side VBUS conductor from downstream VBUS. A
   permanent alternative is to cut both native VBUS traces and inject the
   protected outputs at the USB connector pads, but only after schematic and
   continuity verification.
2. Feed both downstream VBUS conductors from a regulated, current-limited 5 V
   supply through separate high-side protected outputs. A dual-channel USB
   power switch such as the active-high TPS2561 is a suitable candidate.
3. Use a common ground between the supply, P4 and both USB devices. Feed the
   P4 itself from the same regulated 5 V source at JP1 pin 2.
4. For a TPS2561 implementation, use one output per port, local input/output
   decoupling, outputs disabled by default, and an independently verified
   current limit. A 69.8 kOhm `ILIM` resistor provisionally targets
   approximately 0.8 A per channel; verify the exact populated part and its
   datasheet before assembly. Approximately 0.8--1.0 A per port is the initial
   design range, not an accepted measurement.
5. Preserve USB-C role detection with known-good host/OTG adapters. Do not put
   D+/D- on a solderless breadboard or loose Dupont wiring.

Before connecting the drive or FLX4, verify with a multimeter:

- open circuit between upstream and downstream VBUS on each interposer;
- continuity and no cross-short on D+, D- and ground;
- 4.75–5.25 V at each downstream connector with its protected output enabled;
- no VBUS-to-ground short and no backfeed into the P4-side VBUS conductor.

The supply and wiring must be sized for the P4, the FLX4 and the USB drive
together; the provisional minimum bench target is a regulated 5 V / 3 A source
with per-port over-current protection. A quality 4 A source gives additional
startup and wiring margin. Both remain design targets, not measured acceptance
results. Keep the power wiring short and use an oscilloscope or a reliable
min/max capture to catch transients that a normal multimeter can miss.

After the static checks, qualify the branches incrementally: P4 only, USB
drive only, FLX4 only, then both devices during sustained dual-deck playback.
Externally powering the FLX4 may reduce load but does not replace VBUS
isolation and backfeed checks. A powered hub is diagnostic-only because it
changes the required direct-root topology. Passive Y-cables and tied
independent 5 V supplies are prohibited.

## Project impact

Development on `feat/p4-dual-usb-host` is intentionally paused at firmware
checkpoint `850ab3f` plus this documentation update. Do not merge the branch,
remove the S3 fallback or claim dual-USB hardware acceptance until the
protected VBUS arrangement is built, electrically checked and the full
dual-device matrix is run. Normal work may continue from `master` without this
experimental branch.
