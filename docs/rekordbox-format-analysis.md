# Rekordbox File Format Analysis

Validated on real USB drive (308 tracks, 2026-05-20).

## Context

Rekordbox USB drives contain analyzed metadata for each track. We can read this data on the P4 without our own audio analysis in the MVP stage — the waveform, BPM, beat grid, and cue points are already calculated on the PC.

**Medium: USB drive** (not SD card). Rekordbox formats the USB drive with a specific folder structure. The P4 reads tracks and metadata from this USB via the USB host interface.

**Filesystem support note (2026-06-29):** Newer AlphaTheta/rekordbox
OneLibrary-style USB exports still include `PIONEER/rekordbox/export.pdb` and
`PIONEER/USBANLZ`, and those files parse successfully on PC. The current P4 USB
MSC/FatFs path supports FAT32 only when the disk uses an MBR partition table. It
does not mount exFAT (`FF_FS_EXFAT=0`) and failed with FatFs
`FR_NO_FILESYSTEM` on the tested FAT32-on-GPT stick. After converting the same
stick to MBR + FAT32 and re-exporting, P4 read the USB library. exFAT and GPT
support remain firmware backlog items.

---

## Folder Structure on USB Drive

```
USB:/
  PIONEER/
    rekordbox/
      export.pdb         ← Pioneer Hardware Database (title, artist, anlz path, ...)
      exportLibrary.db   ← encrypted / OneLibrary database (NOT USED; export.pdb is primary when present)
    USBANLZ/
      P000/
        00000832/
          ANLZ0000.DAT   ← BPM, beatgrid, waveform, cues
          ANLZ0000.EXT   ← high-res waveform (PWV3) + newer tags
          ANLZ0000.2EX   ← color waveform (PWV7, PWV6) — newer format
        00000D18/
          ANLZ0000.DAT
          ...
      P001/
        ...
  Contents/
    Artist/
      track.mp3          ← audio files
```

**Audio → ANLZ mapping:** `export.pdb` contains the direct ANLZ path for each track (e.g., `/PIONEER/USBANLZ/P036/00023F66/ANLZ0000.DAT`). There is no need to iterate through all ANLZ directories — the PDB provides an O(1) lookup.

---

## Pioneer Hardware Database (`export.pdb`)

**Format reference:** https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/

Non-encrypted Pioneer-specific database. Used directly by CDJ hardware players without the Rekordbox software. It contains all metadata required for the USB media library.

### File structure

```
Offset 0x00:  magic (4B)
Offset 0x04:  page_size (4B LE) — usually 4096
Offset 0x08:  num_tables (4B LE)
Offset 0x1C:  table_pointers[] — 16B each:
                +0x00  type (4B LE)
                +0x04  empty_candidate (4B LE)
                +0x08  first_page (4B LE)
                +0x0C  last_page (4B LE)
```

### Table type IDs

| ID | Content |
|----|---------|
| 0x00 | Tracks |
| 0x01 | Genres |
| 0x02 | Artists |
| 0x03 | Albums |
| 0x04 | Labels |
| 0x05 | Keys |
| 0x07 | PlaylistTree |
| 0x08 | PlaylistEntries |
| 0x0D | Artwork |
| 0x11 | HistoryPlaylists |
| 0x12 | HistoryEntries |

### Page layout

Each page is `page_size` bytes (4096). Header (40B):

```
+0x04  page_index (4B LE)
+0x08  page_type  (4B LE)   — matches table type ID
+0x0C  next_page  (4B LE)   — 0xFFFFFFFF = end of table
+0x18  packed     (4B LE)   — lower 13 bits = num_row_offsets
+0x1E  used_size  (2B LE)
```

The heap starts at `page_base + 0x28`. The row slot table grows backward from the end of the page, in groups of max 16 slots. For each group (reading backward from the end):
- `[ptr-2]` tranrf (2B, ignored)
- `[ptr-4]` rowpf (16-bit bitmask, bit i = slot i is occupied)
- `[ptr-4-2*(i+1)]` heap_off[i] (2B, relative to `page_base + 0x28`)

### Track row layout

Subtype `0x0024` at offset 0 identifies a track row.

| Offset | Type | Content |
|--------|-----|---------|
| +0x38 | uint32 LE | BPM × 100 |
| +0x3C | uint32 LE | genre_id |
| +0x40 | uint32 LE | album_id |
| +0x44 | uint32 LE | artist_id |
| +0x48 | uint32 LE | track_id |
| +0x50 | uint16 LE | year |
| +0x54 | uint16 LE | duration (s) |
| +0x59 | uint8 | rating (0–5) |
| +0x5E | 21 × uint16 LE | string offset table (relative to row start) |

