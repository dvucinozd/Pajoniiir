#!/usr/bin/env python3
"""
parse_anlz.py — Python ANLZ parser that mirrors the C implementation.
Tests real Rekordbox ANLZ files and validates our C parser logic.

Rekordbox USBANLZ structure (IMPORTANT — differs from early assumption):
  PIONEER/USBANLZ/<hash1>/<hash2>/ANLZ0000.DAT
  PIONEER/USBANLZ/<hash1>/<hash2>/ANLZ0000.EXT
  PIONEER/USBANLZ/<hash1>/<hash2>/ANLZ0000.2EX  (newer format, color waveforms)

  The hash folders (e.g. P000/00000832) are NOT derived from the audio path.
  They are internal Rekordbox IDs.  To map audio file → ANLZ folder, the
  library component must walk all ANLZ files and index them by PPTH path.

Usage:
    python parse_anlz.py <ANLZ0000.DAT> [ANLZ0000.EXT]
    python parse_anlz.py --scan <PIONEER/USBANLZ root> [--limit N]
"""

import sys
import os
import struct
import argparse

def be16(d,o): return struct.unpack_from('>H',d,o)[0]
def be32(d,o): return struct.unpack_from('>I',d,o)[0]

# ── Sequential tag scanner ───────────────────────────────────────────────────
# Tags are laid out sequentially in the file.  PMAI is the file header whose
# segment_size = total file size; all other tags are contiguous after it.

KNOWN_TAGS = {
    'PMAI','PPTH','PVBR','PQTZ','PWAV','PWV2','PWV3','PWV4','PWV5','PWV6','PWV7',
    'PCOB','PCO2','PQT2','PSSI','PPHR','PWVC',
}

def scan_tags(data):
    """Return list of (offset, tag_str, hdr_size, seg_size)."""
    results = []
    offset = 0
    while offset + 12 <= len(data):
        tag     = data[offset:offset+4].decode('ascii', errors='replace')
        hdr     = be32(data, offset + 4)
        seg     = be32(data, offset + 8)
        results.append((offset, tag, hdr, seg))
        if tag == 'PMAI':
            # PMAI seg_size = entire file; only advance by header
            offset += hdr
        else:
            if seg < 12 or offset + seg > len(data):
                break
            offset += seg
    return results

# ── Tag parsers ──────────────────────────────────────────────────────────────

def parse_ppth(data, off, hdr, seg):
    path_bytes = data[off+hdr : off+seg]
    try:
        path = path_bytes.decode('utf-16-be').rstrip('\x00').replace('\\', '/')
    except Exception:
        path = repr(path_bytes[:40])
    return {'path': path}

def parse_pvbr(data, off, hdr, seg):
    count   = (seg - hdr) // 4
    offsets = [be32(data, off+hdr + i*4) for i in range(min(count, 400))]
    return {'count': count, 'first4': offsets[:4], 'last4': offsets[-4:] if len(offsets)>=4 else offsets}

def parse_pqtz(data, off, hdr, seg):
    count  = (seg - hdr) // 8
    beats  = []
    for i in range(count):
        o = off+hdr + i*8
        beats.append({'phase': be16(data,o), 'bpm_x100': be16(data,o+2), 'time_ms': be32(data,o+4)})
    bpm      = round(beats[0]['bpm_x100'] / 100, 2) if beats else 0
    duration = beats[-1]['time_ms'] if beats else 0
    return {'bpm': bpm, 'beats': count, 'duration_ms': duration,
            'first': beats[0] if beats else None, 'last': beats[-1] if beats else None}

def parse_pwav(data, off, hdr, seg):
    wav     = data[off+hdr : off+seg]
    heights = [b & 0x1F for b in wav]
    return {'bytes': len(wav), 'max_h': max(heights) if heights else 0,
            'sample': heights[:8]}

def parse_pcob(data, off, hdr, seg):
    data_len = seg - hdr
    count    = data_len // 56
    cues     = []
    for i in range(count):
        o     = off+hdr + i*56
        if o + 56 > len(data): break
        etype = data[o]; idx = data[o+1]
        start = be32(data, o+4); end = be32(data, o+8)
        cues.append({'type': 'loop' if etype==2 else 'single',
                     'idx': idx, 'start_ms': start,
                     'end_ms': end if etype==2 else None})
    return {'count': len(cues), 'cues': cues}

def parse_pwv3(data, off, hdr, seg):
    return {'bytes': seg - hdr}

PARSERS = {
    'PPTH': parse_ppth, 'PVBR': parse_pvbr, 'PQTZ': parse_pqtz,
    'PWAV': parse_pwav, 'PCOB': parse_pcob, 'PWV3': parse_pwv3,
}

# ── Full file parse ───────────────────────────────────────────────────────────

def parse_file(path, verbose=True):
    data   = open(path, 'rb').read()
    tags   = scan_tags(data)
    result = {'path': path, 'size': len(data), 'tags': [], 'data': {}, 'errors': []}

    for (off, tag, hdr, seg) in tags:
        result['tags'].append(tag)
        if tag in PARSERS:
            try:
                result['data'][tag] = PARSERS[tag](data, off, hdr, seg)
            except Exception as e:
                result['errors'].append(f'{tag}: {e}')

    if verbose:
        _print(result, tags)
    return result

