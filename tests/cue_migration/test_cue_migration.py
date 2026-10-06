import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('migration', ROOT / 'tools/migrate_m3_hot_cues.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def legacy():
    data = bytearray(104)
    struct.pack_into('<II', data, 0, 1, 5)
    struct.pack_into('<IIB', data, 8, 1100, 0, 1)
    struct.pack_into('<IIB', data, 32, 2000, 4000, 2)
    return bytes(data)


def fixture_pdb(path):
    # Independent DeviceSQL track row, title at index 17 and audio at index 20.
    data = bytearray(1024)
    struct.pack_into('<II', data, 4, 512, 1)
    struct.pack_into('<I', data, 36, 1)
    struct.pack_into('<I', data, 512 + 12, 0xffffffff)
    struct.pack_into('<I', data, 512 + 24, 1)
    row = 552
    struct.pack_into('<H', data, row, 0x24)
    struct.pack_into('<I', data, row + 0x48, 123)
    data[1020] = 1
    for index, offset, value in [(17, 160, b'Migration title'), (20, 210, b'/Contents/audio.wav')]:
        struct.pack_into('<H', data, row + 0x5e + index * 2, offset)
        data[row + offset] = ((len(value) + 1) << 1) | 1
        data[row + offset + 1:row + offset + 1 + len(value)] = value
    path.write_bytes(data)


class MigrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        idf = os.environ.get('IDF_PATH', r'C:\Espressif\v6.0.2\esp-idf')
        cls.parser, cls.generator = m.idf_tools(idf)
        cls.gcc = shutil.which('gcc') or r'C:\msys64\ucrt64\bin\gcc.exe'

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.workspace = Path(self.temp.name)
        self.digest = '12' * 32
        row = dict(index=0, legacy_key=123, track_id=123, path='/Contents/audio.wav',
                   title='Title', status='ok', export_digest=self.digest,
                   file_size=44100, mtime=1700000000)
        row['persistent_id'] = m.identity(row, self.digest)
        self.catalog = dict(schema=1, board_id='m3', project='main-deck-m3',
                            source_sha='34' * 20, count=1, tracks=[row])
        self.old = [dict(legacy_key=123, track_id=123, path=row['path'])]
        self.records = {('settings', 'wifi'): ('string', 'preserve-secret'),
                        ('settings', 'gain'): ('int32_t', -17),
                        ('hotcue', 'hc0000007b'): ('blob', legacy())}

    def tearDown(self):
        self.temp.cleanup()

    def test_roundtrip_preserves_all_settings_namespaces_and_large_blobs(self):
        self.records[('other', 'large')] = ('blob', bytes(range(256)) * 31)
        self.records[('other', 'empty')] = ('blob', b'')
        raw = m.write_nvs(['settings', 'hotcue', 'other', 'empty_ns'], self.records,
                          self.generator, self.workspace)
        namespaces, decoded = m.read_nvs(raw, self.parser)
        self.assertEqual(decoded, self.records)
        self.assertIn('empty_ns', namespaces)

    def test_valid_merge_crc_override_and_idempotence(self):
        original = dict(self.records)
        mapped, archived = m.map_cues(self.records, self.old, self.catalog, self.digest)
        self.assertEqual((len(mapped), len(archived)), (1, 0))
        key = ('hotcue_v3', mapped[0]['new_key'])
        raw = self.records[key][1]
        self.assertEqual(len(raw), 148)
        self.assertEqual(struct.unpack_from('<II', raw, 40), (5, 5))
        self.assertEqual(struct.unpack_from('<I', raw, 144)[0], zlib.crc32(raw[:144]))
        for k, value in original.items():
            self.assertEqual(self.records[k], value)
        mapped, archived = m.map_cues(self.records, self.old, self.catalog, self.digest)
        self.assertEqual(mapped[0]['result'], 'already migrated')
        self.assertFalse(archived)

    def test_same_track_id_different_export_refused(self):
        with self.assertRaisesRegex(ValueError, 'does not match'):
            m.map_cues(self.records, self.old, self.catalog, '99' * 32)

    def test_ambiguous_old_key_archived(self):
        original = dict(self.records)
        mapped, archived = m.map_cues(self.records, self.old * 2, self.catalog, self.digest)
        self.assertFalse(mapped)
        self.assertIn('ambiguous', archived[0]['reason'])
        self.assertEqual(original, self.records)

    def test_existing_local_data_preserved(self):
        key = 'h' + self.catalog['tracks'][0]['persistent_id'][:14]
        for ns in ('hotcue_v2', 'hotcue_v3'):
            records = dict(self.records)
            records[(ns, key)] = ('blob', b'keep existing')
            before = dict(records)
            mapped, archived = m.map_cues(records, self.old, self.catalog, self.digest)
            self.assertFalse(mapped)
            self.assertIn('preserved', archived[0]['reason'])
            self.assertEqual(records, before)

    def test_missing_and_corrupt_legacy_archived(self):
        self.catalog['tracks'][0]['status'] = 'missing'
        mapped, archived = m.map_cues(self.records, self.old, self.catalog, self.digest)
        self.assertFalse(mapped)
        self.assertIn('missing', archived[0]['reason'])
        self.catalog['tracks'][0]['status'] = 'ok'
        self.records[('hotcue', 'hc0000007b')] = ('blob', b'bad')
        mapped, archived = m.map_cues(self.records, self.old, self.catalog, self.digest)
        self.assertEqual(archived[0]['reason'], 'legacy blob size')

    def test_forged_device_identity_and_duplicate_pages_refused(self):
        self.catalog['tracks'][0]['mtime'] += 2
        with self.assertRaisesRegex(ValueError, 'identity mismatch'):
            m.map_cues(self.records, self.old, self.catalog, self.digest)
        self.catalog['tracks'] *= 2
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            m.map_cues(self.records, self.old, self.catalog, self.digest)

    def test_corrupt_backup_is_refused(self):
        raw = bytearray(m.write_nvs(['settings', 'hotcue'], self.records, self.generator, self.workspace))
        for offset in (4, 68, 140):
            damaged = bytearray(raw)
            damaged[offset] ^= 1
            with self.assertRaises(ValueError):
                m.read_nvs(damaged, self.parser)
        with self.assertRaises(ValueError):
            m.read_nvs(raw[:-1], self.parser)

    def test_production_pdb_parser_and_complete_cli(self):
        export = self.workspace / 'export.pdb'
        fixture_pdb(export)
        rows = m.parse_old_export(export, self.workspace, self.gcc)
        self.assertEqual(rows, [dict(legacy_key=123, track_id=123,
            path='/Contents/audio.wav', title='Migration title')])
        self.digest = m.sha(export.read_bytes())
        row = self.catalog['tracks'][0]
        row['export_digest'] = self.digest
        row['persistent_id'] = m.identity(row, self.digest)
        catalog = self.workspace / 'catalog.json'
        catalog.write_text(json.dumps(self.catalog))
        backup = self.workspace / 'backup.bin'
        raw = m.write_nvs(['settings', 'hotcue'], self.records, self.generator, self.workspace)
        backup.write_bytes(raw)
        output = self.workspace / 'output'
        command = [sys.executable, str(ROOT / 'tools/migrate_m3_hot_cues.py'), 'prepare',
            '--backup', str(backup), '--backup-sha256', m.sha(raw), '--old-export', str(export),
            '--catalog', str(catalog), '--output-dir', str(output), '--gcc', self.gcc]
        run = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads((output / 'report.json').read_text())
        self.assertEqual(len(report['mapped']), 1)
        self.assertFalse(report['device_written'])
        self.assertNotIn('preserve-secret', (output / 'report.json').read_text())
        self.assertEqual(backup.read_bytes(), raw)

        _, decoded = m.read_nvs((output / 'merged-nvs.bin').read_bytes(), self.parser)
        for k, value in self.records.items():
            self.assertEqual(decoded[k], value)
        # Refused overwrite leaves complete prior output and immutable input.
        run = subprocess.run(command, capture_output=True)
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(backup.read_bytes(), raw)

    def test_production_catalog_identity_fences(self):
        exe = self.workspace / ('identity.exe' if os.name == 'nt' else 'identity')
        base = ROOT / 'firmware/p4-core/components'
        includes = [ROOT / 'tests/support/stubs'] + [base / name / 'include' for name in
            ('media_catalog', 'library', 'service_log', 'media_identity', 'media_io_gate')]
        command = [self.gcc, '-std=c11', '-Wall', '-Wextra', '-ffunction-sections',
            '-fdata-sections', '-Wl,--gc-sections', '-Dstat=cue_test_stat']
        command += ['-I' + str(p) for p in includes]
        command += [str(ROOT / 'tests/cue_migration/test_catalog_identity.c'),
                    str(base / 'media_catalog/media_catalog.c'),
                    str(base / 'media_identity/media_identity.c'), '-o', str(exe)]
        run = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == '__main__':
    unittest.main()