String indices in the offset table:

| Index | Content |
|--------|---------|
| 14 | **anlz_path** — `/PIONEER/USBANLZ/.../ANLZ0000.DAT` |
| 17 | comment |
| 18 | title |
| 19 | filename |
| 20 | file_path — `/Contents/...` |

### DeviceSQL string format

| Flag | Format |
|------|--------|
| `0x00`, `0x40` | Empty string (1B) |
| `flag & 1` (odd) | Short ASCII: total_field_len = `flag >> 1` (includes flag byte), data = `(flag>>1)-1` char |
| even, non-zero | Long: flag(1B) + total_len(2B LE) + pad(1B) + data. W bit (0x10) = UTF-16, E bit (0x80) = LE |

### Name table row layout (Artists, Albums)

```
+0x00  4B  internal link (ignored)
+0x04  4B  row_id (uint32 LE) — ID referenced by track row
+0x08  1B  empty DeviceSQL string (0x03)
+0x09  1B  unknown (0x0A)
+0x0A  ?B  name (DeviceSQL short ASCII)
```

### Implementation

- `firmware/main-deck-p4/components/library/include/rekordbox_pdb.h`
- `firmware/main-deck-p4/components/library/rekordbox_pdb.c`
- PC test harness: `tests/rekordbox_pdb/`

**API:**
```c
pdb_t *pdb;
esp_err_t rc = pdb_open("/usb/PIONEER/rekordbox/export.pdb", &pdb);
if (rc == ESP_OK) {
    for (int i = 0; i < pdb_track_count(pdb); i++) {
        pdb_track_t t;
        pdb_get_track(pdb, i, &t);
        // t.title, t.artist, t.file_path, t.anlz_path, t.bpm, t.duration_s
    }
    pdb_close(pdb);
}
```

**Validation (308 tracks, 2026-05-20):**
- 154/154 artists resolved
- 308/308 tracks have a valid ANLZ path
- 9/9 PC unit tests PASS

---

## ANLZ File Format

Both files use **big-endian** byte order. The structure is tag-based and **sequential**:

```
Each tag starts with:
  [4B  tag ID   ]  e.g., 0x50505448 = 'PPTH'
  [4B  hdr_size ]  header size including these 12B at the start
  [4B  seg_size ]  total tag size (header + data)
  [(hdr_size - 12)B  additional header fields ]
  [(seg_size - hdr_size)B  data             ]

PMAI is special: seg_size = total file size; hdr_size = 28.
All other tags follow sequentially after the PMAI header.
```

**Parser Note:** Tags are searched via a forward-only sliding window scan (`fgetc()` byte-by-byte). Backward `fseek()` is not used as it would destroy FILE buffering on the USB drive.

---

## Sections in ANLZ0000.DAT

Verified header sizes from a real USB drive:

| Tag | hdr_size | Content | Note |
|-----|----------|---------|---------|
| `PMAI` | 28 | File header | seg_size = entire file |
| `PPTH` | 16 | UTF-16 BE path to audio file | 4B path_length in header |
| `PVBR` | 16 | 400/401 × 4B VBR seek offsets | Cap at 400 |
| `PQTZ` | 24 | Beat grid entries | 12B extra header |
| `PWAV` | 20 | 400B low-res waveform | 8B extra header |
| `PWV2` | 20 | 100B tiny waveform (4-bit/entry) | Not parsed |
| `PCOB` | 24 | Cue/loop container (PCPT sub-records) | Can be empty (0 hot cues) |

### PCOB / PCPT — Cue Point Structure (56 bytes per entry)

```c
// Offsets within the 56-byte PCPT entry (big-endian):
uint8_t  entry_type;  // byte 0: 1 = single point, 2 = loop
uint8_t  index;       // byte 1: hot cue slot (0–7)
// bytes 2–3: color/unknown
uint32_t start_ms;    // bytes 4–7: position from track start (ms)
uint32_t end_ms;      // bytes 8–11: loop end (only if type=2)
// bytes 12–55: name, color info (not used)
```

### PQTZ — Beat Grid Entry (8 bytes, big-endian)

```c
uint16_t beat_phase;  // position within the bar (0 = downbeat)
uint16_t bpm_x100;    // BPM × 100 (e.g., 13611 = 136.11 BPM)
uint32_t time_ms;     // absolute time from track start (ms)
```

---

## Sections in ANLZ0000.EXT

