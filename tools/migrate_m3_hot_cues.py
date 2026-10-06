#!/usr/bin/env python3
"""Read-only catalog capture and offline, loss-checked legacy M3 NVS merge.

No device writes are performed here. Applying merged.bin requires the wired
migration tool with a fresh backup/hash guard. Never infer identities from
Windows stat() or assign a bare Rekordbox track_id across exports.
"""
import argparse
import base64
import collections
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import urllib.request
import zlib

ROOT = Path(__file__).resolve().parents[1]
NVS_SIZE = 0x6000


def sha(data):
    return hashlib.sha256(data).hexdigest()


def idf_tools(idf):
    idf = Path(idf)
    version = (idf / 'tools/cmake/version.cmake').read_text()
    for name, value in [('MAJOR', 6), ('MINOR', 0), ('PATCH', 2)]:
        if not re.search(rf'set\(IDF_VERSION_{name}\s+{value}\)', version):
            raise ValueError('ESP-IDF 6.0.2 is required for NVS migration')
    parser = idf / 'components/nvs_flash/nvs_partition_tool/nvs_parser.py'
    spec = importlib.util.spec_from_file_location('cue_nvs_parser', parser)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    generator = idf / 'components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py'
    return module, generator


def read_nvs(raw, parser):
    """Reject damaged, ambiguous or encrypted partitions; preserve logical data."""
    if len(raw) != NVS_SIZE:
        raise ValueError('M3 NVS backup must be exactly 0x6000 bytes')
    partition = parser.NVS_Partition('backup', bytearray(raw))
    entries = []
    for page in partition.pages:
        if page.is_empty:
            if raw[page.start_address:page.start_address + 4096] != b'\xff' * 4096:
                raise ValueError('Data in an uninitialized NVS page')
            continue
        if page.header['status'] not in ('Active', 'Full'):
            raise ValueError('NVS page needs recovery before migration')
        crc = page.header['crc']
        if crc['original'] != crc['computed'] or page.header['version'] not in (1, 2):
            raise ValueError('Unsupported/corrupt NVS page header')
        for entry in page.entries:
            if entry.state == 'Invalid':
                raise ValueError('Invalid NVS entry state')
            if entry.state != 'Written':
                continue
            crc = entry.metadata['crc']
            if crc['original'] != crc['computed']:
                raise ValueError('NVS entry CRC mismatch')
            if not entry.key or not entry.key.isascii() or len(entry.key) > 15:
                raise ValueError('Unsupported NVS key encoding')
            kind = entry.metadata['type']
            if kind in ('string', 'blob', 'blob_data'):
                data = b''.join(bytes(c.raw) for c in entry.children)[:entry.data['size']]
                if (len(data) != entry.data['size'] or
                        any(c.state != 'Written' for c in entry.children) or
                        zlib.crc32(data, 0xffffffff) != entry.data['crc']):
                    raise ValueError('NVS payload CRC/state/length mismatch')
            entries.append(entry)
    namespaces = {}
    for e in entries:
        if e.metadata['namespace'] == 0:
            if e.metadata['type'] != 'uint8_t' or not 1 <= e.data['value'] < 255:
                raise ValueError('Invalid NVS namespace definition')
            ns = e.data['value']
            if ns in namespaces and namespaces[ns] != e.key:
                raise ValueError('Ambiguous NVS namespace')
            namespaces[ns] = e.key
    if len(set(namespaces.values())) != len(namespaces):
        raise ValueError('Duplicate NVS namespace name')
    groups = collections.defaultdict(list)
    for e in entries:
        ns = e.metadata['namespace']
        if ns:
            if ns not in namespaces:
                raise ValueError('Undefined NVS namespace')
            groups[(namespaces[ns], e.key)].append(e)
    records = {}
    for key, group in groups.items():
        chunks = [e for e in group if e.metadata['type'] == 'blob_data']
        heads = [e for e in group if e.metadata['type'] != 'blob_data']
        if not heads:
            # Power loss may leave uncommitted chunks. They are not logical keys.
            continue
        values = []
        for e in heads:
            kind = e.metadata['type']
            if kind == 'blob_index':
                count, start = e.data['chunk_count'], e.data['chunk_start']
                if start not in (0, 128) or count > 128:
                    raise ValueError('Invalid NVS blob index')
                selected = []
                for index in range(start, start + count):
                    candidates = [c for c in chunks if c.metadata['chunk_index'] == index]
                    if len(candidates) != 1:
                        raise ValueError('Missing/ambiguous NVS blob chunk')
                    c = candidates[0]
                    selected.append(b''.join(bytes(x.raw) for x in c.children)[:c.data['size']])
                value = b''.join(selected)
                if len(value) != e.data['size']:
                    raise ValueError('NVS blob length mismatch')
                kind = 'blob'
            elif kind in ('string', 'blob'):
                value = b''.join(bytes(c.raw) for c in e.children)[:e.data['size']]
                if kind == 'string':
                    if not value.endswith(b'\x00') or b'\x00' in value[:-1]:
                        raise ValueError('Invalid NVS string')
                    value = value[:-1].decode('utf-8')
            elif kind in ('uint8_t', 'int8_t', 'uint16_t', 'int16_t',
                          'uint32_t', 'int32_t', 'uint64_t', 'int64_t'):
                value = e.data['value']
            else:
                raise ValueError('Unsupported NVS value type')
            values.append((kind, value))
        if any(v != values[0] for v in values[1:]):
            raise ValueError('Ambiguous live NVS key; recover before migration')
        records[key] = values[0]
    return sorted(namespaces.values()), records


