# P4 dual-USB hotplug and OTA smoke — 2026-08-29

## Scope and verdict

This record covers the first focused hardware pass after the P4-only USB host
recovery and MSC teardown remediation on `feat/p4-dual-usb-host`.

Verdict: **PASS for one signed OTA reboot and one USB0 remove/reinsert cycle
while the direct USB1 DDJ-FLX4 path remained active.** The P4 did not reboot,
the Rekordbox volume remounted, the Library reloaded and subsequent track loads
completed. This closes the focused hotplug reproduction. It does not qualify
the common 5 V/VBUS path or replace the repeated reboot, insertion-order and
long dual-active soak gates.

## Source and artifact identity

- Branch: `feat/p4-dual-usb-host`.
- Runtime-reported version: `RC2-106-gfa55e43-dirty`.
- The tested source was committed immediately afterward as `77aa23a`
  (`fix(p4): harden dual USB hotplug and media mount`) without intervening
  firmware changes. Because the image was built before that commit, this is not
  an exact-commit artifact claim.
- Required toolchain: ESP-IDF v6.0.2; `idf.py --version` reported
  `ESP-IDF v6.0.2`.
- Application image: 2,449,552 bytes, 1,744,752 bytes free in the 4 MiB OTA
  slot, SHA-256
  `a8f377bd4b310338cae545c1f7b07b0da60bdc415569993e4c7c416d912faa1a`.
- Signed bundle: `main-deck-p4.ddjota`, ECDSA-P256-SHA256 key ID `rel-001`.
- OTA upload returned HTTP success with `{"ok":true,"rebooting":true}` and the
  P4 returned on `ota_0` with transfer service `state=idle` and no OTA error.

The ignored local package directory was
`releases/pajoniiir-RC2-106-gfa55e43-dirty/`; release artifacts were not added
to Git.

## Remediation under test

The accepted source keeps USB ownership bounded across disconnects:

- root recovery powers down one indexed port only when the HCD still reports
  that root as disconnected and has no pending attach event; active
  attach/enumeration suppresses the recovery attempt;
- MSC teardown retires the transfer before deleting its callback semaphore and
  detaches the sole-owner handles before VFS/device destruction;
- MSC uses one fixed 8 KiB DMA-capable transfer for the device lifetime instead
  of freeing and reallocating the transfer during I/O;
- FatFs reads and writes larger than 8 KiB are split into bounded SCSI
  READ10/WRITE10 transactions. This avoids the 32 KiB contiguous internal-DMA
  allocation which returned `ESP_ERR_NO_MEM` after the first hotplug attempt;
- desired/current storage state, mount retries and lifecycle counters remain
  owned by the storage task and are exposed through `/api/status`.

The 8 KiB bound was selected after comparing the related `PAJONIIIR-M3`
implementation, which starts with a 64-byte MSC transfer and grows it on demand.
The P4-only product deliberately retains fixed ownership to avoid the observed
disconnect/free race, while using the smaller bounded size to avoid internal
DMA-heap fragmentation.

## Software verification

`tests/run_p4_host_tests.ps1` exited with code 0. Relevant results included:

- `audio_engine`: 393 PASS / 0 FAIL;
- `usb_storage_session`: 73 checks passed;
- `usb_storage_recovery`: 88 checks passed;
- `controller_audio_resampler`: passed;
- static source contracts for indexed idle recovery, active-enumeration
  suppression, teardown ownership, 8 KiB fixed transfer and byte-bounded media
  transactions: passed;
- OTA signing and release-helper tests: passed.

`idf.py -B build_signed build` also exited with code 0 under ESP-IDF v6.0.2.
The application occupied 58% of the 4 MiB slot and passed the stricter P4-only
binary budget with 1,220,464 bytes remaining.

## Hardware sequence and evidence

After OTA reboot, both roots were already populated:

- USB0 storage mounted on the first attempt and loaded a 100-track library;
- USB1 enumerated the DDJ-FLX4 as `2B73:0045`, activated
  `pioneer_ddj_flx4`, and reported MIDI IN, MIDI OUT and USB Audio available;
- root power mask was `3`, meaning both indexed roots were powered.

The operator then removed and reinserted the USB0 medium while the FLX4 stayed
connected. The service journal for boot 365 recorded:

```text
ms=23809 event=USB_UNMOUNTED msg=drive removed
ms=30106 event=USB_MOUNTED msg=rekordbox drive
ms=30159 event=LIBRARY_LOADED a0=100
```

The post-test status snapshot reported:

| Signal | Result |
| --- | --- |
| P4 retained boot trace | remained at boot 15; no hotplug reboot |
| Storage connect events | 2 received / 2 accepted |
| Storage disconnect events | 1 received / 1 accepted |
| Mounts | 2 attempts / 2 successes |
| Last mount | `ESP_OK` |
| Last unmount | `ESP_OK` |
| Last MSC uninstall | `ESP_OK` |
| USB host daemon errors | 0 |
| Root recovery failures / queue drops | 0 / 0 |
| FLX4 | connected, active profile, MIDI and USB Audio available |

The same journal then recorded successful loads of track keys 9 and 11,
including `Bon Jovi - Bad Medicine.mp3` and
`Pink Floyd - Breathe (In the Air).mp3`. This proves the remounted filesystem
was not merely present in status; media paths remained readable after hotplug.

The status endpoint still exposed an older retained coredump from the prior USB
panic investigation. The current boot reason was software reset from the OTA,
and no new panic or coredump was produced by this hotplug cycle.

## Remaining acceptance gates

- repeat cold boot, OTA/software reboot and both physical insertion orders;
- run repeated USB0 and USB1 independent disconnect/reconnect cycles, including
  removal during active load/decode and controller traffic;
- sustain simultaneous two-deck playback, storage cache misses, FLX4 MIDI/LED
  activity and four-channel UAC output for at least 30 minutes, followed by a
  multi-hour product soak;
- measure the common 5 V rail and each protected downstream VBUS during startup
  and combined load; confirm current limiting, isolation and zero backfeed;
- record internal/DMA heap floor, task stacks, audio deadline/underrun counters
  and direct-UAC data-loss counters throughout the soak;
- repeat with verified physical MP3, WAV and FLAC fixtures.
