# ESP32-P4-only software integration harness

This is a compile and integration harness for reusable USB and controller
components added during the no-hardware portion of the migration. It is not
Pajoniiir product firmware.

It proves at build time that:

- one `usb_host_manager` owns `usb_host_install()`;
- P4 USB0 and USB1 are enabled with `peripheral_map = 0x03`;
- MSC and asynchronous USB-MIDI clients coexist in one ESP32-P4 image;
- raw USB-MIDI messages feed the same mature FLX4 semantic mapping currently
  used by the S3;
- a separate dispatch task drains a bounded 64-entry semantic-event buffer;
- discrete commands remain FIFO while high-rate absolute controls coalesce to
  their newest value only under pressure;
- relative jog deltas accumulate with saturation;
- durable held-state levels survive queue pressure and generate forced release
  events on controller disconnect;
- reconnect snapshots are generated locally on the P4 side.

```bash
cd firmware/p4-only-software-harness
idf.py set-target esp32p4
idf.py build
```

The harness intentionally does not mount the Rekordbox filesystem, dispatch
semantic events into `deck_core`, control LEDs or stream USB Audio. Those paths
remain gated by physical regression tests.

For direct-root devices, `usb_device_info_t.parent.port_num` identifies the root
port index when `parent.dev_hdl == NULL`. The remaining library gap is selective
per-port power/recovery: the current public power API affects all enabled root
ports.
