# Pajoniiir P4 FatFs Override Notes

Documentation status: active local integration note, reviewed 2026-07-13.
FAT32/exFAT on superfloppy, MBR and GPT media is hardware-validated.

This component was copied from:

```text
C:\Espressif\frameworks\esp-idf-v5.5\components\fatfs
```

Reason: ESP-IDF v5.5 hardcodes `FF_FS_EXFAT=0` in the bundled FatFs
configuration, but the Pajoniiir P4 USB media mount needs exFAT support for DJ
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

Pruned from the upstream copy (not compiled into this firmware; re-add from
ESP-IDF v5.5 if re-vendoring):

- `test_apps/`, `test_fatfsgen/`, `host_test/` — CI test infrastructure.
- `fatfsgen.py`, `fatfsparse.py`, `wl_fatfsgen.py`, `fatfs_utils/` — the
  build-time FAT image generators. `project_include.cmake` invokes these from
  `${IDF_PATH}/components/fatfs/` (the real install), not this local copy, so
  the local copies were dead weight.

The build (`CMakeLists.txt`) only compiles `src/`, `diskio/`, `port/`, and
`vfs/`.
