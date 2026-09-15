# P4 common 5 V and dual-VBUS bench acceptance

Date: **2026-09-11**

Status: **PASS for the current bench wiring, operator-reported**.

## Scope

The operator completed the electrical checks defined by
[`../HARDWARE_WIRING.md`](../HARDWARE_WIRING.md) and the measurement checklist
issued for the current P4-only topology. The checked configuration includes the
ESP32-P4 board, USB0 Rekordbox storage branch and USB1 DDJ-FLX4 branch on the
current regulated supply and wiring.

The operator reported that all measurements were either inside the preferred
range or inside the allowed range. This confirms for the current bench
configuration:

- correct polarity and common-ground continuity;
- no persistent 5 V-to-ground short;
- no observed backfeed between independently powered paths;
- P4 input, USB0 VBUS and USB1 VBUS inside the project acceptance range of
  `4.75--5.25 V` through the exercised states;
- the 3.3 V rail inside its allowed range;
- acceptable supply/ground voltage drop;
- total and branch current inside the selected supply/protection limits;
- no protection trip, brownout or reset during the measurement sequence.

The exercised sequence covered the requested static and loaded conditions:
P4 only, USB0 attach/mount, FLX4 enumeration, both devices idle, dual-deck
load/playback and reconnect activity.

## Evidence boundary

Individual numeric readings, instrument model, min/max trace and protection
trip waveform were not supplied to the repository. This record therefore
preserves an operator-confirmed pass against the stated limits, not a raw
instrument transcript. If the wiring, supply, cable lengths, connectors or
protection hardware change, this acceptance no longer applies.

The common 5 V/dual-VBUS P0 bench blocker is closed for the current
configuration. Power measurements must be repeated with final cable lengths
and the enclosure closed; that remains part of the enclosure release gate.

## Next gate

Proceed with the 50-cycle dual-USB lifecycle/recovery matrix on exact installed
image `RC2-116-g77d723c`, recording boot epoch and USB/audio/controller counters
before and after each group.
