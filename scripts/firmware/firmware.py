# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

"""Create and verify signed Meshbus FIRMWARE packages.

West Python owns deterministic delta generation.  The Rust ``meshbus`` tool
owns device inspection and transfer; this module deliberately has no serial or
radio transport implementation.
"""

import hashlib
import json
import shutil
import struct
import subprocess
import tempfile
import uuid
from pathlib import Path
from typing import Any, Optional

from west import log
from west.commands import WestCommand


MCUBOOT_IMAGE_MAGIC = 0x96F3B83D
MCUBOOT_TLV_INFO_MAGIC = 0x6907
MCUBOOT_TLV_PROT_INFO_MAGIC = 0x6908
MCUBOOT_TLV_KEYHASH = 0x01
MCUBOOT_TLV_SHA256 = 0x10
MCUBOOT_TLV_ED25519 = 0x24
MCUBOOT_TLV_SEC_CNT = 0x50

PROTOCOL_VERSION = 1
PARTITION_ABI = 1
PATCH_FORMAT_NEWP = 1
CHUNK_SIZE = 448
MAX_PATCH_SIZE = 0x6000
MAX_MANIFEST_SIZE = 384
NEWP_HEADER_SIZE = 88
DETOOLS_PATCH_TYPE_SEQUENTIAL = 0
DETOOLS_COMPRESSION_CRLE = 2
ROLE_IDS = {"repeater": 1, "room": 2, "sensor": 3}
DEFAULT_OUTPUT_DIR = "build/_firmware"


class FirmwareError(RuntimeError):
    """Stable user-facing package validation error."""


def _sha256(path: Path) -> bytes:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            digest.update(chunk)
    return digest.digest()


def _version_text(version: tuple[int, int, int, int]) -> str:
    major, minor, patch, build = version
    return f"{major}.{minor}.{patch}" + (f"+{build}" if build else "")


def _cbor_head(major: int, value: int) -> bytes:
    if value < 0:
        raise FirmwareError("canonical manifest values must be non-negative")
    if value < 24:
        return bytes([(major << 5) | value])
    if value <= 0xFF:
        return bytes([(major << 5) | 24, value])
    if value <= 0xFFFF:
        return bytes([(major << 5) | 25]) + struct.pack(">H", value)
    if value <= 0xFFFFFFFF:
        return bytes([(major << 5) | 26]) + struct.pack(">I", value)
    if value <= 0xFFFFFFFFFFFFFFFF:
        return bytes([(major << 5) | 27]) + struct.pack(">Q", value)
    raise FirmwareError("canonical manifest integer exceeds uint64")


def _cbor_encode(value: Any) -> bytes:
    if isinstance(value, bool):
        return b"\xf5" if value else b"\xf4"
    if isinstance(value, int):
        return _cbor_head(0, value)
    if isinstance(value, bytes):
        return _cbor_head(2, len(value)) + value
    if isinstance(value, str):
        encoded = value.encode("utf-8")
        return _cbor_head(3, len(encoded)) + encoded
    if isinstance(value, (list, tuple)):
        return _cbor_head(4, len(value)) + b"".join(_cbor_encode(v) for v in value)
    if isinstance(value, dict):
        encoded = [(_cbor_encode(k), _cbor_encode(v)) for k, v in value.items()]
        encoded.sort(key=lambda item: (len(item[0]), item[0]))
        return _cbor_head(5, len(encoded)) + b"".join(k + v for k, v in encoded)
    raise FirmwareError(f"unsupported canonical CBOR value: {type(value).__name__}")


