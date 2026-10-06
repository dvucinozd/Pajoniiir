import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import migrate_m3_factory as migration
import ota_signing
from create_integration_candidate import record


class EspTransportTests(unittest.TestCase):
    def test_python_adapter_attaches_flash_before_reading(self):
        # Unlike the CLI, Python read_flash assumes SPI flash was attached.
        # The real USB-JTAG bench failed at its first read without this step.
        from types import SimpleNamespace
        calls = []
        class Esp:
            CHIP_NAME = 'ESP32-P4'
            attached = False
            def uses_usb_jtag_serial(self): return False
            def uses_usb_otg(self): return False
            def run_stub(self):
                calls.append('stub')
                return self
            def change_baud(self, baud):
                calls.append(('baud', baud))
        esp = Esp()
        def attach(device):
            self.assertIs(device, esp)
            calls.append('attach')
            device.attached = True
        def read(device, address, size, **kwargs):
            if not device.attached:
                raise OSError('flash is not attached')
            calls.append(('read', address, size))
            return bytes(size)
        tool = SimpleNamespace(__version__='5.3.1', detect_chip=lambda **_: esp,
                               attach_flash=attach, read_flash=read)
        with patch.dict(sys.modules, {'esptool': tool}):
            device = migration.EspDevice('COM20')
            self.assertEqual(device.read(0x8000, 0x1000), bytes(0x1000))
        self.assertEqual(calls, ['stub', ('baud', 460800), 'attach', ('read', 0x8000, 0x1000)])

    def test_native_usb_bounded_reads_and_incomplete_refusal(self):
        from types import SimpleNamespace
        from unittest.mock import Mock
        esp = SimpleNamespace(CHIP_NAME='ESP32-P4', uses_usb_jtag_serial=lambda: True,
                              uses_usb_otg=lambda: False, change_baud=Mock())
        esp.run_stub = lambda: esp
        reader = Mock(side_effect=lambda device, address, count, **kw: bytes(count))
        tool = SimpleNamespace(__version__='5.3.1', detect_chip=lambda **_: esp,
                               attach_flash=Mock(), read_flash=reader)
        with patch.dict(sys.modules, {'esptool': tool}):
            device = migration.EspDevice('COM20')
            self.assertEqual(len(device.read(0x8000, 0x20001)), 0x20001)
        esp.change_baud.assert_not_called()
        self.assertEqual([(a.args[1], a.args[2]) for a in reader.call_args_list],
                         [(0x8000, 0x10000), (0x18000, 0x10000), (0x28000, 1)])
        reader.side_effect = lambda *_args, **_kwargs: b''
        with self.assertRaisesRegex(ValueError, 'incomplete bounded'):
            device.read(0x8000, 0x1000)


def image(project, version):
    data = bytearray(32 + 256)
    data[0:2] = bytes((0xe9, 1))
    struct.pack_into('<H', data, 12, 0x12)
    data[23] = 1
    struct.pack_into('<II', data, 24, 0x40000000, 256)
    struct.pack_into('<I', data, 32, 0xabcd5432)
    for offset, text in ((48, version), (80, project), (144, 'v6.0.2')):
        data[offset:offset + len(text)] = text.encode()
    data[176:208] = bytes(range(32))
    checksum = 0xef
    for byte in data[32:]:
        checksum ^= byte
    data += bytes(((len(data) + 16) & ~15) - len(data) - 1) + bytes((checksum,))
    data += hashlib.sha256(data).digest()
    return bytes(data)


def table():
    data = bytearray()
    for name, (kind, subtype, offset, size) in migration.PARTITIONS.items():
        data += struct.pack('<HBBII16sI', 0x50aa, kind, subtype, offset, size, name.encode(), 0)
    data += b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(data).digest()
    return bytes(data) + b'\xff' * (0x1000 - len(data))


