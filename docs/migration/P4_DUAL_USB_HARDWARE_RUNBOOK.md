# ESP32-P4 Dual USB Phase 1 Hardware Runbook

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

`parent.port_num` is recorded for diagnostics but is not accepted as proof of
P4 USB0 versus USB1. `espressif/usb` 1.5.0 does not expose a stable public
root-controller identifier.