def _print(r, raw_tags):
    fname = os.path.basename(r['path'])
    print(f"\n{'─'*64}")
    print(f"  {fname}  ({r['size']:,} bytes)")
    print(f"  Tags: {' '.join(r['tags'])}")
    if r['errors']:
        print(f"  ERRORS: {r['errors']}")

    d = r['data']

    if 'PPTH' in d:
        print(f"\n  PPTH  path     : {d['PPTH']['path']}")

    if 'PQTZ' in d:
        q = d['PQTZ']
        m,s = divmod(q['duration_ms']//1000, 60)
        print(f"  PQTZ  BPM       : {q['bpm']}")
        print(f"        beats     : {q['beats']}")
        print(f"        duration  : {m}:{s:02d}  ({q['duration_ms']} ms)")

    if 'PVBR' in d:
        v = d['PVBR']
        nz = sum(1 for x in v['first4'] if x != 0)
        print(f"  PVBR  entries   : {v['count']}  (nonzero in first4: {nz})")

    if 'PWAV' in d:
        w = d['PWAV']
        print(f"  PWAV  bytes     : {w['bytes']}  max_height={w['max_h']}  sample={w['sample']}")

    if 'PCOB' in d:
        c = d['PCOB']
        if c['count'] == 0:
            print(f"  PCOB  cues      : 0  (no hot cues set)")
        else:
            for cue in c['cues']:
                if cue['type'] == 'loop':
                    print(f"  PCOB  LOOP[{cue['idx']}] : {cue['start_ms']}ms → {cue['end_ms']}ms")
                else:
                    print(f"  PCOB  CUE[{cue['idx']}]  : {cue['start_ms']}ms")

    if 'PWV3' in d:
        print(f"  PWV3  bytes     : {d['PWV3']['bytes']:,}")

    # Note extra tags not parsed by C implementation
    extra = [t for t in r['tags'] if t not in ('PMAI','PPTH','PVBR','PQTZ','PWAV','PWV2','PCOB','PWV3') and t in KNOWN_TAGS]
    if extra:
        print(f"  Extra tags (future): {extra}")

# ── Validation ─────────────────────────────────────────────────────────────

def validate(r, fname):
    d  = r['data']
    ok = True
    issues = []

    if 'PPTH' not in d or not d['PPTH']['path']:
        issues.append("PPTH missing"); ok = False
    if 'PQTZ' not in d:
        issues.append("PQTZ missing"); ok = False
    else:
        if d['PQTZ']['bpm'] < 60 or d['PQTZ']['bpm'] > 200:
            issues.append(f"BPM out of range: {d['PQTZ']['bpm']}")
        if d['PQTZ']['beats'] == 0:
            issues.append("zero beat entries")
    if 'PVBR' not in d:
        issues.append("PVBR missing (no VBR seek)")
    if 'PWAV' not in d:
        issues.append("PWAV missing (no waveform)")
    elif d['PWAV']['bytes'] != 400:
        issues.append(f"PWAV wrong size: {d['PWAV']['bytes']}")
    if r['errors']:
        issues.extend(r['errors']); ok = False

    return ok, issues

# ── Scan mode ─────────────────────────────────────────────────────────────────

def scan_usbanlz(root, limit=5):
    total = ok = 0
    all_issues = []

    for dirpath, dirs, files in os.walk(root):
        dirs.sort()
        if 'ANLZ0000.DAT' not in files:
            continue

        total += 1
        dat = os.path.join(dirpath, 'ANLZ0000.DAT')
        ext = os.path.join(dirpath, 'ANLZ0000.EXT')

        verbose = (total <= limit)
        r = parse_file(dat, verbose=verbose)

        # EXT
        if os.path.exists(ext) and verbose:
            parse_file(ext, verbose=True)

        passed, issues = validate(r, dat)
        if passed:
            ok += 1
        else:
            all_issues.append((dat, issues))

    print(f"\n{'═'*64}")
    print(f"  Total tracks : {total}")
    print(f"  Validated OK : {ok}")
    print(f"  Issues       : {len(all_issues)}")
    for (path, issues) in all_issues[:10]:
        print(f"  ⚠  {os.path.basename(os.path.dirname(path))}/{os.path.basename(path)}")
        for iss in issues:
            print(f"      - {iss}")

    return total, ok

# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--scan', metavar='ROOT', help='Scan USBANLZ root')
    ap.add_argument('--limit', type=int, default=5, help='Tracks to print in scan (default 5)')
    ap.add_argument('dat', nargs='?')
    ap.add_argument('ext', nargs='?')
    args = ap.parse_args()

    if args.scan:
        total, ok = scan_usbanlz(args.scan, args.limit)
        sys.exit(0 if ok == total else 1)
    elif args.dat:
        parse_file(args.dat)
        if args.ext:
            parse_file(args.ext)
    else:
        ap.print_help()

if __name__ == '__main__':
    main()