class _CborReader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def _head(self) -> tuple[int, int]:
        if self.pos >= len(self.data):
            raise FirmwareError("truncated canonical manifest")
        head = self.data[self.pos]
        self.pos += 1
        major, ai = head >> 5, head & 0x1F
        if ai < 24:
            return major, ai
        widths = {24: 1, 25: 2, 26: 4, 27: 8}
        width = widths.get(ai)
        if width is None or self.pos + width > len(self.data):
            raise FirmwareError("indefinite or truncated canonical manifest value")
        value = int.from_bytes(self.data[self.pos : self.pos + width], "big")
        self.pos += width
        minima = {1: 24, 2: 0x100, 4: 0x10000, 8: 0x100000000}
        if value < minima[width]:
            raise FirmwareError("non-minimal canonical manifest integer")
        return major, value

    def read(self) -> Any:
        major, value = self._head()
        if major == 0:
            return value
        if major in (2, 3):
            if self.pos + value > len(self.data):
                raise FirmwareError("truncated canonical manifest string")
            raw = self.data[self.pos : self.pos + value]
            self.pos += value
            return raw if major == 2 else raw.decode("utf-8")
        if major == 4:
            return [self.read() for _ in range(value)]
        if major == 5:
            result: dict[Any, Any] = {}
            previous = b""
            for _ in range(value):
                start = self.pos
                key = self.read()
                encoded = self.data[start : self.pos]
                if previous and (len(encoded), encoded) <= (len(previous), previous):
                    raise FirmwareError("canonical manifest map keys are not ordered")
                previous = encoded
                if key in result:
                    raise FirmwareError("duplicate canonical manifest key")
                result[key] = self.read()
            return result
        if major == 7 and value in (20, 21):
            return value == 21
        raise FirmwareError("unsupported canonical manifest type")


def decode_manifest(encoded: bytes) -> dict[int, Any]:
    reader = _CborReader(encoded)
    value = reader.read()
    if reader.pos != len(encoded) or not isinstance(value, dict):
        raise FirmwareError("manifest must be exactly one canonical CBOR map")
    if list(value) != list(range(1, 21)) or _cbor_encode(value) != encoded:
        raise FirmwareError("manifest is not the canonical v1 20-key envelope")
    return value


