# DDJ-FFL4 P4 FatFs Override

This component was copied from:

```text
C:\Espressif\frameworks\esp-idf-v5.5\components\fatfs
```

Reason: ESP-IDF v5.5 hardcodes `FF_FS_EXFAT=0` in the bundled FatFs
configuration, but the DDJ-FFL4 P4 USB media mount needs exFAT support for DJ
USB media.

Local delta:

- `src/ffconf.h`: `FF_FS_EXFAT=1`
- `src/ffconf.h`: `FF_USE_LABEL` maps the optional
  `CONFIG_FATFS_USE_LABEL` Kconfig symbol to a numeric 0/1 value so exFAT code
  compiles when volume-label support is unset.

Limitation:

- `FF_LBA64=0` remains unchanged. The mount layer translates a 32-bit base LBA
  before FatFs sees partition-relative sectors. This supports normal DJ USB
  sticks, but is not a general-purpose stack for media larger than 2 TiB.
