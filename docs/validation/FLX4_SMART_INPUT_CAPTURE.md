# FLX4 Smart Controls Raw MIDI Input Capture

Document status: completed physical capture record, reviewed 2026-07-13.

Status: complete for the physical SMART CFX and SMART FADER buttons.

Capture date: 2026-06-20

Repository state: branch `codex/flx4-extended-controls`, commit `843232a`

Final integration note: the input mapping was salvaged and merged to `master`
on 2026-06-20 at commit `9df574c`.

Capture path:

```text
DDJ-FLX4
  -> ESP32-S3 USB host
  -> flx4_midi_host raw logger
  -> UART0 / COM3 at 115200 baud
```

The S3 was temporarily built with
`CONFIG_DDJ_FLX4_TRANSLATE_TO_P4` disabled. Each button was pressed and
released three times without touching another control. All three cycles were
identical.

## Captured Messages

| Physical control | USB cable/CIN | MIDI status | Data 1 | Press | Release | Repetitions | Result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| SMART CFX | cable `0`, CIN `0x9` | `0x96` | note `0x00` | `0x7F` | `0x00` | 3 | physically verified |
| SMART FADER | cable `0`, CIN `0x9` | `0x96` | note `0x01` | `0x7F` | `0x00` | 3 | physically verified |

Raw sequence:

```text
SMART CFX:
96 00 7F
96 00 00
96 00 7F
96 00 00
96 00 7F
96 00 00

SMART FADER:
96 01 7F
96 01 00
96 01 7F
96 01 00
96 01 7F
96 01 00
```

## Interpretation Boundary

- These messages confirm momentary press/release inputs for the two physical
  buttons.
- No separate physical Smart CFX intensity or target message was established
  by this capture. Those controls remain in P4 Settings until explicitly
  mapped by a separate hardware capture and product decision.
- This capture does not authorize S3-owned toggle state. S3 may translate a
  press event, while P4 remains authoritative for enabled state and LED output.
- Production semantic IDs, S3 mapping, P4 routing, LED feedback integration,
  and end-to-end acceptance remain separate implementation work.

After capture, the S3 production translator configuration was rebuilt and
flashed back to COM3.

## Salvage Branch Hardware Verification

Verification date: 2026-06-20

Firmware branch: `codex/flx4-extended-controls-salvage`

Firmware commit: `b848b5c`

Merged-to-master commit: `9df574c`

COM ports:

- S3: COM3
- P4: COM15

Result: pass, confirmed by operator.

Observed behavior:

- SMART CFX and SMART FADER raw input mapping was included in the flashed S3
  translator firmware.
- This salvage slice intentionally maps only momentary semantic button
  press/release input. It does not enable P4 Smart CFX or Smart Fader DSP,
  settings, or LED state behavior.
