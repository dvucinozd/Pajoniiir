# ESP32-P4 Dual USB Phase 1 Hardware Runbook

This runbook is for the dedicated `firmware/p4-dual-usb-spike` diagnostic
image and its machine-parsable `PHASE1 STATUS` log format. The experimental
`main-deck-p4` product image was first wired-smoked on 2026-08-09 and remains
flashed on the bench. Resume that product-image test from
[`P4_DUAL_USB_NEXT_SESSION.md`](P4_DUAL_USB_NEXT_SESSION.md) before using this
spike for the topology/reconnect soak.

Flash `firmware/p4-dual-usb-spike` and save the complete serial monitor output.

Run the following sequence:

1. connect storage, then controller;
2. reboot with both connected;
3. disconnect and reconnect storage while MIDI remains active;
4. disconnect and reconnect the controller while MSC reads continue;
5. repeat with controller connected before storage;
6. keep both active for at least 30 minutes while operating controls.

Validate the saved log:

```bash
python tools/validate_p4_dual_usb_log.py phase1-serial.log \
  --require-soak-seconds 1800 \
  --require-disconnect-matrix \
  --json
```

Exit code `0` and `"accepted": true` are required. The log must show:

- `peripheral_map=0x03`;
- `MSC READY` and `MIDI READY`;
- advancing MSC read and MIDI packet counters;
- one disconnect/reconnect cycle for each class;
- restored dual-active state after reconnects;
- at least 1800 seconds of continuous dual-active operation;
- no panic, watchdog, assertion, brownout, queue drop, MSC read failure or
  MIDI submit failure.
- every accepted soak status reports `msc_parent=0 msc_direct=1`,
  `midi_parent=1 midi_direct=1` and `root_mask=0x00000003`.

For devices connected directly to the P4 controllers, record
`parent.dev_hdl == NULL` and the corresponding `parent.port_num`. That index is
the root port identity. Hub-connected devices are not accepted in Phase 1.

The current global root-power API must not be used as evidence of independent
recovery because it affects both enabled root ports.
