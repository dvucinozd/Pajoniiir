# P4 dual-USB initial wired smoke — 2026-08-09

Document status: **partial hardware evidence; migration is not hardware-accepted**

This record covers the first wired flash and short boot/storage smoke of the
experimental P4-local-controller image. It proves that the candidate boots on
the physical P4 and that the existing USB0 Rekordbox-storage path remains
usable. It does not prove the direct USB1 FLX4 path or any release gate that
requires playback, reconnect testing or a sustained soak.

## Candidate and environment

- Branch: `feat/p4-dual-usb-host`
- Commit: `fc03034` (`fix(p4-usb): raise production control transfer limit`)
- Firmware version reported at boot: `RC2-54-gfc03034`
- ESP-IDF: `v6.0.2`
- Target: ESP32-P4 revision v1.3
- Serial/flash port: `COM15`
- Build directory: `firmware/main-deck-p4/build_flash`
- Application size: 2,446,160 bytes
- Application SHA-256:
  `581d6d6033e145245b244dc8664aa9484865af847710ab36b8f9d60280249d73`
- Application partition margin: 1,748,144 bytes (42%)

The feature overlay was active: the boot log reported that the experimental
P4-local controller path was ready on USB1 while retaining the S3 fallback.

## Wired flash result

The following command completed successfully:

```powershell
idf.py -B build_flash -p COM15 flash
```

Esptool connected to the ESP32-P4, wrote the bootloader, partition table,
initial OTA data and factory application, and verified the hash of every
written region. The board then hard-reset through RTS and booted the factory
application at `0x20000`.

## Boot and storage observations

The controlled serial reset produced one clean boot. The log confirmed:

- ESP-IDF v6.0.2 second-stage bootloader;
- 16 MB DIO flash at 80 MHz;
- factory application `RC2-54-gfc03034` loaded from `0x20000`;
- the microSD card was detected as a 29,520 MB SDHC device;
- the first library scan correctly reported that USB was not mounted yet;
- USB0 recovery was requested once while the drive was being attached;
- the Rekordbox USB library subsequently loaded **191 tracks**;
- the S3 fallback/control-link peer reported firmware `RC2`, slot 1, state 3;
- the P4-local controller runtime reported ready on USB1;
- no panic, assertion, watchdog, brownout or unexpected reboot appeared.

Serial windows covered boot through approximately 44.8 seconds and a second
window from approximately 69.8 through 98.9 seconds. The device remained up
between captures. The later window contained only the one-second monitor-PCM
status line with zero submitted, dropped and sent frames, which is expected
because playback was not started.

## Non-fatal warnings observed

- The BOYA flash chip used the generic SPI-flash driver. This is an existing
  configuration warning and did not prevent boot or verified flash access.
- The initial `/usb/PIONEER/rekordbox/export.pdb` open failed before the drive
  mounted. The later `USB media library loaded: 191 tracks` message proves that
  this transient startup state recovered.
- MSC reported SCSI sense key `0x06`, ASC `0x28`, ASCQ `0x00` during media
  insertion. The library loaded immediately afterwards, so this run treats it
  as the expected medium-change transition rather than a persistent fault.

## Acceptance impact

Passed in this run:

- wired flash and per-region hash verification;
- clean feature-image boot on physical ESP32-P4 hardware;
- boot with USB0 Rekordbox storage and successful 191-track library load;
- short idle stability with no fatal runtime fault;
- existing S3 fallback/control-link visibility.

Still open:

- direct DDJ-FLX4 enumeration and routing on USB1;
- MIDI input, LED output and reconnect snapshot through the P4-local path;
- both USB insertion orders and boot with both devices attached;
- simultaneous storage reads and controller traffic;
- playback and cache-miss behavior;
- selective USB0/USB1 disconnect and recovery;
- heap, DMA, stack, latency and power measurements;
- the required 30-minute dual-active soak;
- direct P4-to-FLX4 USB Audio.

The migration therefore remains **software-ready with partial physical
bring-up evidence, not hardware-accepted**.