def write_nvs(namespaces, records, generator, workspace):
    csv_path = workspace / 'merge.csv'
    primitive = {f'{prefix}int{bits}_t': f'{"i" if prefix == "" else "u"}{bits}'
                 for prefix in ('', 'u') for bits in (8, 16, 32, 64)}
    with csv_path.open('w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['key', 'type', 'encoding', 'value'])
        for ns in sorted(set(namespaces) | {key[0] for key in records}):
            writer.writerow([ns, 'namespace', '', ''])
            for (namespace, key), (kind, value) in sorted(records.items()):
                if namespace != ns:
                    continue
                if kind == 'blob':
                    blob_path = workspace / f'blob-{len(list(workspace.glob("blob-*")))}.bin'
                    blob_path.write_bytes(value)
                    writer.writerow([key, 'file', 'binary', str(blob_path)])
                else:
                    writer.writerow([key, 'data', 'string' if kind == 'string' else primitive[kind], value])
    output = workspace / 'merged.bin'
    result = subprocess.run([sys.executable, str(generator), 'generate', str(csv_path),
                             str(output), hex(NVS_SIZE), '--version', '2'],
                            capture_output=True, text=True)
    if result.returncode:
        # CSV contains credentials: never expose generator diagnostics verbatim.
        raise ValueError('NVS generation failed; output withheld')
    return output.read_bytes()


def identity(row, digest):
    path = row['path'].encode('utf-8')
    if not path.startswith(b'/') or len(path) >= 256:
        raise ValueError('Invalid catalog path')
    value = (b'pajoniiir.hotcue.v2' + bytes.fromhex(digest) + struct.pack('<H', len(path)) +
             path + struct.pack('<Qq', row['file_size'], row['mtime']))
    return sha(value)


def legacy_record(raw, persistent_id):
    if len(raw) != 104:
        raise ValueError('legacy blob size')
    version, valid = struct.unpack_from('<II', raw)
    if version != 1 or valid & ~255:
        raise ValueError('legacy version/mask')
    slots = bytearray(96)
    for slot in range(8):
        if not valid & (1 << slot):
            continue
        pos, end, kind = struct.unpack_from('<IIB', raw, 8 + slot * 12)
        if kind not in (1, 2) or (kind == 2 and end <= pos):
            raise ValueError('invalid legacy cue/loop')
        struct.pack_into('<IIB', slots, slot * 12, pos, end if kind == 2 else 0, kind)
    # Absent old slots are not proof of deletion: override only present slots.
    record = struct.pack('<IHH', 0x33435648, 3, 104) + bytes.fromhex(persistent_id)
    record += struct.pack('<II', valid, valid) + slots
    return record + struct.pack('<I', zlib.crc32(record))


def map_cues(records, old_rows, catalog, digest):
    if catalog.get('schema') != 1 or catalog.get('board_id') != 'm3' or catalog.get('project') != 'main-deck-m3':
        raise ValueError('Catalog is not from the new M3 identity runtime')
    rows = catalog['tracks']
    if len(rows) != catalog['count'] or sorted(r['index'] for r in rows) != list(range(len(rows))):
        raise ValueError('Incomplete/duplicate catalog pages')
    by_old, by_path = collections.defaultdict(list), collections.defaultdict(list)
    for r in old_rows:
        by_old[r['legacy_key']].append(r)
    for r in rows:
        if r['export_digest'] != digest:
            raise ValueError('Chosen old export does not match mounted medium')
        if r['status'] == 'ok' and identity(r, digest) != r['persistent_id']:
            raise ValueError('Catalog persistent identity mismatch')
        by_path[r['path']].append(r)
    mapped, archived, proposals = [], [], collections.defaultdict(list)
    for (ns, key), (kind, value) in sorted(records.items()):
        if ns != 'hotcue':
            continue
        info = {'legacy_key': key, 'legacy_blob_base64': base64.b64encode(value).decode() if kind == 'blob' else None}
        reason = None
        if kind != 'blob' or not re.fullmatch(r'hc[0-9a-fA-F]{8}', key):
            reason = 'unsupported legacy record'
        else:
            old = by_old[int(key[2:], 16)]
            if len(old) != 1:
                reason = 'absent or ambiguous old export key'
            else:
                matches = by_path[old[0]['path']]
                if len(matches) != 1 or matches[0]['status'] != 'ok':
                    reason = 'absent, missing or ambiguous mounted audio'
                else:
                    row = matches[0]
                    if row['legacy_key'] != old[0]['legacy_key'] or row['track_id'] != old[0]['track_id']:
                        reason = 'catalog row differs from selected export'
                    else:
                        pid = row['persistent_id']
                        try:
                            record = legacy_record(value, pid)
                            new_key = 'h' + pid[:14]
                            info.update(persistent_id=pid, new_key=new_key, path=row['path'])
                            proposals[new_key].append((info, record))
                        except ValueError as exc:
                            reason = str(exc)
        if reason:
            info['reason'] = reason
            archived.append(info)
    for key, candidates in proposals.items():
        for info, record in candidates:
            existing = records.get(('hotcue_v3', key))
            if len(candidates) != 1:
                info['reason'] = 'ambiguous target namespace key'
            elif existing == ('blob', record):
                info['result'] = 'already migrated'
                mapped.append(info)
                continue
            elif existing or ('hotcue_v2', key) in records:
                info['reason'] = 'existing persistent cue data preserved'
            else:
                records[('hotcue_v3', key)] = ('blob', record)
                info['result'] = 'mapped'
                mapped.append(info)
                continue
            archived.append(info)
    return mapped, archived


def parse_old_export(export, workspace, gcc):
    exe = workspace / ('catalog.exe' if os.name == 'nt' else 'catalog')
    result = subprocess.run([gcc, '-std=c11', '-O2', '-Wall', '-Wextra',
        '-DREKORDBOX_PDB_STANDALONE_TEST', '-I' + str(ROOT / 'firmware/p4-core/components/library/include'),
        str(ROOT / 'tools/cue_migration_catalog.c'),
        str(ROOT / 'firmware/p4-core/components/library/rekordbox_pdb.c'), '-o', str(exe)], capture_output=True)
    if result.returncode:
        raise ValueError('Production PDB migration helper build failed')
    output = workspace / 'old-catalog.json'
    result = subprocess.run([str(exe), str(export), str(output)], capture_output=True)
    if result.returncode:
        raise ValueError('Old export is unreadable, empty or truncated')
    return json.loads(output.read_text(encoding='utf-8'))


def capture(base_url):
    tracks, first, offset = [], None, 0
    while True:
        url = base_url.rstrip('/') + f'/api/cue-migration/catalog?offset={offset}'
        if first:
            url += '&generation=' + str(first['generation'])
        with urllib.request.urlopen(url, timeout=30) as response:
            page = json.load(response)
        if first is None:
            first = page
        fields = ('schema', 'board_id', 'project', 'source_sha', 'generation', 'count')
        if any(page[k] != first[k] for k in fields) or page['offset'] != offset:
            raise ValueError('Catalog changed during capture')
        tracks.extend(page['tracks'])
        next_offset = page['next_offset']
        if next_offset == page['count']:
            break
        if not offset < next_offset <= page['count']:
            raise ValueError('Invalid catalog pagination')
        offset = next_offset
    result = {k: first[k] for k in fields}
    result['tracks'] = tracks
    if len(tracks) != result['count']:
        raise ValueError('Incomplete catalog capture')
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='command', required=True)
    cap = sub.add_parser('capture')
    cap.add_argument('--device-url', required=True)
    cap.add_argument('--output', type=Path, required=True)
    prep = sub.add_parser('prepare')
    for name in ('backup', 'old-export', 'catalog', 'output-dir'):
        prep.add_argument('--' + name, type=Path, required=True)
    prep.add_argument('--backup-sha256', required=True)
    prep.add_argument('--idf-path', default=os.environ.get('IDF_PATH', r'C:\Espressif\v6.0.2\esp-idf'))
    prep.add_argument('--gcc', default='gcc')
    args = ap.parse_args()
    try:
        if args.command == 'capture':
            # Exclusive creation never overwrites an earlier capture.
            data = capture(args.device_url)
            with args.output.open('x', encoding='utf-8') as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
            return 0
        if args.output_dir.exists():
            raise ValueError('Output directory already exists')
        raw = args.backup.read_bytes()
        if sha(raw) != args.backup_sha256.lower():
            raise ValueError('Selected backup SHA-256 mismatch')
        parser, generator = idf_tools(args.idf_path)
        namespaces, original = read_nvs(raw, parser)
        records = dict(original)
        export_raw = args.old_export.read_bytes()
        digest = sha(export_raw)
        catalog_raw = args.catalog.read_bytes()
        catalog = json.loads(catalog_raw)
        args.output_dir.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.cue-migration-', dir=args.output_dir.parent) as temp:
            workspace = Path(temp)
            frozen_export = workspace / 'selected-export.pdb'
            frozen_export.write_bytes(export_raw)
            old_rows = parse_old_export(frozen_export, workspace, args.gcc)
            mapped, archived = map_cues(records, old_rows, catalog, digest)
            merged = write_nvs(namespaces, records, generator, workspace)
            merged_ns, roundtrip = read_nvs(merged, parser)
            if roundtrip != records or not set(namespaces).issubset(merged_ns):
                raise ValueError('Generated NVS failed complete preservation check')
            deliver = workspace / 'deliver'
            deliver.mkdir()
            (deliver / 'original-nvs.bin').write_bytes(raw)
            (deliver / 'merged-nvs.bin').write_bytes(merged)
            report = {'schema': 1, 'board_id': 'm3', 'source_sha': catalog['source_sha'],
                      'backup_sha256': sha(raw), 'merged_sha256': sha(merged),
                      'old_export_sha256': digest, 'catalog_sha256': sha(catalog_raw),
                      'mapped': mapped, 'archived': archived, 'device_written': False}
            (deliver / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
            # Same filesystem rename publishes only fully validated output.
            deliver.rename(args.output_dir)
        print(f'Prepared {len(mapped)} mapped cues; {len(archived)} archived. Device unchanged.')
        return 0
    except (ValueError, OSError, KeyError, TypeError, struct.error) as exc:
        print(f'Cue migration refused: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
