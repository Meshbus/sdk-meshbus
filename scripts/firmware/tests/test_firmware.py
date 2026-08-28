# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

import struct
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import firmware


def _key(path: Path) -> Ed25519PrivateKey:
    key = Ed25519PrivateKey.generate()
    path.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )
    return key


def _tlv(tlv_type: int, value: bytes) -> bytes:
    return struct.pack("<BBH", tlv_type, 0, len(value)) + value


def _image(
    path: Path,
    key: Ed25519PrivateKey,
    version: int,
    marker: int,
    *,
    protected_counters: tuple[bytes, ...] = (struct.pack("<I", 1),),
    unprotected_counters: tuple[bytes, ...] = (),
    key_hashes: tuple[bytes, ...] | None = None,
) -> None:
    header_size = 0x800
    payload = bytes([marker]) * 4096
    protected_entries = b"".join(
        _tlv(firmware.MCUBOOT_TLV_SEC_CNT, counter) for counter in protected_counters
    )
    protected = (
        struct.pack(
            "<HH",
            firmware.MCUBOOT_TLV_PROT_INFO_MAGIC,
            len(protected_entries) + 4,
        )
        + protected_entries
        if protected_entries
        else b""
    )
    header = struct.pack(
        "<IIHHIIBBHI",
        firmware.MCUBOOT_IMAGE_MAGIC,
        0,
        header_size,
        len(protected),
        len(payload),
        0,
        0,
        0,
        version,
        0,
    ) + bytes(header_size - 28)
    hash_region = header + payload + protected
    digest = firmware.hashlib.sha256(hash_region).digest()
    signature = key.sign(digest)
    if key_hashes is None:
        public_key_der = key.public_key().public_bytes(
            serialization.Encoding.DER,
            serialization.PublicFormat.SubjectPublicKeyInfo,
        )
        key_hashes = (firmware.hashlib.sha256(public_key_der).digest(),)
    entries = (
        _tlv(firmware.MCUBOOT_TLV_SHA256, digest)
        + b"".join(_tlv(firmware.MCUBOOT_TLV_KEYHASH, key_hash) for key_hash in key_hashes)
        + _tlv(firmware.MCUBOOT_TLV_ED25519, signature)
        + b"".join(
            _tlv(firmware.MCUBOOT_TLV_SEC_CNT, counter)
            for counter in unprotected_counters
        )
    )
    path.write_bytes(
        hash_region
        + struct.pack("<HH", firmware.MCUBOOT_TLV_INFO_MAGIC, len(entries) + 4)
        + entries
    )


def _args(source: Path, target: Path, output: Path, key: Path) -> SimpleNamespace:
    return SimpleNamespace(
        source=str(source),
        target=str(target),
        output=str(output),
        role="repeater",
        board_id="devkit_nrf54l15",
        soc_id="nrf54l15",
        hardware_min=1,
        hardware_max=1,
        partition_abi=1,
        campaign_id="00112233445566778899aabbccddeeff",
        image_key_id=1,
        image_public_key=str(key),
        manifest_key_id=1,
        security_counter=1,
        manifest_private_key=str(key),
        manifest_signature=None,
        manifest_public_key=None,
        signer_provenance="unit-test",
    )


def test_canonical_manifest_round_trip():
    manifest = {index: index for index in range(1, 21)}
    encoded = firmware._cbor_encode(manifest)
    assert firmware.decode_manifest(encoded) == manifest
    with pytest.raises(firmware.FirmwareError, match="canonical"):
        firmware.decode_manifest(b"\xb8\x14" + encoded[1:])


def test_package_round_trip_and_tamper_rejection(tmp_path: Path):
    key_path = tmp_path / "key.pem"
    key = _key(key_path)
    source, target = tmp_path / "source.bin", tmp_path / "target.bin"
    _image(source, key, 99, 0x11)
    _image(target, key, 100, 0x12)
    package = tmp_path / "package"

    created = firmware.create_package(_args(source, target, package, key_path))
    verified = firmware.verify_package(package, key_path, key_path)
    assert created["transfer_id"] == verified["transfer_id"]
    assert 88 < created["patch_size"] <= firmware.MAX_PATCH_SIZE
    raw_patch = (package / "patch.newp").read_bytes()[firmware.NEWP_HEADER_SIZE :]
    assert raw_patch[0] == 0x02

    signature = package / "manifest.sig"
    damaged = bytearray(signature.read_bytes())
    damaged[0] ^= 1
    signature.write_bytes(damaged)
    with pytest.raises(firmware.FirmwareError, match="inventory|signature"):
        firmware.verify_package(package, key_path, key_path)


def test_non_crle_patch_is_rejected():
    raw_patch = bytes((0x04, 0x01, 0x44, 0x00))
    with pytest.raises(firmware.FirmwareError, match="sequential CRLE"):
        firmware._validate_detools_patch_contract(raw_patch)


def test_wrong_image_signer_fails_before_patch_creation(tmp_path: Path):
    key_path, wrong_path = tmp_path / "key.pem", tmp_path / "wrong.pem"
    key, _ = _key(key_path), _key(wrong_path)
    source, target = tmp_path / "source.bin", tmp_path / "target.bin"
    _image(source, key, 99, 0x11)
    _image(target, key, 100, 0x12)
    args = _args(source, target, tmp_path / "package", key_path)
    args.image_public_key = str(wrong_path)
    with pytest.raises(firmware.FirmwareError, match="signature"):
        firmware.create_package(args)


@pytest.mark.parametrize(
    ("protected_counters", "unprotected_counters", "message"),
    (
        ((), (), "exactly one protected"),
        ((struct.pack("<I", 1), struct.pack("<I", 1)), (), "exactly one protected"),
        ((struct.pack("<H", 1),), (), "protected 4-byte uint32"),
        ((), (struct.pack("<I", 1),), "protected TLV area"),
        ((struct.pack("<I", 1),), (struct.pack("<I", 2),), "protected TLV area"),
    ),
)
def test_security_counter_must_be_unique_protected_uint32(
    tmp_path: Path,
    protected_counters: tuple[bytes, ...],
    unprotected_counters: tuple[bytes, ...],
    message: str,
):
    key_path = tmp_path / "key.pem"
    key = _key(key_path)
    image = tmp_path / "image.bin"
    _image(
        image,
        key,
        100,
        0x12,
        protected_counters=protected_counters,
        unprotected_counters=unprotected_counters,
    )

    with pytest.raises(firmware.FirmwareError, match=message):
        firmware._parse_image(image)


@pytest.mark.parametrize(
    ("key_hashes", "message"),
    (
        ((), "exactly one 32-byte"),
        ((bytes(32), bytes([1]) * 32), "exactly one 32-byte"),
        ((bytes(32),), "does not match"),
    ),
)
def test_image_key_hash_must_match_supplied_public_key(
    tmp_path: Path,
    key_hashes: tuple[bytes, ...],
    message: str,
):
    key_path = tmp_path / "key.pem"
    key = _key(key_path)
    image = tmp_path / "image.bin"
    _image(image, key, 100, 0x12, key_hashes=key_hashes)

    with pytest.raises(firmware.FirmwareError, match=message):
        firmware._verify_image_signature(image, key_path)