def _parse_image(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < 32:
        raise FirmwareError(f"MCUboot image is truncated: {path}")
    magic, _, header_size, protected_size, image_size, _, major, minor, revision, build = (
        struct.unpack_from("<IIHHIIBBHI", data, 0)
    )
    if magic != MCUBOOT_IMAGE_MAGIC or header_size < 32:
        raise FirmwareError(f"not an MCUboot image: {path}")
    protected_offset = header_size + image_size
    tlv_offset = protected_offset
    protected_tlvs: dict[int, list[bytes]] = {}
    unprotected_tlvs: dict[int, list[bytes]] = {}

    def parse_area(
        offset: int,
        expected_magic: int,
        tlvs: dict[int, list[bytes]],
        expected_size: Optional[int] = None,
    ) -> int:
        if offset + 4 > len(data):
            raise FirmwareError(f"MCUboot TLV area is truncated: {path}")
        area_magic, area_size = struct.unpack_from("<HH", data, offset)
        if area_magic != expected_magic or area_size < 4 or offset + area_size > len(data):
            raise FirmwareError(f"invalid MCUboot TLV area: {path}")
        if expected_size is not None and area_size != expected_size:
            raise FirmwareError(f"protected MCUboot TLV size mismatch: {path}")
        cursor = offset + 4
        while cursor < offset + area_size:
            if cursor + 4 > offset + area_size:
                raise FirmwareError(f"truncated MCUboot TLV entry: {path}")
            tlv_type, _, length = struct.unpack_from("<BBH", data, cursor)
            cursor += 4
            if cursor + length > offset + area_size:
                raise FirmwareError(f"invalid MCUboot TLV entry length: {path}")
            tlvs.setdefault(tlv_type, []).append(data[cursor : cursor + length])
            cursor += length
        return offset + area_size

    if protected_size:
        tlv_offset = parse_area(
            protected_offset,
            MCUBOOT_TLV_PROT_INFO_MAGIC,
            protected_tlvs,
            protected_size,
        )
    hash_region_end = tlv_offset
    parse_area(tlv_offset, MCUBOOT_TLV_INFO_MAGIC, unprotected_tlvs)

    protected_counters = protected_tlvs.get(MCUBOOT_TLV_SEC_CNT, [])
    if unprotected_tlvs.get(MCUBOOT_TLV_SEC_CNT):
        raise FirmwareError(f"MCUboot security counter must be in the protected TLV area: {path}")
    if len(protected_counters) != 1:
        raise FirmwareError(f"signed image lacks exactly one protected security counter: {path}")
    if len(protected_counters[0]) != 4:
        raise FirmwareError(f"MCUboot security counter must be a protected 4-byte uint32: {path}")
    if (
        len(unprotected_tlvs.get(MCUBOOT_TLV_SHA256, [])) != 1
        or len(unprotected_tlvs[MCUBOOT_TLV_SHA256][0]) != 32
    ):
        raise FirmwareError(f"signed image lacks its SHA-256 TLV: {path}")
    if (
        len(unprotected_tlvs.get(MCUBOOT_TLV_KEYHASH, [])) != 1
        or len(unprotected_tlvs[MCUBOOT_TLV_KEYHASH][0]) != 32
    ):
        raise FirmwareError(f"signed image lacks exactly one 32-byte MCUboot key hash TLV: {path}")
    if (
        len(unprotected_tlvs.get(MCUBOOT_TLV_ED25519, [])) != 1
        or len(unprotected_tlvs[MCUBOOT_TLV_ED25519][0]) != 64
    ):
        raise FirmwareError(f"image is not signed exactly once with Ed25519: {path}")
    return {
        "version": (major, minor, revision, build),
        "security_counter": int.from_bytes(protected_counters[0], "little"),
        "size": len(data),
        "sha256": hashlib.sha256(data).digest(),
        "_hash_region": data[:hash_region_end],
        "_image_digest": unprotected_tlvs[MCUBOOT_TLV_SHA256][0],
        "_key_hash": unprotected_tlvs[MCUBOOT_TLV_KEYHASH][0],
        "_signature": unprotected_tlvs[MCUBOOT_TLV_ED25519][0],
    }


def _verify_image_signature(path: Path, public_key: Path) -> dict[str, Any]:
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import serialization

    info = _parse_image(path)
    digest = hashlib.sha256(info["_hash_region"]).digest()
    if digest != info["_image_digest"]:
        raise FirmwareError(f"MCUboot image hash TLV is invalid: {path}")
    key = _load_public_key(public_key)
    public_key_der = key.public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )
    if hashlib.sha256(public_key_der).digest() != info["_key_hash"]:
        raise FirmwareError(
            f"MCUboot image key hash does not match the supplied signature public key: {path}"
        )
    try:
        key.verify(info["_signature"], digest)
    except InvalidSignature as error:
        raise FirmwareError(f"MCUboot Ed25519 signature is invalid: {path}") from error
    return info


def _newp_header(source: Path, target: Path, raw_patch: bytes) -> bytes:
    source_info = _parse_image(source)
    target_info = _parse_image(target)
    source_version = struct.pack("<BBH", *source_info["version"][:3])
    target_version = struct.pack("<BBH", *target_info["version"][:3])
    return b"".join(
        (
            b"NEWP",
            source_version,
            target_version,
            struct.pack("<III", len(raw_patch), source.stat().st_size, target.stat().st_size),
            source_info["sha256"],
            target_info["sha256"],
            raw_patch,
        )
    )


def _validate_detools_patch_contract(raw_patch: bytes) -> None:
    expected_header = (
        (DETOOLS_PATCH_TYPE_SEQUENTIAL << 4) | DETOOLS_COMPRESSION_CRLE
    )
    if len(raw_patch) < 2 or raw_patch[0] != expected_header:
        raise FirmwareError("detools patch must use sequential CRLE encoding")


