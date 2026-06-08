#!/usr/bin/env python3
"""
inspect_pdb.py - Pioneer PDB format inspector
Reads export.pdb from a Rekordbox USB drive and dumps track metadata.

Usage:
    python inspect_pdb.py F:\PIONEER\rekordbox\export.pdb
    python inspect_pdb.py F:\PIONEER\rekordbox\export.pdb --limit 20
    python inspect_pdb.py F:\PIONEER\rekordbox\export.pdb --raw-page 53
"""

import struct, sys, argparse

# ---------------------------------------------------------------------------
# Table type IDs (from Deep Symmetry rekordbox-export-analysis)
# ---------------------------------------------------------------------------
TABLE_TYPES = {
    0x00: "Tracks",
    0x01: "Genres",
    0x02: "Artists",
    0x03: "Albums",
    0x04: "Labels",
    0x05: "Keys",
    0x06: "Colors",
    0x07: "PlaylistTree",
    0x08: "PlaylistEntries",
    0x0D: "Artwork",
    0x10: "Columns",
    0x11: "HistoryPlaylists",
    0x12: "HistoryEntries",
    0x13: "History",
}

# Track row string indices (0-based)
STR_ANALYZE_PATH = 14   # path to ANLZ file on USB  e.g. /PIONEER/USBANLZ/P000/00000001/ANLZ0000.DAT
STR_COMMENT      = 17
STR_TITLE        = 18   # often empty for non-ID3-tagged files
STR_FILENAME     = 19   # mp3/flac filename without directory
STR_FILE_PATH    = 20   # full audio path on USB

# Track row fixed field offsets (little-endian)
TRACK_OFF_TEMPO      = 0x38   # uint32, BPM * 100
TRACK_OFF_GENRE_ID   = 0x3C   # uint32
TRACK_OFF_ALBUM_ID   = 0x40   # uint32
TRACK_OFF_ARTIST_ID  = 0x44   # uint32
TRACK_OFF_TRACK_ID   = 0x48   # uint32  (own ID, matches ANLZ folder numeric ID)
TRACK_OFF_DURATION   = 0x54   # uint16, seconds
TRACK_OFF_YEAR       = 0x50   # uint16
TRACK_OFF_RATING     = 0x59   # uint8 (0-5)
TRACK_OFF_STR_OFFS   = 0x5E   # start of 21 × 2B string offsets (relative to row start)

TRACK_ROW_SUBTYPE = 0x0024     # first 2 bytes of a track row

# ---------------------------------------------------------------------------
# DeviceSQL string decoder
# ---------------------------------------------------------------------------

def read_devicesql(data, abs_off):
    """
    Decode a DeviceSQL string.
    Returns (string_value, total_bytes_consumed).
    string_value may be '' for empty/null strings.
    """
    if abs_off >= len(data):
        return '', 1

    flag = data[abs_off]

    # Empty / null
    if flag == 0x40 or flag == 0x00:
        return '', 1

    # Short ASCII string: low bit set.
    # total_field_len (including flag byte) = flag >> 1
    # data bytes = (flag >> 1) - 1
    if flag & 1:
        total = flag >> 1
        nbytes = total - 1
        if nbytes <= 0:
            return '', total
        raw = data[abs_off + 1 : abs_off + 1 + nbytes]
        return raw.decode('ascii', errors='replace').rstrip('\x00'), total

    # Long string: flag + uint16 total_len + 1 pad byte + string data
    # total_len includes all header bytes (4 bytes overhead)
    if abs_off + 3 >= len(data):
        return '', 1
    total_len = struct.unpack_from('<H', data, abs_off + 1)[0]
    # pad byte at abs_off+3
    data_start = abs_off + 4
    data_len   = total_len - 4
    if data_len <= 0:
        return '', total_len

    raw = data[data_start : data_start + data_len]

    # Encoding based on flags
    wide = bool(flag & 0x10)   # W bit: UTF-16
    le   = bool(flag & 0x80)   # E bit: little-endian

    if wide:
        enc = 'utf-16-le' if le else 'utf-16-be'
        try:
            return raw.decode(enc, errors='replace').rstrip('\x00'), total_len
        except Exception:
            return raw.decode('utf-8', errors='replace').rstrip('\x00'), total_len
    else:
        return raw.decode('ascii', errors='replace').rstrip('\x00'), total_len


def read_track_string(data, row_base, str_idx):
    """Read string str_idx from a track row at row_base."""
    off_pos = row_base + TRACK_OFF_STR_OFFS + str_idx * 2
    str_off = struct.unpack_from('<H', data, off_pos)[0]
    val, _ = read_devicesql(data, row_base + str_off)
    return val


# ---------------------------------------------------------------------------
# PDB file reader
# ---------------------------------------------------------------------------

