# P4 USB exFAT + GPT Smoke

Date:
Firmware branch:
P4 serial port:
ESP-IDF version: v5.5

## Test Matrix

| Case | Media layout | Filesystem | Expected | Result | Notes |
|---|---|---|---|---|---|
| 1 | MBR | FAT32 | `/usb` mounts, `export.pdb` loads, one track opens |  | Baseline regression |
| 2 | GPT | FAT32 | `/usb` mounts, `export.pdb` loads, one track opens |  | Regression from bench notes |
| 3 | MBR | exFAT | `/usb` mounts, `export.pdb` loads, one track opens |  | exFAT MBR |
| 4 | GPT | exFAT | `/usb` mounts, `export.pdb` loads, one track opens |  | Primary target |

## Required Log Evidence

Paste representative boot/mount lines showing:

```text
USB MSC device:
USB media mounted:
USB media library loaded:
```

For exFAT cases, include:

```text
exFAT candidate detected
```

## Acceptance

The smoke passes when all four rows mount at `/usb`, the library parser reports a positive track count, and at least one track can be loaded without `NOT FOUND`, `NO FILESYSTEM`, or USB MSC reset errors.
