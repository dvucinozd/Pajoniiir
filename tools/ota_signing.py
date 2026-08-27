#!/usr/bin/env python3
"""Create and verify Pajoniiir signed OTA bundles.

The private P-256 key stays outside git. Firmware contains only the DER public
key and accepts a bundle after both manifest-signature and image-hash checks.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import (
    decode_dss_signature,
    encode_dss_signature,
)

MAGIC = b"DDJOTA1\0"
SCHEMA_VERSION = 1
HEADER_SIZE = 188
SIGNED_SIZE = 124
SIGNATURE_SIZE = 64
PROJECT_SIZE = 32
VERSION_SIZE = 32
KEY_ID_SIZE = 8
TARGET_CODES = {"p4": 1}

OFFSET_SCHEMA = 8
OFFSET_HEADER_SIZE = 10
OFFSET_TARGET = 12
OFFSET_FLAGS = 13
OFFSET_CHIP_ID = 14
OFFSET_IMAGE_SIZE = 16
OFFSET_PROJECT = 20
OFFSET_VERSION = 52
OFFSET_SHA256 = 84
OFFSET_KEY_ID = 116
OFFSET_SIGNATURE = 124


def _fixed_text(value: str, size: int, label: str) -> bytes:
    encoded = value.encode("utf-8")
    if not encoded or len(encoded) >= size or b"\0" in encoded:
        raise ValueError(f"{label} must be 1..{size - 1} UTF-8 bytes without NUL")
    return encoded + bytes(size - len(encoded))


def _load_private(path: Path) -> ec.EllipticCurvePrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(
        key.curve, ec.SECP256R1
    ):
        raise ValueError("OTA signing key must be an unencrypted ECDSA P-256 key")
    return key


def _load_public(path: Path) -> ec.EllipticCurvePublicKey:
    data = path.read_bytes()
    try:
        key = serialization.load_der_public_key(data)
    except ValueError:
        key = serialization.load_pem_public_key(data)
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(
        key.curve, ec.SECP256R1
    ):
        raise ValueError("OTA verification key must be an ECDSA P-256 public key")
    return key


def _raw_sign(key: ec.EllipticCurvePrivateKey, data: bytes) -> bytes:
    der = key.sign(data, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def _raw_verify(key: ec.EllipticCurvePublicKey, signature: bytes, data: bytes) -> None:
    if len(signature) != SIGNATURE_SIZE:
        raise ValueError("signature must contain 64 raw r||s bytes")
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    key.verify(encode_dss_signature(r, s), data, ec.ECDSA(hashes.SHA256()))


def create_bundle(
    image: bytes,
    private_key: ec.EllipticCurvePrivateKey,
    target: str,
    chip_id: int,
    project: str,
    version: str,
    key_id: str,
) -> bytes:
    if target not in TARGET_CODES:
        raise ValueError(f"unsupported target: {target}")
    if not 0 <= chip_id <= 0xFFFF:
        raise ValueError("chip ID is outside uint16 range")
    if len(image) < 24 or len(image) > 0xFFFFFFFF:
        raise ValueError("firmware image size is invalid")

    header = bytearray(HEADER_SIZE)
    header[: len(MAGIC)] = MAGIC
    struct.pack_into("<H", header, OFFSET_SCHEMA, SCHEMA_VERSION)
    struct.pack_into("<H", header, OFFSET_HEADER_SIZE, HEADER_SIZE)
    header[OFFSET_TARGET] = TARGET_CODES[target]
    header[OFFSET_FLAGS] = 0
    struct.pack_into("<H", header, OFFSET_CHIP_ID, chip_id)
    struct.pack_into("<I", header, OFFSET_IMAGE_SIZE, len(image))
    header[OFFSET_PROJECT : OFFSET_PROJECT + PROJECT_SIZE] = _fixed_text(
        project, PROJECT_SIZE, "project"
    )
    header[OFFSET_VERSION : OFFSET_VERSION + VERSION_SIZE] = _fixed_text(
        version, VERSION_SIZE, "version"
    )
    header[OFFSET_SHA256 : OFFSET_SHA256 + 32] = hashlib.sha256(image).digest()
    header[OFFSET_KEY_ID : OFFSET_KEY_ID + KEY_ID_SIZE] = _fixed_text(
        key_id, KEY_ID_SIZE, "key ID"
    )
    header[OFFSET_SIGNATURE : OFFSET_SIGNATURE + SIGNATURE_SIZE] = _raw_sign(
        private_key, bytes(header[:SIGNED_SIZE])
    )
    return bytes(header) + image


def inspect_bundle(bundle: bytes, public_key: ec.EllipticCurvePublicKey) -> dict[str, object]:
    if len(bundle) < HEADER_SIZE:
        raise ValueError("bundle is shorter than its manifest header")
    header = bundle[:HEADER_SIZE]
    if header[:8] != MAGIC:
        raise ValueError("bad bundle magic")
    schema, header_size = struct.unpack_from("<HH", header, OFFSET_SCHEMA)
    if schema != SCHEMA_VERSION or header_size != HEADER_SIZE:
        raise ValueError("unsupported bundle schema/header size")
    if header[OFFSET_FLAGS] != 0:
        raise ValueError("unsupported manifest flags")
    image_size = struct.unpack_from("<I", header, OFFSET_IMAGE_SIZE)[0]
    if len(bundle) != HEADER_SIZE + image_size:
        raise ValueError("bundle length does not match signed image size")
    _raw_verify(
        public_key,
        header[OFFSET_SIGNATURE : OFFSET_SIGNATURE + SIGNATURE_SIZE],
        header[:SIGNED_SIZE],
    )
    image = bundle[HEADER_SIZE:]
    expected_hash = header[OFFSET_SHA256 : OFFSET_SHA256 + 32]
    if hashlib.sha256(image).digest() != expected_hash:
        raise ValueError("firmware SHA-256 does not match signed manifest")

    def text(offset: int, size: int) -> str:
        field = header[offset : offset + size]
        if b"\0" not in field:
            raise ValueError("unterminated manifest text field")
        value, padding = field.split(b"\0", 1)
        if not value or any(padding):
            raise ValueError("non-canonical manifest text field")
        return value.decode("utf-8")

    target_code = header[OFFSET_TARGET]
    target = next((name for name, code in TARGET_CODES.items() if code == target_code), None)
    if target is None:
        raise ValueError("unknown manifest target")
    return {
        "schema_version": schema,
        "target": target,
        "chip_id": struct.unpack_from("<H", header, OFFSET_CHIP_ID)[0],
        "image_size": image_size,
        "project": text(OFFSET_PROJECT, PROJECT_SIZE),
        "version": text(OFFSET_VERSION, VERSION_SIZE),
        "sha256": expected_hash.hex(),
        "key_id": text(OFFSET_KEY_ID, KEY_ID_SIZE),
    }


def generate_key(private_path: Path, public_path: Path) -> None:
    if private_path.exists() or public_path.exists():
        raise FileExistsError("refusing to overwrite an existing OTA key")
    private_path.parent.mkdir(parents=True, exist_ok=True)
    public_path.parent.mkdir(parents=True, exist_ok=True)
    key = ec.generate_private_key(ec.SECP256R1())
    private_path.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )
    public_path.write_bytes(
        key.public_key().public_bytes(
            serialization.Encoding.DER,
            serialization.PublicFormat.SubjectPublicKeyInfo,
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    gen = sub.add_parser("generate-key", help="generate a new P-256 signing key pair")
    gen.add_argument("--private", type=Path, required=True)
    gen.add_argument("--public", type=Path, required=True)

    bundle_cmd = sub.add_parser("bundle", help="create a signed target .ddjota bundle")
    bundle_cmd.add_argument("--private-key", type=Path, required=True)
    bundle_cmd.add_argument("--target", choices=sorted(TARGET_CODES), required=True)
    bundle_cmd.add_argument("--chip-id", type=lambda value: int(value, 0), required=True)
    bundle_cmd.add_argument("--project", required=True)
    bundle_cmd.add_argument("--version", required=True)
    bundle_cmd.add_argument("--key-id", default="rel-001")
    bundle_cmd.add_argument("--input", type=Path, required=True)
    bundle_cmd.add_argument("--output", type=Path, required=True)

    verify = sub.add_parser("verify-bundle", help="verify and print bundle metadata")
    verify.add_argument("--public-key", type=Path, required=True)
    verify.add_argument("--input", type=Path, required=True)

    sign_file = sub.add_parser("sign-file", help="write a raw P-256 signature for a file")
    sign_file.add_argument("--private-key", type=Path, required=True)
    sign_file.add_argument("--input", type=Path, required=True)
    sign_file.add_argument("--output", type=Path, required=True)

    verify_file = sub.add_parser("verify-file", help="verify a raw P-256 file signature")
    verify_file.add_argument("--public-key", type=Path, required=True)
    verify_file.add_argument("--input", type=Path, required=True)
    verify_file.add_argument("--signature", type=Path, required=True)

    args = parser.parse_args()
    if args.command == "generate-key":
        generate_key(args.private, args.public)
    elif args.command == "bundle":
        bundle = create_bundle(
            args.input.read_bytes(),
            _load_private(args.private_key),
            args.target,
            args.chip_id,
            args.project,
            args.version,
            args.key_id,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(bundle)
    elif args.command == "verify-bundle":
        info = inspect_bundle(args.input.read_bytes(), _load_public(args.public_key))
        for key, value in info.items():
            print(f"{key}={value}")
    elif args.command == "sign-file":
        args.output.write_bytes(
            _raw_sign(_load_private(args.private_key), args.input.read_bytes())
        )
    elif args.command == "verify-file":
        _raw_verify(
            _load_public(args.public_key),
            args.signature.read_bytes(),
            args.input.read_bytes(),
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (InvalidSignature, OSError, ValueError) as exc:
        raise SystemExit(f"OTA signing error: {exc}") from exc