class PDBFile:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()

        if len(self.data) < 28:
            raise ValueError("File too short")

        self.page_size  = struct.unpack_from('<I', self.data, 4)[0]
        self.num_tables = struct.unpack_from('<I', self.data, 8)[0]
        self.total_pages = len(self.data) // self.page_size

        # Table pointers: 16 bytes each, starting at offset 0x1C = 28
        self.tables = []
        for i in range(self.num_tables):
            off = 0x1C + i * 16
            t = {
                'type':            struct.unpack_from('<I', self.data, off +  0)[0],
                'empty_candidate': struct.unpack_from('<I', self.data, off +  4)[0],
                'first_page':      struct.unpack_from('<I', self.data, off +  8)[0],
                'last_page':       struct.unpack_from('<I', self.data, off + 12)[0],
            }
            t['name'] = TABLE_TYPES.get(t['type'], 'Unknown_%02X' % t['type'])
            self.tables.append(t)

    def page_offset(self, page_num):
        return page_num * self.page_size

    def page_header(self, page_num):
        off = self.page_offset(page_num)
        # Packed 24-bit field at offset 0x18: lower 13 bits = num_row_offsets
        packed = struct.unpack_from('<I', self.data, off + 0x18)[0] & 0x00FFFFFF
        return {
            'page_index':     struct.unpack_from('<I', self.data, off + 0x04)[0],
            'page_type':      struct.unpack_from('<I', self.data, off + 0x08)[0],
            'next_page':      struct.unpack_from('<I', self.data, off + 0x0C)[0],
            'num_row_offsets': packed & 0x1FFF,
            'used_size':      struct.unpack_from('<H', self.data, off + 0x1E)[0],
        }

    def row_heap_offsets(self, page_num):
        """
        Return list of (heap_offset, is_present) for all row slots.
        heap_offset is relative to page offset 0x28 (heap start).
        Row absolute position = page_base + 0x28 + heap_offset.

        Slot table grows backwards from page end.  Rows are grouped in
        blocks of up to 16.  Each block (reading from end):
          [ptr-2]            tranrf  (ignore)
          [ptr-4]            rowpf   (16-bit bitmask: bit i = row i present)
          [ptr-6]            heap_off for row 0 of block
          [ptr-8]            heap_off for row 1 of block
          ...
          [ptr-4-m*2]        heap_off for row (m-1) of block
        Then next block starts at ptr - (4 + m*2).
        """
        h = self.page_header(page_num)
        n = h['num_row_offsets']
        if n == 0:
            return []

        page_base = self.page_offset(page_num)
        ptr = page_base + self.page_size   # reading pointer (grows backwards)

        results = []
        idx = 0
        while idx < n:
            m = min(16, n - idx)           # rows in this group
            rowpf = struct.unpack_from('<H', self.data, ptr - 4)[0]
            for i in range(m):
                heap_off = struct.unpack_from('<H', self.data, ptr - 4 - 2*(i+1))[0]
                present  = bool(rowpf & (1 << i))
                results.append((heap_off, present))
            ptr -= 4 + m * 2              # tranrf(2) + rowpf(2) + m*ofs(2 each)
            idx += m
        return results

    def table_pages(self, table):
        """Yield page numbers in a table's linked chain."""
        page_num = table['first_page']
        visited = set()
        while page_num != 0xFFFFFFFF and page_num < self.total_pages:
            if page_num in visited:
                break
            visited.add(page_num)
            yield page_num
            h = self.page_header(page_num)
            page_num = h['next_page']


# ---------------------------------------------------------------------------
# Name table reader (Artists, Albums, Genres)
# Named rows: id (uint32) + unknown(4B) + name (DeviceSQL string)
# ---------------------------------------------------------------------------

def read_name_table(pdb, table):
    """
    Read id→name mapping from a simple name table (Artists, Albums, Genres).
    Row layout:
      +0x00  4B  internal link/ref (ignored)
      +0x04  4B  row_id  (uint32 — the ID that track rows reference)
      +0x08  1B  empty DeviceSQL string (0x03)
      +0x09  1B  unknown byte
      +0x0A  ?B  name string (DeviceSQL short ASCII)
    """
    result = {}
    for page_num in pdb.table_pages(table):
        page_base = pdb.page_offset(page_num)
        for heap_off, present in pdb.row_heap_offsets(page_num):
            if not present:
                continue
            row_base = page_base + 0x28 + heap_off
            if row_base + 14 > len(pdb.data):
                continue
            row_id = struct.unpack_from('<I', pdb.data, row_base + 4)[0]
            name, _ = read_devicesql(pdb.data, row_base + 10)
            if name and row_id > 0:
                result[row_id] = name
    return result


# ---------------------------------------------------------------------------
# Track iterator
# ---------------------------------------------------------------------------