def _apply_patch(source: Path, patch: Path, output: Path) -> None:
    data = patch.read_bytes()
    if len(data) <= NEWP_HEADER_SIZE or data[:4] != b"NEWP":
        raise FirmwareError("patch does not contain a valid NEWP envelope")
    _validate_detools_patch_contract(data[NEWP_HEADER_SIZE:])
    raw_patch = output.with_suffix(".detools")
    raw_patch.write_bytes(data[NEWP_HEADER_SIZE:])
    try:
        subprocess.run(
            ["detools", "apply_patch", str(source), str(raw_patch), str(output)],
            check=True,
            capture_output=True,
        )
    except FileNotFoundError as error:
        raise FirmwareError("detools is unavailable in the active Zephyr environment") from error
    except subprocess.CalledProcessError as error:
        detail = error.stderr.decode(errors="replace").strip()
        raise FirmwareError(f"offline patch reconstruction failed: {detail}") from error
    finally:
        raw_patch.unlink(missing_ok=True)


def _load_public_key(path: Path):
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

    raw = path.read_bytes()
    try:
        key = serialization.load_pem_public_key(raw)
    except ValueError:
        try:
            private = serialization.load_pem_private_key(raw, password=None)
            key = private.public_key()
        except ValueError:
            if len(raw) == 32:
                key = Ed25519PublicKey.from_public_bytes(raw)
            else:
                raise FirmwareError("manifest public key must be Ed25519 PEM or 32 raw bytes")
    if isinstance(key, Ed25519PrivateKey):
        key = key.public_key()
    if not isinstance(key, Ed25519PublicKey):
        raise FirmwareError("manifest public key is not Ed25519")
    return key


def _sign_manifest(path: Path, manifest: bytes) -> bytes:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise FirmwareError("manifest private key is not Ed25519")
    return key.sign(manifest)


def _verify_signature(public_key: Path, manifest: bytes, signature: bytes) -> None:
    from cryptography.exceptions import InvalidSignature

    if len(signature) != 64:
        raise FirmwareError("detached manifest signature must be exactly 64 bytes")
    try:
        _load_public_key(public_key).verify(signature, manifest)
    except InvalidSignature as error:
        raise FirmwareError("detached manifest signature is invalid") from error


def _manifest_json(manifest: dict[int, Any]) -> dict[str, Any]:
    names = {
        1: "protocol_version", 2: "campaign_id", 3: "role", 4: "board_id",
        5: "soc_id", 6: "hardware_revision_min", 7: "hardware_revision_max",
        8: "partition_abi", 9: "source_version", 10: "target_version",
        11: "source_sha256", 12: "target_sha256", 13: "patch_sha256",
        14: "patch_size", 15: "chunk_size", 16: "image_key_id",
        17: "manifest_key_id", 18: "security_counter",
        19: "downgrade_allowed", 20: "patch_format",
    }
    result: dict[str, Any] = {}
    for key, value in manifest.items():
        if isinstance(value, bytes):
            value = value.hex()
        result[names[key]] = value
    return result


def _validate_manifest(manifest: dict[int, Any]) -> None:
    if manifest[1] != PROTOCOL_VERSION or manifest[8] != PARTITION_ABI:
        raise FirmwareError("unsupported FIRMWARE protocol or partition ABI")
    if manifest[3] not in ROLE_IDS.values() or not isinstance(manifest[4], str) or not manifest[4]:
        raise FirmwareError("invalid manifest target identity")
    if manifest[5] != "nrf54l15" or manifest[6] > manifest[7]:
        raise FirmwareError("invalid manifest SoC or hardware revision range")
    if len(manifest[2]) != 16 or any(len(manifest[key]) != 32 for key in (11, 12, 13)):
        raise FirmwareError("invalid manifest campaign or hash width")
    if any(not isinstance(manifest[key], list) or len(manifest[key]) != 4 for key in (9, 10)):
        raise FirmwareError("invalid manifest version tuple")
    if tuple(manifest[10]) <= tuple(manifest[9]) or manifest[19] is not False:
        raise FirmwareError("remote FIRMWARE target version must increase and downgrade must be false")
    if not NEWP_HEADER_SIZE < manifest[14] <= MAX_PATCH_SIZE:
        raise FirmwareError("manifest patch size exceeds the 24 KiB contract")
    if manifest[15] != CHUNK_SIZE or manifest[20] != PATCH_FORMAT_NEWP:
        raise FirmwareError("unsupported manifest chunk geometry or patch format")