| Tag | hdr_size | Content |
|-----|----------|---------|
| `PMAI` | 28 | File header |
| `PPTH` | 16 | Same path as in DAT |
| `PWV3` | 24 | High-res waveform — up to ~62 KB (max 128 KB limit) |
| `PCOB` | 24 | Cue/loop (usually empty like in DAT) |
| `PCO2` | 20 | Newer cue format — not parsed |
| `PQT2` | 56 | Newer beat grid — not parsed |
| `PWV5` | 24 | Alternative waveform format — not parsed |
| `PWV4` | 24 | Alternative waveform format — not parsed |

## Sections in ANLZ0000.2EX (Newer Format)

| Tag | Content |
|-----|---------|
| `PWV7` | Color waveform (up to 105 KB) |
| `PWV6` | Alternative color waveform |
| `PWVC` | Waveform checksum/metadata |

---

## ANLZ Parser Implementation

**Files:**
- `firmware/main-deck-p4/components/library/include/rekordbox_anlz.h`
- `firmware/main-deck-p4/components/library/rekordbox_anlz.c`

**Struct to be populated:**

```c
typedef struct {
    char audio_path[512];           // from PPTH (UTF-16 BE → ASCII)

    anlz_beat_t *beats;             // heap: PQTZ beat grid
    uint16_t     beat_count;
    uint16_t     bpm;               // rounded from bpm_x100

    anlz_cue_t cues[8];             // from PCOB
    uint8_t    cue_count;

    uint32_t vbr[400];              // from PVBR (VBR seek table)
    bool     has_vbr;

    uint8_t  waveform_low[400];     // from PWAV
    bool     has_waveform_low;

    uint8_t  *waveform_high;        // heap: from PWV3 in .EXT (up to ~62 KB)
    uint32_t  waveform_high_len;
} anlz_metadata_t;
```

**Known Bugs Found via Testing (All Fixed in Code):**

1. `ANLZ_TAG_PQTZ` typo: `0x50515457` ("PQTW") → `0x5051545A` ("PQTZ")
2. `find_tag()` infinite loop on Windows USB drive: `fseek(-3)` cleared FILE buffer; replaced with `fgetc()` forward-only sliding window.
3. `ANLZ_WAVEFORM_HIGH_MAX`: increased from 60,000 to 131,072 (actual maximum is ~62 KB).

---

## How We Use This on the P4

| P4 Component | Rekordbox Data | Source |
|--------------|----------------|--------|
| `library/` | title, artist, album, file_path | `export.pdb` |
| `library/` | anlz_path (direct, no walker required) | `export.pdb` |
| `audio_engine/` | VBR seek offsets for fast MP3 seeks | ANLZ PVBR |
| `deck_core/` | Precise BPM, beat grid positions in ms | ANLZ PQTZ |
| `deck_core/` | Hot cue start/end positions | ANLZ PCOB/PCPT |
| LVGL UI | Waveform display 400px low-res | ANLZ PWAV |
| LVGL UI | Waveform high-res for zoom | ANLZ PWV3 |

---

## Limitations of the Current Parser

- `exportLibrary.db` — SQLCipher encrypted database (Rekordbox 6), not used; we use `export.pdb` instead.
- `PCO2`, `PQT2`, `PWV5`, `PWV4`, `PWV7`, `PWV6`, `PWVC` — newer tags are not parsed.
- SPIRAM required for in-memory PDB index (~281 KB for 308 tracks) — `CONFIG_SPIRAM=y` in `sdkconfig.defaults`.

---

## Validation

**PC Test Harness:**

```powershell
# ANLZ unit tests (32 synthetic + real files)
cd tests/anlz
mingw32-make test

# PDB unit tests + real USB drive
cd tests/rekordbox_pdb
mingw32-make test
.\test_pdb.exe F:\PIONEER\rekordbox\export.pdb
```

Requires GCC 16.1.0 (WinLibs MinGW UCRT, `winget install BrechtSanders.WinLibs.POSIX.UCRT`).

**Results on a Real USB Drive (308 tracks, 2026-05-20):**

ANLZ:
- 308/308 validation passed (Python parse_anlz.py)
- Darude Sandstorm: BPM=136.11, 527 beats, 3:51, PWV3=34,839B ✅
- Patrick Cowley Do You Wanna Funk: BPM=129.97, 899 beats, 6:54, PWV3=62,216B ✅

PDB:
- 154 artists, 308 tracks, 0 albums (tracks are not tagged with albums)
- 308/308 tracks have a valid ANLZ path (/PIONEER/USBANLZ/...) ✅
- 9/9 PC unit tests PASS ✅

---

## References

- https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/
- https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/anlz.html
- https://github.com/Deep-Symmetry/crate-digger