def iter_tracks(pdb):
    """
    Yield dicts for each track in the Tracks table.
    Keys: track_id, bpm, duration, artist_id, album_id, genre_id, year, rating,
          title, filename, file_path, analyze_path, comment
    """
    tracks_table = next((t for t in pdb.tables if t['type'] == 0x00), None)
    if not tracks_table:
        return

    for page_num in pdb.table_pages(tracks_table):
        page_base = pdb.page_offset(page_num)
        h = pdb.page_header(page_num)

        for heap_off, present in pdb.row_heap_offsets(page_num):
            if not present:
                continue
            row_base = page_base + 0x28 + heap_off
            if row_base + TRACK_OFF_STR_OFFS + 42 > len(pdb.data):
                continue

            # Verify track row subtype
            subtype = struct.unpack_from('<H', pdb.data, row_base)[0]
            if subtype != TRACK_ROW_SUBTYPE:
                continue

            t = {
                'track_id':    struct.unpack_from('<I', pdb.data, row_base + TRACK_OFF_TRACK_ID)[0],
                'bpm':         struct.unpack_from('<I', pdb.data, row_base + TRACK_OFF_TEMPO)[0] / 100.0,
                'duration':    struct.unpack_from('<H', pdb.data, row_base + TRACK_OFF_DURATION)[0],
                'year':        struct.unpack_from('<H', pdb.data, row_base + TRACK_OFF_YEAR)[0],
                'rating':      pdb.data[row_base + TRACK_OFF_RATING],
                'artist_id':   struct.unpack_from('<I', pdb.data, row_base + TRACK_OFF_ARTIST_ID)[0],
                'album_id':    struct.unpack_from('<I', pdb.data, row_base + TRACK_OFF_ALBUM_ID)[0],
                'genre_id':    struct.unpack_from('<I', pdb.data, row_base + TRACK_OFF_GENRE_ID)[0],
                'title':       read_track_string(pdb.data, row_base, STR_TITLE),
                'filename':    read_track_string(pdb.data, row_base, STR_FILENAME),
                'file_path':   read_track_string(pdb.data, row_base, STR_FILE_PATH),
                'analyze_path':read_track_string(pdb.data, row_base, STR_ANALYZE_PATH),
                'comment':     read_track_string(pdb.data, row_base, STR_COMMENT),
            }
            yield t


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('pdb_path')
    parser.add_argument('--limit', type=int, default=None)
    parser.add_argument('--raw-page', type=int, default=None)
    parser.add_argument('--tables', action='store_true')
    args = parser.parse_args()

    pdb = PDBFile(args.pdb_path)

    if args.raw_page is not None:
        off = pdb.page_offset(args.raw_page)
        h = pdb.page_header(args.raw_page)
        print('Page %d: type=0x%02X next=%d num_rows=%d used=%d' % (
            args.raw_page, h['page_type'], h['next_page'],
            h['num_row_offsets'], h['used_size']))
        for i, (ho, pres) in enumerate(pdb.row_heap_offsets(args.raw_page)):
            print('  slot %d: heap_off=0x%04X present=%s' % (i, ho, pres))
        return

    if args.tables:
        print('Tables (%d):' % pdb.num_tables)
        for i, t in enumerate(pdb.tables):
            print('  [%2d] type=0x%02X %-20s first=%d last=%d' % (
                i, t['type'], t['name'], t['first_page'], t['last_page']))
        print()

    # Build lookup tables
    artists_t = next((t for t in pdb.tables if t['type'] == 0x02), None)
    albums_t  = next((t for t in pdb.tables if t['type'] == 0x03), None)
    artists = read_name_table(pdb, artists_t) if artists_t else {}
    albums  = read_name_table(pdb, albums_t)  if albums_t  else {}
    print('Loaded %d artists, %d albums' % (len(artists), len(albums)))

    # Dump tracks
    count = 0
    ok = 0
    hdr = '%-4s %-6s %-7s %-25s %-35s %-40s'
    print(hdr % ('#', 'ID', 'BPM', 'Artist', 'Title/Filename', 'Path'))
    print('-' * 120)

    for t in iter_tracks(pdb):
        count += 1
        if args.limit and count > args.limit:
            break

        artist = artists.get(t['artist_id'], '[%d]' % t['artist_id'])
        album  = albums.get(t['album_id'],   '[%d]' % t['album_id'])
        display_title = t['title'] or t['filename']
        path  = t['file_path'] or '?'
        anlz  = t['analyze_path'] or '?'

        print('%-4d %-6d %-7.2f %-25s %-35s %-40s' % (
            count, t['track_id'], t['bpm'],
            artist[:24], display_title[:34], path[:39]))

        ok += 1

    print('\nTotal: %d tracks (%d artists, %d albums)' % (count, len(artists), len(albums)))
    return count


if __name__ == '__main__':
    main()