def create_package(args) -> dict[str, Any]:
    source, target = Path(args.source).resolve(), Path(args.target).resolve()
    output = Path(args.output or DEFAULT_OUTPUT_DIR).resolve()
    if not source.is_file() or not target.is_file():
        raise FirmwareError("source and target signed images must both exist")
    if output.exists() and any(output.iterdir()):
        raise FirmwareError(f"output directory is not empty: {output}")
    image_public_key = Path(args.image_public_key)
    source_info = _verify_image_signature(source, image_public_key)
    target_info = _verify_image_signature(target, image_public_key)
    if target_info["version"] <= source_info["version"]:
        raise FirmwareError("target MCUboot version must be greater than source")
    if target_info["security_counter"] != args.security_counter:
        raise FirmwareError("target protected security counter does not match the manifest")
    if target_info["security_counter"] < source_info["security_counter"]:
        raise FirmwareError("target protected security counter is a downgrade")
    campaign_id = bytes.fromhex(args.campaign_id) if args.campaign_id else uuid.uuid4().bytes
    if len(campaign_id) != 16:
        raise FirmwareError("campaign ID must be exactly 16 bytes of hexadecimal")

    output.mkdir(parents=True, exist_ok=True)
    source_name, target_name, patch_name = "source.signed.bin", "target.signed.bin", "patch.newp"
    shutil.copyfile(source, output / source_name)
    shutil.copyfile(target, output / target_name)
    with tempfile.TemporaryDirectory() as temporary:
        raw_patch = Path(temporary) / "patch.detools"
        try:
            subprocess.run(
                [
                    "detools",
                    "create_patch",
                    "--compression",
                    "crle",
                    str(source),
                    str(target),
                    str(raw_patch),
                ],
                check=True,
                capture_output=True,
            )
        except FileNotFoundError as error:
            raise FirmwareError("detools is unavailable in the active Zephyr environment") from error
        except subprocess.CalledProcessError as error:
            detail = error.stderr.decode(errors="replace").strip()
            raise FirmwareError(f"delta creation failed: {detail}") from error
        raw_patch_data = raw_patch.read_bytes()
        _validate_detools_patch_contract(raw_patch_data)
        patch = _newp_header(source, target, raw_patch_data)
    if len(patch) > MAX_PATCH_SIZE:
        raise FirmwareError(f"complete NEWP patch is {len(patch)} bytes; limit is {MAX_PATCH_SIZE}")
    (output / patch_name).write_bytes(patch)
    with tempfile.TemporaryDirectory() as temporary:
        reconstructed = Path(temporary) / "target.bin"
        _apply_patch(source, output / patch_name, reconstructed)
        if _sha256(reconstructed) != target_info["sha256"]:
            raise FirmwareError("offline reconstruction does not equal the retained target image")

    manifest_map = {
        1: PROTOCOL_VERSION,
        2: campaign_id,
        3: ROLE_IDS[args.role],
        4: args.board_id,
        5: args.soc_id,
        6: args.hardware_min,
        7: args.hardware_max,
        8: args.partition_abi,
        9: list(source_info["version"]),
        10: list(target_info["version"]),
        11: source_info["sha256"],
        12: target_info["sha256"],
        13: hashlib.sha256(patch).digest(),
        14: len(patch),
        15: CHUNK_SIZE,
        16: args.image_key_id,
        17: args.manifest_key_id,
        18: args.security_counter,
        19: False,
        20: PATCH_FORMAT_NEWP,
    }
    _validate_manifest(manifest_map)
    manifest = _cbor_encode(manifest_map)
    if len(manifest) > MAX_MANIFEST_SIZE:
        raise FirmwareError(f"canonical manifest is {len(manifest)} bytes; limit is {MAX_MANIFEST_SIZE}")
    if args.manifest_private_key:
        signature = _sign_manifest(Path(args.manifest_private_key), manifest)
    else:
        signature = Path(args.manifest_signature).read_bytes()
        _verify_signature(Path(args.manifest_public_key), manifest, signature)
    (output / "manifest.cbor").write_bytes(manifest)
    (output / "manifest.sig").write_bytes(signature)

    transfer_id = hashlib.sha256(manifest).hexdigest()
    inventory = {
        "schema_version": 1,
        "transfer_id": transfer_id,
        "role": args.role,
        "board_id": args.board_id,
        "soc_id": args.soc_id,
        "partition_abi": args.partition_abi,
        "image_key_id": args.image_key_id,
        "manifest_key_id": args.manifest_key_id,
        "signer_provenance": args.signer_provenance,
        "compression": "crle",
        "chunk_size": CHUNK_SIZE,
        "files": {},
    }
    for name in (source_name, target_name, patch_name, "manifest.cbor", "manifest.sig"):
        path = output / name
        inventory["files"][name] = {"size": path.stat().st_size, "sha256": _sha256(path).hex()}
    inventory_path = output / "inventory.json"
    inventory_path.write_text(json.dumps(inventory, sort_keys=True, indent=2) + "\n")
    checksum_names = sorted([*inventory["files"], inventory_path.name])
    (output / "SHA256SUMS").write_text(
        "".join(f"{_sha256(output / name).hex()}  {name}\n" for name in checksum_names)
    )
    return {"package": str(output), "transfer_id": transfer_id, "patch_size": len(patch)}