class Device:
    def __init__(self, raw):
        self.raw = bytearray(raw)
        self.actions = []
        self.corrupt_factory = False
        self.fail_at = None
        self.info = {'chip': 'ESP32-P4', 'revision': 100, 'mac': 'aa:bb:cc:dd:ee:ff',
                     'flash_size': migration.FLASH_SIZE, 'secure_boot': False, 'flash_encrypted': False}

    def identity(self):
        return self.info

    def read(self, offset, size):
        self.actions.append(('read', offset, size))
        return bytes(self.raw[offset:offset + size])

    def digest(self, offset, size):
        return hashlib.md5(self.raw[offset:offset + size]).hexdigest()

    def verify(self, offset, image):
        return self.read(offset, len(image)) == image

    def write(self, offset, data):
        self.actions.append(('write', offset, len(data)))
        if offset == self.fail_at:
            raise OSError('simulated interrupted write')
        self.raw[offset:offset + len(data)] = data
        if offset == 0x20000 and self.corrupt_factory:
            self.raw[offset] ^= 1

    def reset(self):
        self.actions.append(('reset',))


class MigrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        cls.old = image('main-deck-p4', 'M3-51-gafb2099')
        raw = bytearray(b'\xff' * migration.FLASH_SIZE)
        raw[0x8000:0x9000] = table()
        raw[0x420000:0x420000 + len(cls.old)] = cls.old
        raw[0x9000:0xf000] = (b'legacy cues and settings\0' * 1024)[:0x6000].ljust(0x6000, b'\xff')
        cls.raw = bytes(raw)
        cls.backup = cls.root / 'backup'
        migration.backup(Device(raw), cls.backup)
        cls.release = cls.root / 'release'
        cls.release.mkdir()
        (cls.release / 'wired').mkdir()
        (cls.release / 'wired/partition-table.bin').write_bytes(table()[:0xc00])
        cls.private = cls.root / 'private.pem'
        cls.public = cls.root / 'public.der'
        ota_signing.generate_key(cls.private, cls.public)
        key = ota_signing._load_private(cls.private)
        cls.version = 'M3-dev-g0123456789ab'
        cls.new = image('main-deck-m3', cls.version)
        (cls.release / 'main-deck-m3.bin').write_bytes(cls.new)
        (cls.release / 'main-deck-m3.ddjota').write_bytes(ota_signing.create_bundle(
            cls.new, key, 'p4', 0x12, 'main-deck-m3', cls.version, 'rel-001'))
        app = record(cls.release / 'main-deck-m3.bin')
        bundle = record(cls.release / 'main-deck-m3.ddjota')
        cls.candidate = {'project': 'main-deck-m3', 'board': 'M3', 'software_verified': True,
            'source_sha': '0123456789ab' + '0' * 28, 'version': cls.version, 'image': app, 'bundle': bundle,
            'image_elf_sha256': cls.new[176:208].hex()}
        migration.save_json(cls.release / 'candidate-evidence.json', cls.candidate)
        target = {'target': 'p4', 'project': 'main-deck-m3', 'file': app['file'],
            'ota_bundle': bundle['file'], 'size': app['size'], 'sha256': app['sha256'],
            'bundle_size': bundle['size'], 'bundle_sha256': bundle['sha256'],
            'image_elf_sha256': cls.candidate['image_elf_sha256']}
        payload = json.dumps({'schema_version': 2, 'release_version': cls.version,
            'source_sha': cls.candidate['source_sha'], 'targets': [target]}).encode()
        (cls.release / 'manifest.json').write_bytes(payload)
        (cls.release / 'manifest.sig').write_bytes(ota_signing._raw_sign(key, payload))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def plan(self, **kwargs):
        options = dict(directory=self.backup, release=self.release, public_key=self.public,
                       expected_mac='aa:bb:cc:dd:ee:ff', old_slot='ota_0', old_sha=migration.sha(self.old))
        return migration.plan(**(options | kwargs))

    def test_full_backup_retains_recovery_nvs_ota_and_identity(self):
        info, raw = migration.load_backup(self.backup)
        self.assertEqual(raw, self.raw)
        self.assertEqual(info['apps']['ota_0']['sha256'], migration.sha(self.old))
        self.assertEqual((self.backup / 'nvs.bin').stat().st_size, 0x6000)
        self.assertFalse(info['device_written'])

    def test_success_write_order_preserves_all_other_regions(self):
        planned, raw, new, nvs = self.plan()
        device = Device(raw)
        path = self.root / 'success.json'
        result = migration.apply(device, planned, raw, new, nvs, path)
        writes = [event for event in device.actions if event[0] == 'write']
        self.assertEqual(writes, [('write', 0x20000, len(new)), ('write', 0x10000, 0x2000)])
        first_verify = device.actions.index(('read', 0x20000, len(new)))
        ota_write = device.actions.index(('write', 0x10000, 0x2000))
        self.assertLess(first_verify, ota_write)
        self.assertEqual(device.actions[-1], ('reset',))
        self.assertEqual(result['stage'], 'first_boot_requested')
        self.assertEqual(device.raw[:0x10000], raw[:0x10000])
        self.assertEqual(device.raw[0x12000:0x20000], raw[0x12000:0x20000])
        self.assertEqual(device.raw[0x20000 + len(new):], raw[0x20000 + len(new):])

    def test_factory_readback_failure_never_changes_selection_or_resets(self):
        planned, raw, new, nvs = self.plan()
        device = Device(raw)
        device.corrupt_factory = True
        path = self.root / 'readback-failure.json'
        with self.assertRaisesRegex(ValueError, 'factory readback'):
            migration.apply(device, planned, raw, new, nvs, path)
        self.assertEqual([a[1] for a in device.actions if a[0] == 'write'], [0x20000])
        self.assertNotIn(('reset',), device.actions)
        self.assertTrue(json.loads(path.read_text())['recovery_required'])

    def test_interrupted_selection_write_keeps_bootloader_and_backup(self):
        planned, raw, new, nvs = self.plan()
        device = Device(raw)
        device.fail_at = 0x10000
        with self.assertRaises(OSError):
            migration.apply(device, planned, raw, new, nvs, self.root / 'interrupt.json')
        self.assertNotIn(('reset',), device.actions)
        self.assertEqual(migration.load_backup(self.backup)[1], raw)

    def test_stale_flash_and_wrong_device_refuse_all_writes(self):
        planned, raw, new, nvs = self.plan()
        for wrong_identity in (False, True):
            device = Device(raw)
            if wrong_identity:
                device.info = device.info | {'mac': '01:02:03:04:05:06'}
            else:
                device.raw[0x9000] ^= 1
            with self.assertRaises(ValueError):
                migration.apply(device, planned, raw, new, nvs, self.root / 'stale.json')
            self.assertFalse(any(a[0] in ('write', 'reset') for a in device.actions))

    def recovery_capture(self, name):
        path = self.root / (name + '-old.bin')
        path.write_bytes(self.old)
        directory = self.root / name
        device = Device(self.raw)
        report = migration.backup(device, directory, path)
        return directory, device, report

    def test_existing_recovery_image_mode_preserves_settings_without_full_snapshot(self):
        directory, device, report = self.recovery_capture('recovery-valid')
        self.assertFalse(report['full_flash_captured'])
        self.assertFalse((directory / 'full-flash.bin').exists())
        self.assertEqual(report['apps']['ota_0']['sha256'], migration.sha(self.old))
        planned, raw, new, nvs = self.plan(directory=directory)
        self.assertEqual(len(raw), 0x12000)
        self.assertEqual(planned['backup_kind'], 'protected-regions-and-recovery-image')
        device.actions.clear()
        migration.apply(device, planned, raw, new, nvs, self.root / 'recovery-result.json')
        self.assertEqual(device.actions[0], ('read', 0, 0x12000))
        self.assertEqual([a[1] for a in device.actions if a[0] == 'write'], [0x20000, 0x10000])
        self.assertEqual(device.raw[0x9000:0xf000], self.raw[0x9000:0xf000])
        self.assertEqual(device.raw[0x420000:], self.raw[0x420000:])

    def test_recovery_capture_refuses_unmatched_image_or_wrong_selected_slot(self):
        path = self.root / 'unmatched.bin'
        path.write_bytes(image('main-deck-p4', 'M3-other'))
        device = Device(self.raw)
        with self.assertRaisesRegex(ValueError, 'does not match'):
            migration.backup(device, self.root / 'unmatched', path)
        self.assertFalse(any(a[0] == 'write' for a in device.actions))
        directory, _, _ = self.recovery_capture('recovery-slot')
        with self.assertRaisesRegex(ValueError, 'selected slot'):
            self.plan(directory=directory, old_slot='factory')

    def test_recovery_preflight_rejects_changed_nvs_or_installed_recovery_image(self):
        directory, _, _ = self.recovery_capture('recovery-stale')
        planned, raw, new, nvs = self.plan(directory=directory)
        for offset in (0x9000, 0x420030):
            device = Device(self.raw)
            device.raw[offset] ^= 1
            with self.assertRaisesRegex(ValueError, 'changed since'):
                migration.apply(device, planned, raw, new, nvs, self.root / 'recovery-stale-result.json')
            self.assertFalse(any(a[0] in ('write', 'reset') for a in device.actions))

    def test_recovery_file_tampering_is_rejected(self):
        directory, _, _ = self.recovery_capture('recovery-tamper')
        path = directory / 'recovery-image.bin'
        path.write_bytes(path.read_bytes()[:-1] + b'\0')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            self.plan(directory=directory)

    def test_recovery_missing_or_displaced_protected_region_is_rejected(self):
        directory, _, report = self.recovery_capture('recovery-bounds')
        report['files']['nvs']['offset'] += 0x1000
        migration.save_json(directory / 'backup.json', report)
        with self.assertRaisesRegex(ValueError, 'protected region bounds'):
            self.plan(directory=directory)
        del report['files']['nvs']
        migration.save_json(directory / 'backup.json', report)
        with self.assertRaisesRegex(ValueError, 'incomplete recovery capture'):
            self.plan(directory=directory)

    def test_offline_wrong_mac_and_legacy_sha_are_rejected(self):
        for options in ({'expected_mac': '01:02:03:04:05:06'}, {'old_sha': '0' * 64}, {'old_slot': 'factory'}):
            with self.assertRaises(ValueError):
                self.plan(**options)

    def test_image_checksum_sha_length_chip_and_descriptor(self):
        parsed, info = migration.app_image(self.new + b'\xff' * 100)
        self.assertEqual(parsed, self.new)
        self.assertEqual(info['project'], 'main-deck-m3')
        for offset in (12, 23, 32, len(self.new) - 33, len(self.new) - 1):
            corrupt = bytearray(self.new)
            corrupt[offset] ^= 1
            with self.assertRaises(ValueError):
                migration.app_image(corrupt)
        with self.assertRaises(ValueError):
            migration.app_image(self.new[:-1])

    def test_layout_flags_and_md5_are_rejected(self):
        for offset in (0, 12, 28, 240):
            bad = bytearray(table())
            bad[offset] ^= 1
            with self.assertRaises(ValueError):
                migration.partition_table(bad)

    def test_security_flash_size_and_revision_are_rejected(self):
        for changed in ({'secure_boot': True}, {'flash_encrypted': True}, {'flash_size': 0x800000}, {'revision': 300}):
            device = Device(self.raw)
            device.info |= changed
            with self.assertRaises(ValueError):
                migration.device_identity(device)

    def test_cue_nvs_requires_exact_backup_and_source_then_reads_back(self):
        directory = self.root / 'cues'
        directory.mkdir(exist_ok=True)
        nvs = b'new settings and cues\0' * 1024
        nvs = nvs.ljust(0x6000, b'\xff')
        (directory / 'merged-nvs.bin').write_bytes(nvs)
        report = {'schema': 1, 'board_id': 'm3', 'source_sha': self.candidate['source_sha'],
                  'backup_sha256': migration.sha(self.raw[0x9000:0xf000]), 'merged_sha256': migration.sha(nvs)}
        migration.save_json(directory / 'report.json', report)
        planned, raw, new, merged = self.plan(cue_directory=directory)
        device = Device(raw)
        migration.apply(device, planned, raw, new, merged, self.root / 'cue-apply.json')
        self.assertEqual([a[1] for a in device.actions if a[0] == 'write'], [0x20000, 0x9000, 0x10000])
        self.assertEqual(device.raw[0x9000:0xf000], nvs)
        migration.save_json(directory / 'report.json', report | {'backup_sha256': '0' * 64})
        with self.assertRaisesRegex(ValueError, 'stale or corrupt'):
            self.plan(cue_directory=directory)

    def test_finish_requires_signed_same_image_and_valid_health(self):
        planned, raw, new, nvs = self.plan()
        wired = self.root / 'wired-result.json'
        migration.apply(Device(raw), planned, raw, new, nvs, wired)
        info = {'board_id': 'm3', 'project': 'main-deck-m3', 'source_sha': self.candidate['source_sha'],
            'source_dirty': False, 'image_elf_sha256': self.candidate['image_elf_sha256'],
            'running_version': self.version, 'running_slot': 'factory', 'running_image_state': 'factory', 'state': 'idle'}
        idle = {'deck1': {'state_text': 'READY', 'playing': False}, 'deck2': {'state_text': 'IDLE', 'playing': False}}
        values = [info, idle,
            info | {'running_slot': 'ota_0', 'running_image_state': 'pending_verify'},
            info | {'running_slot': 'ota_0', 'running_image_state': 'valid'}]
        class Response:
            status = 200
            def __enter__(self): return self
            def __exit__(self, *_): pass
            def read(self): return b'{"ok":true}'
        with patch.object(migration, 'get_json', side_effect=values), \
             patch.object(migration.urllib.request, 'urlopen', return_value=Response()) as upload, \
             patch.object(migration.time, 'sleep'):
            result = migration.finish('http://192.168.4.1', self.release, self.public, wired, self.root / 'ota-result.json')
        self.assertEqual(result['stage'], 'ota_health_valid')
        self.assertEqual(result['physical_acceptance'], 'NOT RUN')
        request = upload.call_args.args[0]
        self.assertEqual(request.get_header('X-ddj-control'), '1')
        self.assertEqual(request.get_header('X-ddj-ota'), 'p4')
        self.assertEqual(request.data, (self.release / 'main-deck-m3.ddjota').read_bytes())
        with self.assertRaisesRegex(ValueError, 'selected candidate'):
            migration.verify_running(info | {'source_dirty': True}, self.candidate, 'factory')
        for changed in ({'state_text': 'LOADING', 'playing': False},
                        {'state_text': 'PLAYING', 'playing': True},
                        {'state': 'IDLE', 'playing': False}):
            with patch.object(migration, 'get_json', side_effect=[info, idle | {'deck2': changed}]), \
                 patch.object(migration.urllib.request, 'urlopen') as blocked_upload:
                with self.assertRaisesRegex(ValueError, 'stop playback'):
                    migration.finish('http://192.168.4.1', self.release, self.public, wired, self.root / 'blocked-ota.json')
                blocked_upload.assert_not_called()

    def test_cue_only_followup_preserves_factory_and_ota_selection(self):
        raw = bytearray(self.raw)
        raw[0x20000:0x20000 + len(self.new)] = self.new
        raw[0x420000:0x420000 + len(self.new)] = self.new
        directory = self.root / 'shared-backup'
        migration.backup(Device(raw), directory)
        cues = self.root / 'followup-cues'
        cues.mkdir()
        nvs = b'\xaa' * 0x6000
        (cues / 'merged-nvs.bin').write_bytes(nvs)
        migration.save_json(cues / 'report.json', {'schema': 1, 'board_id': 'm3',
            'source_sha': self.candidate['source_sha'], 'backup_sha256': migration.sha(raw[0x9000:0xf000]),
            'merged_sha256': migration.sha(nvs)})
        planned, saved, new, merged = self.plan(directory=directory, old_sha=migration.sha(self.new),
                                              cue_directory=cues, current_shared=True)
        device = Device(saved)
        result = migration.apply(device, planned, saved, new, merged, self.root / 'cue-only.json')
        self.assertEqual([a[1] for a in device.actions if a[0] == 'write'], [0x9000])
        self.assertEqual(result['stage'], 'cue_boot_requested')
        self.assertEqual(device.raw[:0x9000], raw[:0x9000])
        self.assertEqual(device.raw[0xf000:], raw[0xf000:])


if __name__ == '__main__':
    unittest.main()
