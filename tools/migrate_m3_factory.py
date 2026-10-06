"""Preserving M3 wired identity migration; never erase the full chip.

backup leaves the device in its ROM/stub bootloader. plan is entirely offline.
apply rechecks the complete snapshot before changing factory/NVS/OTA selection.
finish verifies the factory identity, uploads the signed bundle, then requires
the OTA image to reach VALID on the same exact source/ELF identity.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile
import time
import urllib.request

from create_integration_candidate import verify_artifacts, verify_release_manifest, record

FLASH_SIZE = 0x1000000
PARTITIONS = {
    'nvs': (1, 2, 0x9000, 0x6000), 'phy_init': (1, 1, 0xf000, 0x1000),
    'otadata': (1, 0, 0x10000, 0x2000), 'factory': (0, 0, 0x20000, 0x400000),
    'ota_0': (0, 0x10, 0x420000, 0x400000), 'ota_1': (0, 0x11, 0x820000, 0x400000),
    'coredump': (1, 3, 0xc20000, 0x10000),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save_json(path, value):
    temporary = path.with_suffix('.tmp')
    with temporary.open('w', encoding='utf-8') as output:
        json.dump(value, output, indent=2)
        output.write('\n')
        output.flush()
        os.fsync(output.fileno())
    temporary.replace(path)


def partition_table(data):
    """Validate the IDF binary table, including its MD5 footer and flags."""
    result = {}
    for offset in range(0, min(len(data), 0xc00), 32):
        entry = data[offset:offset + 32]
        if len(entry) != 32:
            break
        magic = struct.unpack_from('<H', entry)[0]
        if magic == 0xebeb:
            if entry[:16] != b'\xeb\xeb' + b'\xff' * 14 or entry[16:] != hashlib.md5(data[:offset]).digest():
                raise ValueError('partition table MD5 mismatch')
            if any(byte != 0xff for byte in data[offset + 32:]):
                raise ValueError('unexpected trailing partition data')
            if result != PARTITIONS:
                raise ValueError('unsupported partition layout or flags')
            return result
        if magic != 0x50aa:
            break
        _, kind, subtype, address, size, name, flags = struct.unpack('<HBBII16sI', entry)
        label = name.split(b'\0', 1)[0].decode('ascii')
        if flags or label in result:
            raise ValueError('encrypted, flagged or duplicate partition')
        result[label] = (kind, subtype, address, size)
    raise ValueError('partition table is missing its MD5 footer')


def app_image(slot):
    """Locate the exact ESP image length in a full OTA/factory slot."""
    if len(slot) < 288 or slot[0] != 0xe9 or not 1 <= slot[1] <= 16:
        raise ValueError('missing ESP application header')
    if struct.unpack_from('<H', slot, 12)[0] != 0x12 or slot[23] != 1:
        raise ValueError('application must be a hashed ESP32-P4 image')
    checksum, end = 0xef, 24
    for _ in range(slot[1]):
        if end + 8 > len(slot):
            raise ValueError('truncated image segment header')
        length = struct.unpack_from('<I', slot, end + 4)[0]
        end += 8
        if length > len(slot) - end:
            raise ValueError('truncated image segment')
        for byte in slot[end:end + length]:
            checksum ^= byte
        end += length
    digest_offset = (end + 16) & ~15
    if digest_offset + 32 > len(slot) or slot[digest_offset - 1] != checksum:
        raise ValueError('application checksum mismatch')
    if sha(slot[:digest_offset]) != slot[digest_offset:digest_offset + 32].hex():
        raise ValueError('application appended SHA mismatch')
    if struct.unpack_from('<I', slot, 32)[0] != 0xabcd5432:
        raise ValueError('missing app descriptor')
    fields = {}
    for name, start in (('version', 48), ('project', 80), ('idf', 144)):
        field = slot[start:start + 32]
        if b'\0' not in field:
            raise ValueError('unterminated app descriptor')
        fields[name] = field.split(b'\0', 1)[0].decode('utf-8')
    image = slot[:digest_offset + 32]
    return image, fields | {'size': len(image), 'sha256': sha(image),
                            'image_elf_sha256': slot[176:208].hex()}


def device_identity(device):
    value = device.identity()
    if value['chip'] != 'ESP32-P4' or value['flash_size'] != FLASH_SIZE:
        raise ValueError('requires ESP32-P4 and exactly 16 MiB flash')
    if not 100 <= value['revision'] < 300 or value['secure_boot'] or value['flash_encrypted']:
        raise ValueError('unsupported silicon/security mode for existing M3 layout')
    if not re.fullmatch(r'(?:[0-9a-f]{2}:){5}[0-9a-f]{2}', value['mac']):
        raise ValueError('invalid device MAC')
    return value


def backup(device, directory):
    identity = device_identity(device)
    if directory.exists():
        raise ValueError('backup directory must be new')
    raw = device.read(0, FLASH_SIZE)
    if len(raw) != FLASH_SIZE:
        raise ValueError('incomplete full-flash backup')
    partition_table(raw[0x8000:0x9000])
    directory.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.m3-backup-', dir=directory.parent) as workspace:
        deliver = Path(workspace) / 'deliver'
        deliver.mkdir()
        files = {}
        ranges = {'full-flash': (0, FLASH_SIZE), 'bootloader': (0x2000, 0x6000),
                  'partition-table': (0x8000, 0x1000)}
        ranges |= {name: (values[2], values[3]) for name, values in PARTITIONS.items()}
        apps = {}
        for name, (offset, size) in ranges.items():
            path = deliver / f'{name}.bin'
            with path.open('wb') as output:
                output.write(raw[offset:offset + size])
                output.flush()
                os.fsync(output.fileno())
            files[name] = record(path) | {'offset': offset}
            if name in ('factory', 'ota_0', 'ota_1'):
                try:
                    _, apps[name] = app_image(path.read_bytes())
                except ValueError as error:
                    apps[name] = {'unusable': str(error)}
        manifest = {'schema': 'pajoniiir.m3-wired-backup.v1', 'device': identity,
                    'files': files, 'apps': apps, 'device_written': False}
        save_json(deliver / 'backup.json', manifest)
        deliver.rename(directory)
    return manifest


def load_backup(directory):
    manifest = json.loads((directory / 'backup.json').read_text())
    if manifest['schema'] != 'pajoniiir.m3-wired-backup.v1':
        raise ValueError('wrong backup schema')
    for name, entry in manifest['files'].items():
        if entry['file'] != f'{name}.bin' or '/' in name or '\\' in name:
            raise ValueError('invalid backup filename')
        if record(directory / entry['file']) != {k: entry[k] for k in ('file', 'size', 'sha256')}:
            raise ValueError('backup file hash mismatch')
    raw = (directory / 'full-flash.bin').read_bytes()
    if len(raw) != FLASH_SIZE:
        raise ValueError('incomplete backup')
    partition_table(raw[0x8000:0x9000])
    for name, entry in manifest['files'].items():
        if (directory / entry['file']).read_bytes() != raw[entry['offset']:entry['offset'] + entry['size']]:
            raise ValueError('backup region differs from full flash')
    return manifest, raw


def plan(directory, release, public_key, expected_mac, old_slot, old_sha, cue_directory=None,
         current_shared=False):
    backup_info, raw = load_backup(directory)
    if backup_info['device']['mac'] != expected_mac.lower():
        raise ValueError('backup belongs to another MAC')
    offset, size = PARTITIONS[old_slot][2:]
    _, old = app_image(raw[offset:offset + size])
    expected_project = 'main-deck-m3' if current_shared else 'main-deck-p4'
    if old['sha256'] != old_sha.lower() or old['project'] != expected_project or not old['version'].startswith('M3'):
        raise ValueError('selected historical M3 image SHA/project/version mismatch')
    evidence = json.loads((release / 'candidate-evidence.json').read_text())
    if evidence['project'] != 'main-deck-m3' or evidence['board'] != 'M3' or not evidence['software_verified']:
        raise ValueError('requires verified ordinary M3 candidate evidence')
    if not re.fullmatch('[0-9a-f]{40}', evidence['source_sha']):
        raise ValueError('invalid candidate source SHA')
    version, image, bundle = verify_artifacts(release, 'main-deck-m3', public_key)
    if current_shared and (old['sha256'] != image['sha256'] or cue_directory is None):
        raise ValueError('cue-only apply requires this installed candidate and prepared cue migration')
    verify_release_manifest(release, public_key, 'main-deck-m3', version, image, bundle)
    signed = json.loads((release / 'manifest.json').read_text(encoding='utf-8-sig'))
    new_image, desc = app_image((release / image['file']).read_bytes())
    if desc['idf'] != 'v6.0.2' or len(new_image) != image['size'] or desc['image_elf_sha256'] != evidence['image_elf_sha256']:
        raise ValueError('wrong IDF, image length or ELF identity')
    if evidence['image'] != image or evidence['bundle'] != bundle or signed.get('source_sha') != evidence['source_sha']:
        raise ValueError('candidate artifact/source evidence mismatch')
    partition_table((release / 'wired/partition-table.bin').read_bytes())
    if (release / 'wired/partition-table.bin').read_bytes().rstrip(b'\xff') != raw[0x8000:0x9000].rstrip(b'\xff'):
        raise ValueError('migration must preserve the installed partition table')
    nvs = None
    if cue_directory:
        report = json.loads((cue_directory / 'report.json').read_text())
        nvs = (cue_directory / 'merged-nvs.bin').read_bytes()
        if report['schema'] != 1 or report['board_id'] != 'm3' or report['source_sha'] != evidence['source_sha']:
            raise ValueError('cue migration has another target/source')
        if report['backup_sha256'] != sha(raw[0x9000:0xf000]) or report['merged_sha256'] != sha(nvs) or len(nvs) != 0x6000:
            raise ValueError('cue migration is stale or corrupt')
    result = {'schema': 'pajoniiir.m3-wired-migration.v1', 'device': backup_info['device'],
              'backup_sha256': sha(raw), 'old_slot': old_slot, 'old_image': old,
              'candidate': evidence, 'factory_offset': 0x20000, 'factory_image': image,
              'nvs_changed': nvs is not None, 'nvs_sha256': sha(nvs if nvs is not None else raw[0x9000:0xf000]),
              'ota_selection_offset': 0x10000, 'bootloader_changed': False,
              'partition_table_changed': False, 'full_chip_erase': False,
              'device_written': False, 'physical_acceptance': 'NOT RUN'}
    result['cue_only'] = current_shared
    return result, raw, new_image, nvs


def apply(device, planned, raw, image, nvs, result_path):
    save_json(result_path, planned | {'stage': 'preflight'})
    result = planned.copy()
    result['stage'] = 'preflight'
    try:
        if device_identity(device) != planned['device']:
            raise ValueError('connected device identity differs from backup')
        if device.read(0, FLASH_SIZE) != raw:
            raise ValueError('device changed since backup; take a fresh backup')
        result.update(stage='nvs_write_started' if planned['cue_only'] else 'factory_write_started', device_written=True)
        save_json(result_path, result)
        if not planned['cue_only']:
            device.write(0x20000, image)
        if device.read(0x20000, len(image)) != image:
            raise ValueError('factory readback failed; OTA selection not changed')
        result['stage'] = 'factory_verified'
        save_json(result_path, result)
        if nvs is not None:
            result['stage'] = 'nvs_write_started'
            save_json(result_path, result)
            device.write(0x9000, nvs)
        expected_nvs = nvs if nvs is not None else raw[0x9000:0xf000]
        if device.read(0x9000, 0x6000) != expected_nvs:
            raise ValueError('NVS verification failed; restore backup before boot')
        if device.read(0x2000, 0x7000) != raw[0x2000:0x9000]:
            raise ValueError('bootloader/partition table unexpectedly changed')
        if not planned['cue_only']:
            result['stage'] = 'ota_selection_reset_started'
            save_json(result_path, result)
            device.write(0x10000, b'\xff' * 0x2000)
        expected_selection = raw[0x10000:0x12000] if planned['cue_only'] else b'\xff' * 0x2000
        if device.read(0x10000, 0x2000) != expected_selection:
            raise ValueError('OTA selection readback failed; remain in bootloader')
        result['stage'] = 'wired_verified_before_reset'
        save_json(result_path, result)
        device.reset()
        result['stage'] = 'cue_boot_requested' if planned['cue_only'] else 'first_boot_requested'
        save_json(result_path, result)
        return result
    except Exception as error:
        save_json(result_path, result | {'failure': str(error), 'recovery_required': result['device_written']})
        raise


class EspDevice:
    def __init__(self, port):
        import esptool
        if esptool.__version__ != '5.3.1':
            raise ValueError('wired migration is qualified with esptool 5.3.1')
        self.tool = esptool
        self.esp = esptool.detect_chip(port=port)
        if self.esp.CHIP_NAME != 'ESP32-P4':
            raise ValueError('connected chip is not ESP32-P4')
        self.esp = self.esp.run_stub()
        self.esp.change_baud(460800)

    def identity(self):
        return {'chip': self.esp.CHIP_NAME, 'revision': self.esp.get_chip_revision(),
                'mac': ':'.join(f'{byte:02x}' for byte in self.esp.read_mac()),
                'flash_size': 1 << ((self.esp.flash_id() >> 16) & 0xff),
                'secure_boot': bool(self.esp.get_secure_boot_enabled()),
                'flash_encrypted': bool(self.esp.get_flash_encryption_enabled())}

    def read(self, offset, size):
        return self.tool.read_flash(self.esp, offset, size, no_progress=True)

    def write(self, offset, data):
        if offset not in (0x20000, 0x9000, 0x10000):
            raise ValueError('write outside migration regions')
        if (not data or (offset == 0x20000 and len(data) > 0x380000) or
                (offset == 0x9000 and len(data) != 0x6000) or
                (offset == 0x10000 and data != b'\xff' * 0x2000)):
            raise ValueError('write exceeds migration bounds')
        self.tool.write_flash(self.esp, [(offset, data)], flash_size='keep',
                              flash_mode='keep', flash_freq='keep', erase_all=False,
                              force=False, compress=True, no_progress=True)

    def reset(self):
        self.esp.hard_reset()

    def close(self):
        self.esp._port.close()


def get_json(base_url, path):
    with urllib.request.urlopen(base_url.rstrip('/') + path, timeout=4) as response:
        return json.load(response)


def verify_running(info, candidate, slot):
    expected = {'board_id': 'm3', 'project': 'main-deck-m3', 'source_sha': candidate['source_sha'],
                'source_dirty': False, 'image_elf_sha256': candidate['image_elf_sha256'],
                'running_version': candidate['version'], 'running_slot': slot}
    if any(info.get(key) != value for key, value in expected.items()):
        raise ValueError('running device is not the selected candidate/slot')


def finish(base_url, release, public_key, wired_result, result_path):
    result = json.loads(wired_result.read_text())
    if result.get('stage') != 'first_boot_requested':
        raise ValueError('requires successfully verified wired migration')
    candidate = result['candidate']
    version, image, bundle = verify_artifacts(release, 'main-deck-m3', public_key)
    if (image, bundle, version) != (candidate['image'], candidate['bundle'], candidate['version']):
        raise ValueError('signed OTA differs from installed factory candidate')
    info = get_json(base_url, '/api/firmware')
    verify_running(info, candidate, 'factory')
    if info.get('state') != 'idle' or info.get('running_image_state') != 'factory':
        raise ValueError('factory OTA service is not ready')
    status = get_json(base_url, '/api/status')
    if any(status.get(f'deck{deck}', {}).get('state') not in ('IDLE', 'READY') or
           status.get(f'deck{deck}', {}).get('playing') is not False for deck in (1, 2)):
        raise ValueError('stop playback before OTA migration qualification')
    output = {'schema': 'pajoniiir.m3-migration-ota.v1', 'candidate': candidate,
              'factory_verified': info, 'stage': 'upload_started', 'physical_acceptance': 'NOT RUN',
              'settings_operator_confirmation': 'NOT RUN'}
    save_json(result_path, output)
    request = urllib.request.Request(base_url.rstrip('/') + '/api/ota/p4',
        data=(release / bundle['file']).read_bytes(), method='POST',
        headers={'Content-Type': 'application/octet-stream', 'X-DDJ-Control': '1', 'X-DDJ-OTA': 'p4'})
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            if response.status != 200:
                raise ValueError('OTA upload did not return HTTP 200')
            output['upload_response'] = response.read().decode()
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            try:
                current = get_json(base_url, '/api/firmware')
            except (OSError, ValueError):
                time.sleep(1)
                continue
            if current.get('running_slot') == 'factory':
                verify_running(current, candidate, 'factory')
                time.sleep(1)
                continue
            verify_running(current, candidate, 'ota_0')
            if current.get('state') == 'idle' and current.get('running_image_state') == 'valid':
                output.update(stage='ota_health_valid', firmware=current)
                save_json(result_path, output)
                return output
            time.sleep(1)
        raise ValueError('OTA did not reach VALID within observation deadline; inspect rollback/recovery')
    except Exception as error:
        save_json(result_path, output | {'failure': str(error), 'stage': 'not_accepted'})
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    capture = commands.add_parser('backup')
    capture.add_argument('--port', required=True)
    capture.add_argument('--output-dir', type=Path, required=True)
    for name in ('plan', 'apply', 'apply-cues'):
        command = commands.add_parser(name)
        for argument in ('backup-dir', 'release-dir', 'public-key', 'result'):
            command.add_argument('--' + argument, type=Path, required=True)
        command.add_argument('--expected-mac', required=True)
        command.add_argument('--old-slot', choices=('factory', 'ota_0', 'ota_1'), required=True)
        command.add_argument('--old-image-sha256', required=True)
        command.add_argument('--cue-dir', type=Path)
        if name in ('apply', 'apply-cues'):
            command.add_argument('--port', required=True)
    completed = commands.add_parser('finish')
    completed.add_argument('--base-url', required=True)
    for argument in ('release-dir', 'public-key', 'wired-result', 'result'):
        completed.add_argument('--' + argument, type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'finish':
        finish(args.base_url, args.release_dir, args.public_key, args.wired_result, args.result)
    elif args.command == 'backup':
        device = EspDevice(args.port)
        try:
            report = backup(device, args.output_dir)
            print(json.dumps({'device': report['device'], 'apps': report['apps']}, indent=2))
        finally:
            device.close()
    else:
        if args.result.exists():
            raise ValueError('result file must be new')
        planned, raw, image, nvs = plan(args.backup_dir, args.release_dir, args.public_key,
            args.expected_mac, args.old_slot, args.old_image_sha256, args.cue_dir,
            current_shared=args.command == 'apply-cues')
        if args.command == 'plan':
            save_json(args.result, planned | {'stage': 'offline_plan'})
        else:
            device = EspDevice(args.port)
            try:
                apply(device, planned, raw, image, nvs, args.result)
            finally:
                device.close()
        print(f'{args.command}: {args.result}')


if __name__ == '__main__':
    main()
