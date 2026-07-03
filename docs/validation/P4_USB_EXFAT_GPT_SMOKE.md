# P4 USB exFAT + GPT Smoke

Date: 2026-07-03
Firmware branch: `codex/p4-usb-exfat-gpt`
P4 serial port: COM15
ESP-IDF version: v5.5

## Test Matrix

| Case | Media layout | Filesystem | Expected | Result | Notes |
|---|---|---|---|---|---|
| 1 | MBR | FAT32 | `/usb` mounts, `export.pdb` loads, one track opens | PASS | Baseline regression: existing Rekordbox stick mounted, library loaded, track played (operator-verified 2026-07-03; log capture not archived) |
| 2 | GPT | FAT32 | `/usb` mounts, `export.pdb` loads, one track opens | PASS | Inserted media mounted as `gpt=1`, `exfat=0`; UI showed tracks |
| 3 | MBR | exFAT | `/usb` mounts, `export.pdb` loads, one track opens |  | exFAT MBR |
| 4 | GPT | exFAT | `/usb` mounts, `export.pdb` loads, one track opens | PASS | Inserted media mounted as `gpt=1`, `exfat=1`; UI showed tracks |

## Required Log Evidence

Paste representative boot/mount lines showing:

```text
USB MSC device:
USB media mounted:
USB media library loaded:
```

2026-07-03 COM15 GPT/FAT32 evidence:

```text
W (19829) usb_storage: USB MSC device: 29999 MB, sector=512 bytes (VID:0x048D PID:0x1234)
W (19832) usb_storage: USB media mounted: base_lba=2048 sectors=61435904 sector_size=512 exfat=0 gpt=1
W (19987) main: USB media library loaded: 8 tracks
```

2026-07-03 COM15 GPT/exFAT evidence:

```text
W (2608) usb_storage: USB MSC device: 29999 MB, sector=512 bytes (VID:0x048D PID:0x1234)
W (2660) usb_media_mount: exFAT candidate detected at LBA 2048
W (2662) usb_storage: USB media mounted: base_lba=2048 sectors=61435904 sector_size=512 exfat=1 gpt=1
W (2806) main: USB media library loaded: 8 tracks
```

For exFAT cases, include:

```text
exFAT candidate detected
```

## Acceptance

The smoke passes when all four rows mount at `/usb`, the library parser reports a positive track count, and at least one track can be loaded without `NOT FOUND`, `NO FILESYSTEM`, or USB MSC reset errors.
