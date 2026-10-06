# Preserving M3 shared-core migration

This is the one-time wired transition from the historical M3 application
(`main-deck-p4`) to the shared canonical application's identity (`main-deck-m3`).
The accepted old image remains a wired recovery artifact. It cannot install a
different project's OTA bundle. Later updates use signed M3 OTA only.

The canonical repository is `dvucinozd/Pajoniiir`; the M3 repository is historical
source/validation/recovery. It has not been remotely archived. No existing tag,
release asset or public channel is modified by the migration tools.

## Preconditions and immutable candidate

Use ESP-IDF 6.0.2 and esptool 5.3.1 from its PowerShell profile. Build and package
a clean source, push the development branch, and require every exact-SHA CI job.
`tools/create_integration_candidate.py` verifies the successful CI evidence,
ordinary configuration, signatures, descriptor, source, fixed budget, locks and
artifact hashes. Its `candidate-evidence.json` records hardware NOT RUN.
`image_elf_sha256` identifies the embedded build; `image.sha256` is the complete
`.bin` hash, and `bundle.sha256` is the signed `.ddjota` hash.

The device must be explicitly identified as M3. Supply its COM port, MAC, old
slot and exact old image hash after reviewing backup evidence. Historical M3
project text alone cannot distinguish it from JC4880. Stop playback, keep stable
power and wired access, and preserve the private backup locally. It contains NVS
settings and may contain credentials. `.cache/` is ignored by Git.

The tool supports the existing 16 MiB layout only: NVS 0x9000/0x6000,
OTA selection 0x10000/0x2000, factory 0x20000/0x400000,
ota_0 0x420000/0x400000, ota_1 0x820000/0x400000 and coredump 0xc20000/0x10000.
It validates partition-table MD5, offsets, sizes and flags, ESP32-P4 pre-v3
revision, flash capacity and security mode. Secure boot/flash encryption or
another layout is rejected. There is no automatic destructive fallback.

## Backup and offline review

