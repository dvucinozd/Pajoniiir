# Legacy M3 Hot Cue migration

The legacy `hotcue` namespace binds `hc%08x` records to a track ID or path hash.
Those keys cannot distinguish two Rekordbox exports with the same track ID.
The shared runtime therefore never imports them automatically. It reads source
ANLZ cues and persistent `hotcue_v3`/`hotcue_v2` records independently.

`tools/migrate_m3_hot_cues.py` prepares a new NVS partition offline. It performs
no flash or API mutation. Keep the original NVS/recovery backup for wired return.
Backups and merged NVS images contain settings, including Wi-Fi credentials;
store them with the same access restrictions as the original device backup.
The JSON report contains cue mappings and archived cue blobs, not other settings.

## Required evidence

1. An unencrypted, healthy, exactly 24 KiB NVS backup and its selected SHA-256.
2. The explicitly selected **old** `PIONEER/rekordbox/export.pdb` that created
   the legacy cues, with its associated audio files on the mounted USB medium.
3. A catalog capture from the new `main-deck-m3` runtime with both decks stopped.
4. ESP-IDF 6.0.2 and a host GCC compiler. The production C PDB parser is compiled
   for the selected export; the migration does not implement a second PDB parser.
   The NVS generator Python package is pinned to `esp-idf-nvs-partition-gen==0.1.9`
   in CI. The parser comes from ESP-IDF commit
   `7101770dc6db2667b3c477cc31365dd1acd6db4e` (6.0.2).

The first shared-core boot preserves the old namespace. Capture the backup used
for the merge after any settings migration, then keep it unchanged until apply.
An NVS merge must be applied in the wired bootloader with a fresh hash guard;
there is deliberately no web endpoint that can overwrite NVS during playback.

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
$env:Path = "$env:Path;C:\msys64\ucrt64\bin"
python tools/migrate_m3_hot_cues.py capture `
  --device-url http://192.168.4.1 --output D:\Backups\M3\catalog.json
python tools/migrate_m3_hot_cues.py prepare `
  --backup D:\Backups\M3\nvs.bin --backup-sha256 <selected-backup-sha256> `
  --old-export E:\PIONEER\rekordbox\export.pdb `
  --catalog D:\Backups\M3\catalog.json --output-dir D:\Backups\M3\cue-merge
```

## Mapping and preservation rules

The API returns at most eight rows per page, with library generation, export
digest, path, file size, FAT modification time and full persistent identity.
Only the P4 determines FAT timestamps. Windows `stat()` is never used to create
cue identities. Changing media invalidates the capture; capture again instead
of combining pages from different generations.

The selected export SHA must match every catalog row. Its production PDB parser
must produce exactly one old row for a legacy key, and the captured medium must
contain exactly one matching path and track identity. Missing audio, duplicate
IDs, duplicate target keys, invalid legacy cue blobs and conflicting existing
persistent cues are archived with a reason. Existing persistent cues win.

Only present legacy slots become overrides. An absent old slot cannot prove a
source cue was intentionally deleted, so migration does not invent tombstones.
Loop end must follow start; single cues retain their start and clear loop end.
The new v3 record embeds the complete media identity and production CRC format.

The tool preserves every logical NVS key, type and value, all namespace names,
and all old cue blobs. It reconstructs chunked blobs using the official parser,
checks page/entry/payload CRCs and rejects ambiguous live keys or pending page
recovery. Generation is followed by a complete logical round-trip comparison.
It publishes the output directory only after that comparison succeeds. A failed
or interrupted preparation leaves the input backup intact. An existing output
directory is never overwritten.

Outputs: `original-nvs.bin`, `merged-nvs.bin`, `report.json`. Review `mapped` and
`archived`, including the export and both NVS hashes, before wired apply. After
apply and reboot, verify cue recall, source/local merge, delete/restore and the
preserved settings on the exact installed image. Offline software checks do not
constitute physical cue acceptance.

## Cache invalidation

The shared metadata cache uses version 5 and full persistent media identity;
legacy M3 version-2 cache files are rebuilt. Cache invalidation never erases NVS
cue namespaces. Session generation remains an admission fence, not a durable
cue identity.
