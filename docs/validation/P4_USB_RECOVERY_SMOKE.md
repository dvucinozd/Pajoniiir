# P4 USB Storage Recovery Smoke

Date: 2026-07-11
Firmware branch: `codex/usb-storage-recovery`
P4 serial port: COM15
ESP-IDF version: v5.5

## Scope

Validate USB MSC recovery when media is removed during an active track preload,
including a subsequent reconnect without rebooting the P4.

## Acceptance

- A stable connect mounts the drive once and loads the library once.
- Media removal stops both decks and clears the library once.
- A preload interrupted by physical removal does not become `PRELOAD ERR` or
  remain in a partially loaded state.
- Duplicate driver disconnect events do not repeat application teardown.
- The same drive can mount and reload its library without restarting the P4.
- No panic, watchdog, stack overflow, or reboot occurs.

Low-level `READ10`, URB `ESP_ERR_INVALID_STATE`, and root-port reset errors are
expected when a device is physically removed while a USB transfer is in flight.
They are not an application recovery failure if teardown completes once and the
next connect mounts normally.

## Results

| Case | Result | Evidence |
|---|---|---|
| Boot with media connected | PASS | One mount; library loaded with 18 tracks |
| Remove during active preload | PASS | One application disconnect; no `PRELOAD INCOMPLETE` or `PRELOAD ERR` |
| Duplicate disconnect handling | PASS | One `drive disconnected` and one `USB drive removed` |
| Reconnect without P4 reboot | PASS | FAT32/MBR remounted; library reloaded with 18 tracks |
| Runtime stability | PASS | No panic, watchdog, stack overflow, or reboot |

Representative successful mount:

```text
W usb_storage: USB MSC device: 29999 MB, sector=512 bytes (VID:0x346D PID:0x5678)
W usb_storage: USB media mounted: base_lba=2048 sectors=61435904 sector_size=512 exfat=0 gpt=0
W main: USB media library loaded: 18 tracks
```

Representative removal during preload:

```text
W usb_storage: drive disconnected
W main: USB drive removed
W ui: Deck 1 ANLZ metadata unavailable
```

The physical removal also produced the expected ESP-IDF USB transport errors,
but the application emitted no incomplete-preload error and remained available
for the successful reconnect above.