def inspect_package(package: Path) -> dict[str, Any]:
    inventory = json.loads((package / "inventory.json").read_text())
    manifest_bytes = (package / "manifest.cbor").read_bytes()
    manifest = decode_manifest(manifest_bytes)
    _validate_manifest(manifest)
    return {"inventory": inventory, "manifest": _manifest_json(manifest)}


def verify_package(package: Path, public_key: Path, image_public_key: Path) -> dict[str, Any]:
    inspected = inspect_package(package)
    inventory = inspected["inventory"]
    expected_files = {
        "source.signed.bin", "target.signed.bin", "patch.newp",
        "manifest.cbor", "manifest.sig",
    }
    if inventory.get("schema_version") != 1 or set(inventory.get("files", {})) != expected_files:
        raise FirmwareError("package inventory schema or file set is invalid")
    for name, declared in inventory.get("files", {}).items():
        path = package / name
        if not path.is_file() or path.stat().st_size != declared["size"] or _sha256(path).hex() != declared["sha256"]:
            raise FirmwareError(f"package inventory mismatch: {name}")
    manifest_bytes = (package / "manifest.cbor").read_bytes()
    signature = (package / "manifest.sig").read_bytes()
    _verify_signature(public_key, manifest_bytes, signature)
    transfer_id = hashlib.sha256(manifest_bytes).hexdigest()
    if inventory["transfer_id"] != transfer_id:
        raise FirmwareError("package transfer ID does not match its canonical manifest")
    manifest = decode_manifest(manifest_bytes)
    role_name = next((name for name, value in ROLE_IDS.items() if value == manifest[3]), None)
    identity = {
        "role": role_name,
        "board_id": manifest[4],
        "soc_id": manifest[5],
        "partition_abi": manifest[8],
        "image_key_id": manifest[16],
        "manifest_key_id": manifest[17],
        "chunk_size": manifest[15],
    }
    if any(inventory.get(key) != value for key, value in identity.items()):
        raise FirmwareError("package inventory identity does not match the signed manifest")
    patch = package / "patch.newp"
    if _sha256(patch) != manifest[13] or patch.stat().st_size != manifest[14]:
        raise FirmwareError("patch identity does not match the signed manifest")
    source, target = package / "source.signed.bin", package / "target.signed.bin"
    source_info = _verify_image_signature(source, image_public_key)
    target_info = _verify_image_signature(target, image_public_key)
    if source_info["sha256"] != manifest[11] or target_info["sha256"] != manifest[12]:
        raise FirmwareError("retained image identity does not match the signed manifest")
    if list(source_info["version"]) != manifest[9] or list(target_info["version"]) != manifest[10]:
        raise FirmwareError("retained image version does not match the signed manifest")
    if target_info["security_counter"] != manifest[18]:
        raise FirmwareError("target security counter does not match the signed manifest")
    with tempfile.TemporaryDirectory() as temporary:
        reconstructed = Path(temporary) / "target.bin"
        _apply_patch(source, patch, reconstructed)
        if _sha256(reconstructed) != target_info["sha256"]:
            raise FirmwareError("offline reconstruction does not equal the retained target")
    return {"verified": True, "transfer_id": transfer_id, "patch_size": patch.stat().st_size}


