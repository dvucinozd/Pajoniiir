# ESP32-P4-only software integration harness

This is a compile and integration harness for the reusable USB components added
during the no-hardware portion of the migration. It is not Pajoniiir product
firmware.

It proves at build time that one `usb_host_manager` owns `usb_host_install()`,
that the manager enables P4 USB0 and USB1 with `peripheral_map = 0x03`, and that
MSC plus asynchronous USB-MIDI clients compile together against ESP-IDF 6.0.2.

```bash
cd firmware/p4-only-software-harness
idf.py set-target esp32p4
idf.py build
```

The public API still lacks a stable P4 root-controller identifier and exposes
root power globally. The harness records topology but does not claim independent
USB0/USB1 recovery support.