Replace all placeholders with the reviewed device/artifact values:

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
python tools/migrate_m3_factory.py backup --port COM_NUMBER --output-dir .cache/m3-migration/backup
```

Backup captures full 16 MiB flash plus bootloader, partition table, NVS, PHY,
OTA selection, factory, both OTA slots and coredump as separately hashed files.
It validates recoverable application checksum/appended SHA and records their
project/version/ELF/complete-image digests. Invalid or empty unused slots are
reported and still saved. Backup leaves the board in its ROM/stub bootloader;
it does not boot the old application between backup and apply.

```powershell
python tools/migrate_m3_factory.py plan --backup-dir .cache/m3-migration/backup --release-dir RELEASE_DIRECTORY --public-key firmware/common/ota_manifest/keys/ddj_ota_release_public.der --expected-mac DEVICE_MAC --old-slot ota_0 --old-image-sha256 OLD_IMAGE_SHA256 --result .cache/m3-migration/plan.json
```

`plan` is entirely offline and writes a reviewable JSON result. It requires a
verified signed M3 candidate, the selected historical `M3*`/`main-deck-p4` image,
matching MAC and exact old hash. Candidate partition data must match the existing
table. Review artifact identities, backup hashes and unchanged settings/layout.

## Apply and first boot

```powershell
python tools/migrate_m3_factory.py apply --port COM_NUMBER --backup-dir .cache/m3-migration/backup --release-dir RELEASE_DIRECTORY --public-key firmware/common/ota_manifest/keys/ddj_ota_release_public.der --expected-mac DEVICE_MAC --old-slot ota_0 --old-image-sha256 OLD_IMAGE_SHA256 --result .cache/m3-migration/wired-result.json
```

Apply checks the connected device and rereads the entire flash against the
backup. A stale snapshot refuses all writes. It writes only the new factory
application, reads it back byte-for-byte, verifies NVS/bootloader/partition data,
then clears the existing two OTA-selection sectors and verifies that operation.
Only then does it request the first factory boot. It never erases the full chip,
rewrites the bootloader/table or clears NVS. Old OTA-slot bytes are retained until
the subsequent OTA installation needs its inactive slot.

If a write/readback fails, the result records the last stage and failure; no
automatic reboot is issued. Keep the device in bootloader and review recovery.
Restore only affected regions from the hashed snapshot using esptool: factory at
0x20000, NVS at 0x9000 if changed, and original `otadata.bin` at 0x10000 last.
Read back and compare hashes before a manual reset. No full-chip erase is needed.
Do not blindly write a new bootloader or partition table for this transition.

## Signed OTA and startup health

Confirm expected board/project/source/ELF in `/api/firmware`, factory slot,
Wi-Fi ON, display/backlight and preserved operator settings before proceeding.
Stop both decks. This step deliberately performs a signed local OTA installation:

```powershell
python tools/migrate_m3_factory.py finish --base-url http://192.168.4.1 --release-dir RELEASE_DIRECTORY --public-key firmware/common/ota_manifest/keys/ddj_ota_release_public.der --wired-result .cache/m3-migration/wired-result.json --result .cache/m3-migration/ota-result.json
```

Finish revalidates the signed image/bundle, exact installed factory identity and
idle transport, POSTs with the production control/OTA markers, and observes the
new ota_0 image until `running_image_state=valid` and transfer state `idle`.
Pending startup has the production 60-second deadline; the host's 90-second
observation window includes reboot/reconnection. HTTP 200 alone is insufficient.
Failure or rollback is recorded as not accepted. Operator settings and audible/
visual acceptance remain explicit NOT RUN in this machine-generated evidence.
Positive health confirmation does not establish timeout/rollback fault tests.

## Legacy cues after the new catalog is available

Legacy track-ID-only blobs are preserved but never automatically assigned to a
new media namespace. Capture the new catalog and explicitly select the old
Rekordbox export using [the cue migration procedure](M3_CUE_MIGRATION.md).
Only unique mappings enter the new namespace; ambiguous records remain archived.

For device apply, capture the catalog first, stop playback, take a fresh wired
backup of the installed shared candidate, then prepare the merged NVS using that
backup's `nvs.bin`. Do not boot between this fresh backup and apply. The cue-only
command requires the current installed candidate hash, matching source/catalog
and exact fresh NVS backup hash:

```powershell
python tools/migrate_m3_factory.py apply-cues --port COM_NUMBER --backup-dir FRESH_SHARED_BACKUP --release-dir RELEASE_DIRECTORY --public-key firmware/common/ota_manifest/keys/ddj_ota_release_public.der --expected-mac DEVICE_MAC --old-slot ota_0 --old-image-sha256 NEW_IMAGE_SHA256 --cue-dir PREPARED_CUE_DIRECTORY --result .cache/m3-migration/cue-result.json
```

This writes/readbacks NVS only; factory, OTA slots, table and OTA selection remain
unchanged. Confirm mapped cue set/delete/restore/reboot on hardware. Cache version
invalidation rebuilds metadata and does not erase cue storage.

## Acceptance and publication

Software tests cover signatures, board/legacy identity, corrupt tables/images,
stale backup, readback failure, interrupted selection write, settings preservation,
cue-only ordering and pending-to-VALID observation with a mocked device/API.
Actual serial writes, first boot/settings, OTA health/rollback, GUI/touch/controls,
MAIN/PFL, mixed-rate MT/seek/CUE/loops and waveform timing remain NOT RUN until
performed on the exact candidate.

Complete Campaign A (10 cold boots, 10 stopped-deck FLX4 reconnects,
5 USB3-first and 5 FLX4-first cycles) and at least 60 minutes of worst-case Campaign
B using [the read-only monitor](RELIABILITY_MONITORING_PLAN.md). A software or
telemetry PASS never substitutes for operator audio/UI confirmation. Isolated
output-late findings retain their documented monitoring status, not a zero-late
claim. JC4880/JC1060 and additional controllers need their own physical gates.
Update a public board channel only after that target's acceptance and separate
publication authorization.
