import hashlib
import json
from pathlib import Path
import unittest

FIXTURES = Path(__file__).resolve().parents[1] / 'fixtures/p4'
REQUIRED = {'onset-44100.mp3', 'onset-44100.wav', 'short-48000.wav',
            'onset-48000.flac', 'export.pdb', 'ANLZ0000.DAT'}


class FixtureManifestTest(unittest.TestCase):
    def test_all_required_media_are_present_and_immutable(self):
        manifest = json.loads((FIXTURES / 'manifest.json').read_text())
        self.assertEqual(manifest['schema'], 1)
        self.assertEqual(set(manifest['files']), REQUIRED)
        for name in sorted(REQUIRED):
            with self.subTest(fixture=name):
                data = (FIXTURES / name).read_bytes()
                self.assertEqual(len(data), manifest['files'][name]['size'])
                self.assertEqual(hashlib.sha256(data).hexdigest(), manifest['files'][name]['sha256'])


if __name__ == '__main__':
    unittest.main()