class FirmwareCreate(WestCommand):
    def __init__(self):
        super().__init__("firmware", "signed delta FOTA packages", "Create, inspect, and verify Meshbus FIRMWARE packages.")

    def do_add_parser(self, parser_adder):
        return _add_firmware_arguments(parser_adder.add_parser(self.name, help=self.help, description=self.description))

    def do_run(self, args, unknown_args):
        return _run_firmware(args, unknown_args, self)


def _add_firmware_arguments(parser):
    subparsers = parser.add_subparsers(dest="subcmd", required=True)
    create = subparsers.add_parser("create", help="create a deterministic signed FIRMWARE package")
    create.add_argument("source", help="retained signed source MCUboot image")
    create.add_argument("target", help="retained signed target MCUboot image")
    create.add_argument("output", nargs="?", help=f"empty output directory (default: {DEFAULT_OUTPUT_DIR})")
    create.add_argument("--role", choices=sorted(ROLE_IDS), required=True)
    create.add_argument("--board-id", required=True)
    create.add_argument("--soc-id", default="nrf54l15")
    create.add_argument("--hardware-min", type=int, default=0)
    create.add_argument("--hardware-max", type=int, default=0xFFFFFFFF)
    create.add_argument("--partition-abi", type=int, default=PARTITION_ABI)
    create.add_argument("--campaign-id", help="16-byte hexadecimal UUID; random when omitted")
    create.add_argument("--image-key-id", type=int, required=True)
    create.add_argument("--image-public-key", required=True, help="trusted MCUboot Ed25519 PEM")
    create.add_argument("--manifest-key-id", type=int, required=True)
    create.add_argument("--security-counter", type=int, required=True)
    signer = create.add_mutually_exclusive_group(required=True)
    signer.add_argument("--manifest-private-key", help="explicit engineering/offline Ed25519 PEM signer")
    signer.add_argument("--manifest-signature", help="imported 64-byte detached signature")
    create.add_argument("--manifest-public-key", help="required to validate an imported signature")
    create.add_argument("--signer-provenance", default="unspecified-external-signer")
    inspect = subparsers.add_parser("inspect", help="inspect a package without a device or private key")
    inspect.add_argument("package")
    inspect.add_argument("--json", action="store_true")
    verify = subparsers.add_parser("verify", help="verify hashes, signature, signed images, and offline reconstruction")
    verify.add_argument("package")
    verify.add_argument("--manifest-public-key", required=True)
    verify.add_argument("--image-public-key", required=True)
    verify.add_argument("--json", action="store_true")
    return parser


def _run_firmware(args, unknown_args, command=None):
    del command
    if unknown_args:
        log.die(f"unexpected arguments: {' '.join(unknown_args)}")
    if args.subcmd == "create" and args.manifest_signature and not args.manifest_public_key:
        log.die("--manifest-public-key is required with --manifest-signature")
    try:
        if args.subcmd == "create":
            result = create_package(args)
        elif args.subcmd == "inspect":
            result = inspect_package(Path(args.package))
        else:
            result = verify_package(
                Path(args.package), Path(args.manifest_public_key), Path(args.image_public_key)
            )
    except (FirmwareError, OSError, ValueError, json.JSONDecodeError) as error:
        log.die(str(error))
    if getattr(args, "json", False):
        print(json.dumps(result, sort_keys=True))
    else:
        for key, value in result.items():
            log.inf(f"{key}: {json.dumps(value, sort_keys=True) if isinstance(value, dict) else value}")
    return 0
