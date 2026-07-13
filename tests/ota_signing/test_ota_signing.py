import sys
import tempfile
import unittest
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import ota_signing  # noqa: E402


class OtaSigningTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.private_path = root / "private.pem"
        self.public_path = root / "public.der"
        ota_signing.generate_key(self.private_path, self.public_path)
        self.private = ota_signing._load_private(self.private_path)
        self.public = ota_signing._load_public(self.public_path)
        self.image = b"\xe9" + bytes(range(1, 24)) + bytes(4096)

    def tearDown(self):
        self.temp.cleanup()

    def bundle(self, target="p4", chip_id=0x0012, project="main-deck-p4"):
        return ota_signing.create_bundle(
            self.image,
            self.private,
            target,
            chip_id,
            project,
            "RC2-test",
            "rel-001",
        )

    def test_valid_bundle_round_trip(self):
        info = ota_signing.inspect_bundle(self.bundle(), self.public)
        self.assertEqual(info["target"], "p4")
        self.assertEqual(info["chip_id"], 0x0012)
        self.assertEqual(info["project"], "main-deck-p4")
        self.assertEqual(info["version"], "RC2-test")
        self.assertEqual(info["image_size"], len(self.image))

        s3 = self.bundle("s3", 0x0009, "control-board-s3")
        self.assertEqual(ota_signing.inspect_bundle(s3, self.public)["target"], "s3")

    def test_tampered_signed_manifest_is_rejected(self):
        bundle = bytearray(self.bundle())
        bundle[ota_signing.OFFSET_VERSION] ^= 1
        with self.assertRaises(InvalidSignature):
            ota_signing.inspect_bundle(bytes(bundle), self.public)

    def test_tampered_image_is_rejected(self):
        bundle = bytearray(self.bundle())
        bundle[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            ota_signing.inspect_bundle(bytes(bundle), self.public)

    def test_wrong_key_is_rejected(self):
        other_private = Path(self.temp.name) / "other.pem"
        other_public = Path(self.temp.name) / "other.der"
        ota_signing.generate_key(other_private, other_public)
        with self.assertRaises(InvalidSignature):
            ota_signing.inspect_bundle(
                self.bundle(), ota_signing._load_public(other_public)
            )

    def test_truncated_and_extended_bundles_are_rejected(self):
        bundle = self.bundle()
        with self.assertRaisesRegex(ValueError, "length"):
            ota_signing.inspect_bundle(bundle[:-1], self.public)
        with self.assertRaisesRegex(ValueError, "length"):
            ota_signing.inspect_bundle(bundle + b"x", self.public)

    def test_raw_file_signature(self):
        payload = b'{"schema_version":2}'
        signature = ota_signing._raw_sign(self.private, payload)
        ota_signing._raw_verify(self.public, signature, payload)
        with self.assertRaises(InvalidSignature):
            ota_signing._raw_verify(self.public, signature, payload + b"x")


if __name__ == "__main__":
    unittest.main()
