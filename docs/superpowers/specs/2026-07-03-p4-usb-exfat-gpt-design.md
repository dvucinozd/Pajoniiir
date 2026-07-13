# P4 USB exFAT And GPT Design

Document status (2026-07-13): implemented and hardware-validated design record.

## Goal

Make the ESP32-P4 read rekordbox USB media formatted as exFAT and/or partitioned with GPT, while preserving the existing `/usb` VFS path and the current library/audio code that uses `fopen`, `opendir`, and `stat`.

## Scope

Implement one USB media mount closure that supports:

- FAT32 on MBR.
- FAT32 on GPT.
- exFAT as a superfloppy volume.
- exFAT on MBR.
- exFAT on GPT.

The implementation is for normal DJ USB media and targets devices addressable with 32-bit sectors, which covers USB sticks up to 2 TiB at 512-byte sectors and larger at 4096-byte sectors. Full 64-bit LBA devices above that boundary are out of scope for this closure because ESP-IDF v5.5 `ff_diskio_impl_t`, the current `usb_host_msc` diskio adapter, and SCSI READ10/WRITE10 paths are all 32-bit-sector based.

Formatting USB drives on the P4 is out of scope. The P4 only needs to mount and read rekordbox exports and audio files.

## Current constraints

P4 currently mounts USB media in `firmware/main-deck-p4/components/usb_storage/usb_storage.c` by calling `msc_host_vfs_register()` from the `espressif/usb_host_msc` managed component. That helper registers a FatFs diskio backend and calls `f_mount()`. It does not manually select MBR or GPT partitions.

The local ESP-IDF v5.5 FatFs config has:

- `FF_USE_LFN=3` because the project already enables `CONFIG_FATFS_LFN_HEAP`.
- `FF_LFN_UNICODE=2` because the project already enables UTF-8 API encoding.
- `FF_MULTI_PARTITION=1`.
- `FF_FS_EXFAT=0`, hardcoded in `components/fatfs/src/ffconf.h`.
- `FF_LBA64=0`.

This means exFAT cannot mount with the stock FatFs component, and relying on FatFs GPT handling would require LBA64/exFAT configuration changes plus lower-level diskio changes.

## Architecture

Keep the public media path unchanged: `/usb` remains the only mount point used by the library parser, track path resolution, ANLZ parser, and audio preload.

Add a P4-local USB media mount layer inside or alongside the existing `usb_storage` component. This layer replaces the direct call to `msc_host_vfs_register()` with:

1. install/open the MSC device exactly as today;
2. read sector 0 through MSC sector reads;
3. discover the logical FAT/exFAT volume start LBA;
4. register a FatFs diskio adapter that translates logical sector `N` to physical sector `base_lba + N`;
5. register FatFs VFS at `/usb`;
6. call `f_mount()` against the translated logical drive.

The mount layer owns the VFS handle, FatFs object, allocated drive number, base LBA, device handle reference, and unmount cleanup. `usb_storage.c` remains responsible for USB host lifecycle, connect/disconnect events, user callback notification, and root directory debug listing.

## exFAT support

Add a committed repo-local FatFs override for the P4 build under `firmware/main-deck-p4/components/fatfs`, based on ESP-IDF v5.5 FatFs, with the minimum required configuration change:

- enable `FF_FS_EXFAT=1`;
- keep LFN heap and UTF-8 API behavior;
- keep 32-bit LBA for this closure.

The override must document the upstream ESP-IDF FatFs source version it was copied from. It must not require editing `C:\Espressif\frameworks\esp-idf-v5.5`.

The implementation should include a compile-time assertion or small diagnostic log proving that the P4 firmware was built with exFAT enabled.

## Partition discovery

The mount layer should build a small list of candidate volume starts:

- `LBA 0` for superfloppy FAT/exFAT.
- MBR primary partitions with FAT-like type IDs, including `0x01`, `0x04`, `0x06`, `0x0B`, `0x0C`, `0x0E`, and exFAT/NTFS-style `0x07`.
- GPT partitions whose type GUID is Microsoft Basic Data.

Candidates are validated by reading their boot sector:

- exFAT candidate: sector has jump boot plus OEM name `EXFAT   `.
- FAT candidate: sector has a valid FAT-style boot signature and is left to FatFs for final validation.

When multiple candidates are valid, prefer the first candidate that contains `PIONEER/rekordbox/export.pdb` after mount. If probing that path requires temporarily mounting more than one candidate, unmount and unregister each failed candidate cleanly before trying the next. If no candidate contains rekordbox data, mount the first valid FAT/exFAT candidate so diagnostic root listing still works.

## Data flow

1. USB MSC connect event arrives in `usb_storage`.
2. `usb_storage` installs the MSC device and logs capacity/vendor information.
3. New mount layer reads partition metadata from the raw MSC device.
4. New mount layer selects a candidate volume and registers translated diskio.
5. FatFs mounts the selected volume at `/usb`.
6. Existing `usb_storage` root listing runs.
7. Existing mount callback calls `library_init()`.
8. Existing PDB/ANLZ/audio code reads files through `/usb` without knowing whether the stick is FAT32/exFAT or MBR/GPT.

## Error handling

Mount failures should produce actionable logs:

- no readable sector 0;
- no supported FAT/exFAT candidate;
- GPT header invalid;
- GPT candidate found but no mountable FAT/exFAT filesystem;
- exFAT detected but firmware is not built with exFAT support;
- FatFs mount failure with the raw `FRESULT`.

On any failed candidate, the code must unregister VFS, unmount FatFs, release the drive number, and leave the MSC device usable for the next candidate or for clean disconnect.

On USB removal, cleanup must still run through `media_io_gate` as today before unregistering the filesystem and uninstalling the MSC device.

## Testing

Host tests should cover pure parsing and selection logic without USB hardware:

- detect exFAT superfloppy boot sector;
- detect FAT32 MBR partition;
- detect exFAT MBR partition type `0x07`;
- detect GPT protective MBR and Microsoft Basic Data partition;
- reject malformed GPT headers;
- order candidates deterministically;
- prefer the candidate with a simulated `PIONEER/rekordbox/export.pdb` hit.

Firmware/IDF verification should include:

- P4 host tests for the new parser module.
- P4 firmware build, proving the FatFs override and USB storage component compile.
- Hardware smoke with four sticks or images:
  - FAT32 MBR known-good baseline;
  - FAT32 GPT regression case from bench notes;
  - exFAT MBR;
  - exFAT GPT.

The hardware acceptance condition is that P4 mounts `/usb`, lists the expected root folders, `library_init()` reads `PIONEER/rekordbox/export.pdb`, and track preload can open at least one audio path from the mounted drive.

## Risks

- Maintaining a local FatFs override can drift from ESP-IDF updates. Keep the override minimal and document the upstream source version.
- GPT parsing bugs can select the wrong partition. Validation must read the filesystem boot sector before mount.
- exFAT increases FatFs code size and per-operation LFN working memory. P4 has PSRAM, but build size and heap should be checked in IDF build output.
- The current MSC stack reports 32-bit sector counts and uses READ10/WRITE10. That is acceptable for normal USB sticks but not a general-purpose large-disk implementation.
