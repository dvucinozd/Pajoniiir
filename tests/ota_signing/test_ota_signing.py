import sys
import tempfile
import unittest
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import ota_signing  # noqa: E402
import create_integration_candidate as candidate  # noqa: E402


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

        with self.assertRaisesRegex(ValueError, "unsupported target"):
            self.bundle("retired-target", 0x0009, "retired-target")

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

    def candidate_image(self, project="main-deck-p4", version="M2.4-70-g12345678"):
        import struct
        image = bytearray(256)
        image[0] = 0xe9
        struct.pack_into("<H", image, 12, 0x12)
        struct.pack_into("<I", image, 32, 0xabcd5432)
        image[80:80 + len(project)] = project.encode()
        image[48:48 + len(version)] = version.encode()
        return bytes(image)

    def test_candidates_require_matching_signed_and_embedded_board(self):
        root = Path(self.temp.name)
        version = "M2.4-70-g12345678"
        import itertools
        for project, other in itertools.permutations(candidate.PROJECTS, 2):
            image = self.candidate_image(project, version)
            (root / f"{project}.bin").write_bytes(image)
            bundle = root / f"{project}.ddjota"
            bundle.write_bytes(ota_signing.create_bundle(image, self.private, "p4", 0x12, project, version, "rel-001"))
            self.assertEqual(candidate.verify_artifacts(root, project, self.public_path)[0], version)
            bundle.write_bytes(ota_signing.create_bundle(image, self.private, "p4", 0x12, other, version, "rel-001"))
            with self.assertRaisesRegex(ValueError, "descriptor mismatch"):
                candidate.verify_artifacts(root, project, self.public_path)
            bundle.write_bytes(ota_signing.create_bundle(image, self.private, "p4", 0x12, project, "M2.4", "rel-001"))
            with self.assertRaisesRegex(ValueError, "descriptor mismatch"):
                candidate.verify_artifacts(root, project, self.public_path)
            # Correct signature and descriptor, but binary file from another build.
            bundle.write_bytes(ota_signing.create_bundle(image + b"x", self.private, "p4", 0x12, project, version, "rel-001"))
            with self.assertRaisesRegex(ValueError, "bundle mismatch"):
                candidate.verify_artifacts(root, project, self.public_path)
            (root / f"{project}.bin").write_bytes(self.candidate_image(other))
            with self.assertRaisesRegex(ValueError, "wrong project"):
                candidate.verify_artifacts(root, project, self.public_path)

    def test_candidate_rejects_malformed_descriptor_and_budget(self):
        root = Path(self.temp.name)
        image = bytearray(self.candidate_image())
        image[80:112] = b"x" * 32
        with self.assertRaisesRegex(ValueError, "unterminated"):
            candidate.image_identity(image)
        image[32] = 0
        with self.assertRaisesRegex(ValueError, "missing"):
            candidate.image_identity(image)
        image = self.candidate_image() + bytes(0x380001 - 256)
        (root / "main-deck-p4.bin").write_bytes(image)
        with self.assertRaisesRegex(ValueError, "budget"):
            candidate.verify_artifacts(root, "main-deck-p4", self.public_path)

    def test_signed_release_manifest_must_describe_actual_candidate(self):
        import json
        root = Path(self.temp.name)
        image = {"file": "main-deck-p4.bin", "size": 256, "sha256": "a" * 64}
        (root / image["file"]).write_bytes(self.candidate_image())
        bundle = {"file": "main-deck-p4.ddjota", "size": 512, "sha256": "b" * 64}
        target = {"target": "p4", "project": "main-deck-p4", "file": image["file"],
                  "image_elf_sha256": "00" * 32,
                  "ota_bundle": bundle["file"], "size": image["size"], "sha256": image["sha256"],
                  "bundle_size": bundle["size"], "bundle_sha256": bundle["sha256"]}
        def write(target):
            payload = json.dumps({"schema_version": 2, "release_version": "M2.4", "targets": [target]}).encode()
            (root / "manifest.json").write_bytes(payload)
            (root / "manifest.sig").write_bytes(ota_signing._raw_sign(self.private, payload))
        write(target)
        candidate.verify_release_manifest(root, self.public_path, "main-deck-p4", "M2.4", image, bundle)
        write(target | {"project": "main-deck-jc1060"})
        with self.assertRaisesRegex(ValueError, "artifact mismatch"):
            candidate.verify_release_manifest(root, self.public_path, "main-deck-p4", "M2.4", image, bundle)
        write(target | {"bundle_sha256": "c" * 64})
        with self.assertRaisesRegex(ValueError, "artifact mismatch"):
            candidate.verify_release_manifest(root, self.public_path, "main-deck-p4", "M2.4", image, bundle)
        write(target | {"image_elf_sha256": "d" * 64})
        with self.assertRaisesRegex(ValueError, "artifact mismatch"):
            candidate.verify_release_manifest(root, self.public_path, "main-deck-p4", "M2.4", image, bundle)


if __name__ == "__main__":
    unittest.main()
