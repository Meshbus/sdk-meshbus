# SPDX-License-Identifier: Apache-2.0
"""Developer product orchestration. Never publishes or changes device policy.

West and the standalone entry point share this parser and implementation.
EDK/DFOTA format operations are delegated to an existing Rust meshbus CLI.
"""
import argparse
import io
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit

from west.commands import WestCommand

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import artifacts as art
import licensing
from meshbus_cli import cli_command
from board_profiles import discover

CLIENTS = {
    "aarch64-apple-darwin", "x86_64-apple-darwin",
    "aarch64-pc-windows-msvc", "x86_64-pc-windows-msvc",
    "aarch64-unknown-linux-gnu", "x86_64-unknown-linux-gnu",
}
MAX_IMAGE = 16 * 1024 * 1024
MAX_HOST_TOOL = 256 * 1024 * 1024
SPDX_DOCUMENTS = {"app.spdx", "build.spdx", "modules-deps.spdx", "zephyr.spdx"}


def targets(boards=None):
    """APP profiles are the sole product inventory; Zephyr validates identities."""
    boards = Path(boards) if boards is not None else Path(__file__).resolve().parents[2] / "apps/meshbus/boards"
    return [{"id": profile["id"], "board": profile["board"]} for profile in discover(boards)]


def product_name(product):
    # Include qualifiers so two APP profiles for one board cannot overwrite each other.
    return product["board"].replace("/", "_")


def run(command, *, cwd=None, capture=False, env=None):
    result = subprocess.run([str(arg) for arg in command], cwd=cwd, env=env,
                            stdout=subprocess.PIPE if capture else None,
                            text=True, check=False)
    if result.returncode:
        # Preserve tool exit codes, including extension/compiler failures.
        raise subprocess.CalledProcessError(result.returncode, command)
    return result.stdout.strip() if capture else None


def cli_json(*arguments, command=None):
    return json.loads(run([*(command or cli_command()), *arguments], capture=True))


def cli_identity(command):
    art.require(len(command) == 1, "meshbus CLI command must name one executable")
    executable = Path(command[0]).resolve(strict=True)
    version = run([executable, "--version"], capture=True)
    art.require(re.fullmatch(r"meshbus [0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?", version),
                "unexpected meshbus CLI version output")
    return {"sha256": art.digest(art.read(executable, MAX_HOST_TOOL)), "version": version}


def yaml_file(path):
    # West already requires PyYAML; no additional packaging dependency.
    import yaml
    try:
        return yaml.safe_load(art.read(path).decode())
    except yaml.YAMLError as error:
        raise ValueError(f"invalid YAML in {path}: {error}") from error


def config(path, prefix="CONFIG_"):
    return {key: value.strip('"') for line in path.read_text(encoding="utf-8").splitlines()
            if line.startswith(prefix) and "=" in line
            for key, value in [line.split("=", 1)]}


def macro(path, key):
    match = re.search(r"^#define " + re.escape(key) + r"\s+([^\n]*)", path.read_text(encoding="utf-8"), re.M)
    art.require(match, f"missing generated macro {key}")
    return match[1].strip().strip('"')


def cache(path, key):
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            name, value = line.split("=", 1)
            if name.split(":")[0] == key:
                return value
    raise ValueError(f"missing cache value {key}")


def git_state(root, include_manifest=False):
    def git(*args):
        result = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True)
        return result.stdout.strip() if result.returncode == 0 else None
    revision = git("rev-parse", "--verify", "HEAD")
    status = git("status", "--porcelain", "--untracked-files=normal")
    state = {"revision": revision, "dirty": not revision or status is None or bool(status)}
    if include_manifest:
        state["manifest_revision"] = git("rev-parse", "--verify", "refs/heads/manifest-rev")
    return state


def provenance(workspace, firmware):
    listing = run(["west", "list", "-f", "{name}\t{abspath}"], cwd=workspace, capture=True)
    projects = {}
    off_manifest = []
    for line in listing.splitlines():
        name, path = line.split("\t", 1)
        if name == "manifest":
            if Path(path).resolve() == Path(firmware).resolve():
                # The unified source repository is west's manifest project;
                # unlike dependencies it has no manifest-rev tracking branch.
                projects["meshbus"] = git_state(path)
            continue
        state = git_state(path, include_manifest=True)
        manifest_revision = state.pop("manifest_revision")
        projects[name] = state
        if state["revision"] != manifest_revision:
            off_manifest.append(name)
    art.require("meshbus" in projects, "SDK missing from west graph")
    return {"firmware": git_state(firmware), "off_manifest": sorted(off_manifest),
            "projects": projects}


def app_build(build):
    """Resolve the Meshbus sysbuild domain or use a direct image directory."""
    build = Path(build)
    if (build / "meshbus/zephyr/.config").is_file():
        return build / "meshbus"
    return build


def context(build):
    build = app_build(build).resolve(strict=True)
    info = yaml_file(build / "build_info.yml")
    conf = config(build / "zephyr/.config")
    board = info["cmake"]["board"]
    target = "/".join(p for p in (board["name"], board.get("qualifiers")) if p)
    art.require(conf["CONFIG_BOARD_TARGET"] == target, "build_info target differs from Kconfig")
    firmware = Path(run(["git", "-C", info["cmake"]["application"]["source-dir"],
                         "rev-parse", "--show-toplevel"], capture=True))
    workspace = Path(run(["west", "topdir"], cwd=firmware, capture=True))
    source = provenance(workspace, firmware)
    header = build / "zephyr/include/generated/zephyr/app_version.h"
    version = macro(header, "APP_VERSION_STRING")
    revision = macro(header, "APP_BUILD_VERSION")
    art.require(version, "empty host version")
    if re.fullmatch(r"[0-9a-fA-F]{7,40}", revision):
        art.require((source["firmware"]["revision"] or "").startswith(revision.lower()),
                    "host build revision differs from current firmware checkout")
    return build, info, conf, target, version, firmware, source


def normalize(value):
    # Same portable version identity as the EDK exporter.
    return re.sub(r"[^a-z0-9._-]+", "-", value.lower()).strip("-.")


def hex_record(address, kind, data):
    record = bytes([len(data), address >> 8, address & 255, kind]) + data
    return ":" + (record + bytes([-sum(record) & 255])).hex().upper() + "\n"


def full_hex(segments):
    records = []
    for base, data in segments:
        previous = None
        offset = 0
        while offset < len(data):
            address = base + offset
            art.require(0 <= address <= 0xffffffff, "HEX address overflow")
            upper = address >> 16
            if upper != previous:
                records.append(hex_record(0, 4, upper.to_bytes(2, "big")))
                previous = upper
            # Do not let a data record wrap across a 64 KiB address boundary.
            chunk = data[offset:offset + min(16, 0x10000 - (address & 0xffff))]
            records.append(hex_record(address & 0xffff, 0, chunk))
            offset += len(chunk)
    return "".join(records) + ":00000001FF\n"


def imgtool():
    workspace = Path(__file__).resolve().parents[3]
    mcuboot = Path(run(["west", "list", "mcuboot", "-f", "{abspath}"],
                       cwd=workspace, capture=True))
    tool = mcuboot / "scripts/imgtool.py"
    art.read(tool, 4 * 1024 * 1024)
    return tool


def json_digest(value):
    data = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return art.digest(data)


def public_source_location(value):
    if value in {"NONE", "NOASSERTION"}:
        return None
    location = value.removeprefix("git+")
    # Git's scp-style spelling has no URI scheme. Preserve its source identity
    # as an SSH URL, without accepting personal usernames or embedded passwords.
    if location.startswith("git@") and ":" in location:
        authority, path = location.split(":", 1)
        location = f"ssh://{authority}/{path}"
        value = "git+" + location
    try:
        parsed = urlsplit(location)
        valid = (parsed.scheme in {"https", "http", "git", "ssh"} and parsed.hostname
                 and parsed.password is None
                 and (parsed.username is None or
                      (parsed.scheme == "ssh" and parsed.username == "git"))
                 and not parsed.query and not parsed.fragment
                 and not any(c.isspace() or c == "\\" for c in value))
    except ValueError:
        valid = False
    art.require(valid, "SPDX source location must be a credential-free repository or download URL")
    return value


def public_spdx(raw_documents, identity):
    components = {}
    created = set()

    def packages(data):
        for block in re.split(r"(?=^##### Package:)", data.decode("utf-8"), flags=re.M):
            fields = {}
            for line in block.splitlines():
                if ": " in line:
                    key, value = line.split(": ", 1)
                    fields.setdefault(key, []).append(value)
            if "PackageName" in fields:
                yield fields

    def merge(entry, fields, include_licenses=True, include_versions=True):
        if include_licenses:
            entry["declared"].update(fields.get("PackageLicenseDeclared", []))
            entry["licenses"].update(fields.get("PackageLicenseConcluded", []))
        entry["suppliers"].update(fields.get("PackageSupplier", []))
        if include_versions:
            entry["versions"].update(fields.get("PackageVersion", []))
        entry["locations"].update(location for value in fields.get("PackageDownloadLocation", [])
                                  if (location := public_source_location(value)) is not None)
        entry["external_refs"].update(fields.get("ExternalRef", []))

    for (domain, name), data in raw_documents.items():
        text = data.decode("utf-8")
        created.update(re.findall(r"^Created: (\S+)$", text, re.M))
        if name not in {"app.spdx", "zephyr.spdx"}:
            continue
        for fields in packages(data):
            if "FileName" not in fields:
                continue
            package = fields["PackageName"][0]
            if name == "app.spdx" and domain == "app":
                continue
            if name == "app.spdx" and domain == "mcuboot":
                package = "mcuboot-sources"
            component = package.removesuffix("-sources")
            public_name = "meshbus-sdk" if component == "meshbus" else component
            entry = components.setdefault(public_name, {
                "declared": set(), "external_refs": set(), "licenses": set(),
                "locations": set(), "suppliers": set(), "versions": set(),
            })
            merge(entry, fields)

    for (_, name), data in raw_documents.items():
        if name != "modules-deps.spdx":
            continue
        for fields in packages(data):
            package = fields["PackageName"][0]
            if not package.endswith("-deps"):
                continue
            component = package.removesuffix("-deps")
            public_name = "meshbus-sdk" if component == "meshbus" else component
            if public_name in components:
                entry = components[public_name]
                locations = {location for value in fields.get("PackageDownloadLocation", [])
                             if (location := public_source_location(value)) is not None}
                # Module metadata may report a release version while the source
                # inventory reports the checkout SHA for the exact same URL.
                same_checkout = (len(locations) == 1 and locations == entry["locations"]
                                 and any(re.search(r"@[0-9a-f]{40}(?:-off)?(?:-dirty)?$", location)
                                         and location.rsplit("@", 1)[1] in entry["versions"]
                                         for location in locations))
                merge(entry, fields, include_licenses=False, include_versions=not same_checkout)

    art.require(created, "SPDX documents have no creation time")
    art.require(components, "SPDX source inventory contains no compiled dependencies")

    def expression(values):
        values = set(values)
        if not values or values & {"NONE", "NOASSERTION"}:
            return "NOASSERTION"
        return next(iter(values)) if len(values) == 1 else " AND ".join(f"({v})" for v in sorted(values))

    package_ids = {}
    for name in components:
        identifier = "SPDXRef-Component-" + re.sub(r"[^A-Za-z0-9.-]+", "-", name).strip("-")
        art.require(identifier not in package_ids.values(), f"duplicate public SPDX identifier: {identifier}")
        package_ids[name] = identifier

    version = identity["version"]
    target = identity["target"]
    product_id = "SPDXRef-meshbus-firmware"
    lines = [
        "SPDXVersion: SPDX-2.3", "DataLicense: CC0-1.0", "SPDXID: SPDXRef-DOCUMENT",
        f"DocumentName: meshbus-firmware-{normalize(version)}",
        f"DocumentNamespace: https://spdx.org/spdxdocs/meshbus-{identity['seed']}/public",
        "Creator: Organization: Meshbus", "Creator: Tool: Meshbus west release",
        f"Created: {min(created)}", "", f"Relationship: SPDXRef-DOCUMENT DESCRIBES {product_id}",
        *(f"Relationship: {product_id} DEPENDS_ON {package_ids[name]}" for name in sorted(components)),
        "", "##### Package: meshbus-firmware", "", "PackageName: meshbus-firmware",
        f"SPDXID: {product_id}", f"PackageVersion: {version}",
        "PackageSupplier: Organization: Meshbus", "PackageDownloadLocation: NOASSERTION",
        "FilesAnalyzed: false", f"PackageChecksum: SHA256: {identity['full_bin_sha256']}",
        "PackageLicenseConcluded: NOASSERTION", "PackageLicenseDeclared: NOASSERTION",
        "PackageCopyrightText: NOASSERTION", "PrimaryPackagePurpose: FIRMWARE",
        f"PackageComment: <text>Firmware target {target}; source identities are recorded in component packages.</text>",
    ]
    for name in sorted(components):
        entry = components[name]
        versions = set(entry["versions"])
        locations = set(entry["locations"])
        suppliers = set(entry["suppliers"])
        art.require(len(versions) <= 1, f"multiple revisions for SPDX component: {name}")
        art.require(len(locations) <= 1, f"multiple locations for SPDX component: {name}")
        lines += ["", f"##### Package: {name}", "", f"PackageName: {name}",
                  f"SPDXID: {package_ids[name]}"]
        if versions:
            lines.append(f"PackageVersion: {next(iter(versions))}")
        lines += [f"PackageSupplier: {next(iter(suppliers)) if len(suppliers) == 1 else 'NOASSERTION'}",
                  f"PackageDownloadLocation: {next(iter(locations)) if locations else 'NOASSERTION'}"]
        lines += [f"ExternalRef: {value}" for value in sorted(entry["external_refs"])]
        lines += ["FilesAnalyzed: false", f"PackageLicenseConcluded: {expression(entry['licenses'])}",
                  f"PackageLicenseDeclared: {expression(entry['declared'])}",
                  "PackageCopyrightText: NOASSERTION", "PrimaryPackagePurpose: LIBRARY"]
    data = ("\n".join(lines) + "\n").encode()
    art.require(b"FileName:" not in data and b"FileChecksum:" not in data,
                "public SPDX must not expose source-file inventory")
    return data, sorted(components)


def generate_spdx(build, sysbuild, output, identity, source_root, required, domains=None):
    domains = domains if domains is not None else {"app": build, "mcuboot": sysbuild / "mcuboot"}
    enabled = {name: config(path / "zephyr/.config").get("CONFIG_BUILD_OUTPUT_META") == "y"
               for name, path in domains.items()}
    art.require(not any(enabled.values()) or all(enabled.values()),
                "all packaged images must enable CONFIG_BUILD_OUTPUT_META together")
    if not all(enabled.values()):
        art.require(not required, "production package requires SPDX metadata for all packaged images")
        return {"status": "not-generated"}

    seed = json_digest(identity)
    raw_root = sysbuild / "spdx-private" / seed
    art.require(not raw_root.exists() or (raw_root.is_dir() and not raw_root.is_symlink()),
                "private SPDX retention path is not a regular directory")
    raw_documents = {}
    for name, directory in domains.items():
        destination = raw_root / name
        art.require(not destination.exists() or (destination.is_dir() and not destination.is_symlink()),
                    f"private {name} SPDX path is not a regular directory")
        namespace = f"https://spdx.org/spdxdocs/meshbus-{seed}/{name}"
        run(["west", "spdx", "--build-dir", directory, "--spdx-dir", destination,
             "--namespace-prefix", namespace, "--spdx-version", "2.3"], cwd=source_root)
        paths = art.files(destination)
        art.require(len(paths) == len(SPDX_DOCUMENTS) and
                    {path.name for path in paths} == SPDX_DOCUMENTS,
                    f"unexpected {name} SPDX document set")
        for path in paths:
            data = art.read(path, MAX_IMAGE)
            art.require(data.startswith(b"SPDXVersion: SPDX-2.3\n"),
                        f"invalid SPDX 2.3 document: {path.name}")
            for private_path in (Path.home(), source_root, sysbuild):
                for spelling in {str(private_path), private_path.as_posix()}:
                    art.require(len(spelling) < 2 or spelling.encode() not in data,
                                f"SPDX document embeds a developer path: {path.name}")
            raw_documents[(name, path.name)] = data
    art.checksums(raw_root)
    public, components = public_spdx(raw_documents, {**identity, "seed": seed})
    public_path = output / "SBOM.spdx"
    public_path.write_bytes(public)
    for private_path in (Path.home(), source_root, sysbuild):
        for spelling in {str(private_path), private_path.as_posix()}:
            art.require(len(spelling) < 2 or spelling.encode() not in public,
                        "public SPDX embeds a developer path")
    return {"components": components,
            "documents": [{"file": public_path.relative_to(output).as_posix(),
                           "sha256": art.digest(public), "size": len(public)}],
            "format": "SPDX-2.3", "namespace_seed": seed,
            "private_retention": {"documents": len(raw_documents),
                                  "path": raw_root.relative_to(sysbuild).as_posix()},
            "status": "generated"}


def verify_native_signature(build_dir, public_key):
    """Verify Zephyr's signed APP against the public key actually built into MCUboot."""
    build = app_build(build_dir).resolve(strict=True)
    boot = build.parent / "mcuboot/zephyr"
    public_key = Path(public_key).resolve(strict=True)
    tool = imgtool()
    app_runner = yaml_file(build / "zephyr/runners.yaml")["config"]
    app_name = app_runner["bin_file"]
    art.require(Path(app_name).name == app_name, "invalid application binary name")
    boot_name = yaml_file(boot / "runners.yaml")["config"]["bin_file"]
    art.require(Path(boot_name).name == boot_name, "invalid MCUboot binary name")
    boot_data = art.read(boot / boot_name, MAX_IMAGE)
    with tempfile.TemporaryDirectory() as temporary:
        raw = Path(temporary) / "public.raw"
        generated = Path(temporary) / "autogen-pubkey.c"
        run([sys.executable, tool, "keyinfo", "-k", public_key, "--require", "public"])
        run([sys.executable, tool, "getpub", "-k", public_key, "-e", "raw", "-o", raw])
        run([sys.executable, tool, "getpub", "-k", public_key, "-o", generated])
        public_data = art.read(raw, 4096)
        art.require(art.read(generated) == art.read(boot / "autogen-pubkey.c") and
                    public_data in boot_data,
                    "MCUboot verification key differs from the selected public key")
    signed = build / "zephyr" / app_name
    run([sys.executable, tool, "verify", "-k", public_key, signed])
    return {"method": "zephyr-imgtool", "key_sha256": art.digest(public_data),
            "app_sha256": art.digest(art.read(signed, MAX_IMAGE)),
            "mcuboot_sha256": art.digest(boot_data), "verified": True}


def image_private_key(key_file, build_root):
    """Validate a caller-owned PEM path without reading keys from the environment."""
    art.require(key_file, "build requires --image-signing-key")
    art.require("-----BEGIN" not in str(key_file) and "\n" not in str(key_file),
                "--image-signing-key must be a file path, not PEM contents")
    private_key = Path(key_file).resolve(strict=True)
    firmware_root = Path(__file__).resolve().parents[3]
    art.require(not private_key.is_relative_to(firmware_root) and
                not private_key.is_relative_to(build_root),
                "Image private key must remain outside source and build trees")
    if os.name == "posix":
        art.require(private_key.stat().st_mode & 0o077 == 0,
                    "Image private key must not be group- or world-accessible")
    art.read(private_key, 64 * 1024)
    return private_key


def image_format(conf):
    if conf.get("CONFIG_BOOTLOADER_MCUBOOT") == "y":
        return "mcuboot"
    art.require(conf.get("CONFIG_BUILD_OUTPUT_UF2") == "y", "unsupported firmware image format")
    return "uf2"


def mcuboot_authentication(build, app_conf):
    """Determine authentication from generated configurations, never filenames."""
    boot = config(build.parent / "mcuboot/zephyr/.config")
    unsigned = boot.get("CONFIG_BOOT_SIGNATURE_TYPE_NONE") == "y"
    ed25519 = boot.get("CONFIG_BOOT_SIGNATURE_TYPE_ED25519") == "y"
    art.require(unsigned != ed25519, "MCUboot must select hash-only or Ed25519 authentication")
    art.require(boot.get("CONFIG_BOOT_VALIDATE_SLOT0") == "y",
                "MCUboot must validate the primary image on every boot")
    art.require((app_conf.get("CONFIG_MCUBOOT_GENERATE_UNSIGNED_IMAGE") == "y") == unsigned,
                "APP and MCUboot authentication modes differ")
    art.require(bool(app_conf.get("CONFIG_MCUBOOT_SIGNATURE_KEY_FILE")) == ed25519,
                "APP signing key configuration differs from authentication mode")
    art.require(bool(boot.get("CONFIG_BOOT_SIGNATURE_KEY_FILE")) == ed25519,
                "MCUboot key configuration differs from authentication mode")
    return "none" if unsigned else "ed25519"


def verify_mcuboot_image(data, authentication):
    """Bound MCUboot headers/TLVs and verify the image hash and selected mode.

    Ed25519 authorization is verified separately against the bootloader's key.
    A hash-only imgtool output can also be called zephyr.signed.bin.
    """
    art.require(authentication in ("none", "ed25519"), "unknown image authentication mode")
    art.require(len(data) >= 32, "truncated MCUboot image header")
    magic, _, header_size, protected_size, image_size = struct.unpack_from("<IIHHI", data)
    art.require(magic == 0x96F3B83D and header_size >= 32 and image_size > 0,
                "invalid MCUboot image header")
    payload_end = header_size + image_size
    hash_end = payload_end + protected_size
    art.require(hash_end + 4 <= len(data), "MCUboot image exceeds available bytes")

    def tlvs(offset, expected_magic, expected_size=None):
        art.require(offset + 4 <= len(data), "truncated MCUboot TLV header")
        magic, size = struct.unpack_from("<HH", data, offset)
        art.require(magic == expected_magic and size >= 4 and offset + size <= len(data),
                    "invalid MCUboot TLV area")
        art.require(expected_size is None or size == expected_size, "protected TLV size mismatch")
        end, cursor, values = offset + size, offset + 4, {}
        while cursor < end:
            art.require(cursor + 4 <= end, "truncated MCUboot TLV entry")
            kind, length = struct.unpack_from("<HH", data, cursor)
            cursor += 4
            art.require(length > 0 and cursor + length <= end, "invalid MCUboot TLV length")
            art.require(kind not in values, "duplicate MCUboot TLV entry")
            values[kind] = data[cursor:cursor + length]
            cursor += length
        return values

    protected = tlvs(payload_end, 0x6908, protected_size) if protected_size else {}
    values = tlvs(hash_end, 0x6907)
    authentication_types = {0x01, 0x02, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25}
    art.require(not (protected.keys() & (authentication_types | {0x10})),
                "authentication TLV is in the protected area")
    art.require(values.get(0x10) == bytes.fromhex(art.digest(data[:hash_end])),
                "MCUboot image hash is missing or invalid")
    present = values.keys() & authentication_types
    if authentication == "none":
        art.require(not present, "hash-only image contains authentication TLVs")
    else:
        art.require(present in ({0x01, 0x24}, {0x02, 0x24}) and len(values[0x24]) == 64,
                    "image lacks the selected Ed25519 authentication TLVs")


def record_authentication(record):
    """Validate the record's explicit authentication mode and signing evidence."""
    art.require("authentication" in record, "missing authentication metadata")
    mode = record["authentication"]
    signing = record.get("signing")
    art.require(mode in ("none", "ed25519"), "invalid authentication metadata")
    if mode == "none":
        art.require("signing" not in record, "unsigned record contains signing metadata")
    else:
        image = next(i for i in record["images"] if i["domain"] == "app")
        art.require(isinstance(signing, dict) and signing.get("verified") is True and
                    signing.get("app_sha256") == image["sha256"],
                    "signing metadata differs from authenticated application")
    return mode


def verify_uf2(data, binary, address, partition_end, family_id, hex_data=None):
    """Validate native UF2 against BIN, or sparse HEX plus its BIN projection."""
    art.require(binary and data and len(data) % 512 == 0, "invalid UF2 file length")
    hex_image = None
    block_addresses = list(range(address, address + len(binary), 256))
    if hex_data is not None:
        from intelhex import IntelHex, IntelHexError
        try:
            hex_image = IntelHex(io.StringIO(hex_data.decode("ascii")))
        except (UnicodeError, IntelHexError) as error:
            raise ValueError("invalid application HEX") from error
        segments = hex_image.segments()
        art.require(segments and all(address <= start < end <= address + len(binary) for start, end in segments),
                    "HEX writes outside application image")
        hex_image.padding = 255
        art.require(hex_image.tobinarray(start=address, size=len(binary)).tobytes() == binary,
                    "HEX payload differs from application BIN")
        block_addresses = sorted({block for start, end in segments
                                  for block in range(start & ~255, (end + 255) & ~255, 256)})
        # Zephyr's HEX-to-UF2 converter initializes holes inside each emitted
        # block to zero; objcopy's BIN projection uses 0xff for those same holes.
        hex_image.padding = 0
    count = len(block_addresses)
    art.require(len(data) == count * 512, "UF2 block count differs from application")
    art.require(address >= 0 and all(address <= block and block + 256 <= partition_end
                                    for block in block_addresses) and partition_end <= 0x100000000,
                "UF2 writes outside application partition")
    seen = set()
    for offset in range(0, len(data), 512):
        block = data[offset:offset + 512]
        magic0, magic1, flags, target, size, number, total, family = struct.unpack_from("<8I", block)
        art.require((magic0, magic1, struct.unpack_from("<I", block, 508)[0]) ==
                    (0x0A324655, 0x9E5D5157, 0x0AB16F30), "invalid UF2 magic")
        art.require(flags == 0x2000 and family == family_id and family != 0,
                    "UF2 flags or family ID differ from configured target")
        art.require(size == 256 and total == count and number < count and number not in seen,
                    "invalid or duplicate UF2 block")
        art.require(target == block_addresses[number], "UF2 address differs from application")
        expected = (hex_image.tobinarray(start=target, size=256).tobytes() if hex_image is not None
                    else binary[number * 256:(number + 1) * 256].ljust(256, b"\0"))
        art.require(block[32:288] == expected, "UF2 payload differs from application")
        art.require(not any(block[288:508]), "unexpected UF2 extension data")
        seen.add(number)
    result = {"file": "app.uf2", "sha256": art.digest(data), "size": len(data),
              "address": address, "family_id": family_id, "verified": True}
    if hex_data is not None:
        result["hex_sha256"] = art.digest(hex_data)
    return result


def firmware(build_dir, output, development, image_public_key=None, app_sdk=None):
    build, info, conf, target, version, source_root, source = context(build_dir)
    product = next((t for t in targets(source_root / "apps/meshbus/boards") if t["board"] == target), None)
    art.require(product, "not a qualified product target")
    clean = all(not p["dirty"] for p in [source["firmware"], *source["projects"].values()])
    art.require(development or clean, "candidate requires clean committed source; use --development")
    art.require(development or not source["off_manifest"],
                "candidate checkouts differ from the resolved manifest: " +
                ", ".join(source["off_manifest"]))
    sysbuild = build.parent
    format_name = image_format(conf)
    soc = conf.get("CONFIG_SOC", "")
    art.require(soc and soc in target.split("/")[1:],
                "generated CONFIG_SOC must match the qualified product target")
    mcuboot = format_name == "mcuboot"
    art.require((sysbuild / "domains.yaml").is_file(), "product requires sysbuild output")
    if mcuboot:
        art.require((sysbuild / "mcuboot/zephyr/.config").is_file(), "product requires MCUboot output")
    else:
        art.require(image_public_key is None, "UF2 application does not use an MCUboot public key")
    authentication = mcuboot_authentication(build, conf) if mcuboot else "none"
    if product["id"] == "mesh_probe_r2":
        art.require(mcuboot, "Mesh Probe R2 requires MCUboot image layout")
        boot_conf = config(sysbuild / "mcuboot/zephyr/.config")
        art.require(boot_conf.get("CONFIG_MCUBOOT_SERIAL") == "y" and
                    boot_conf.get("CONFIG_BOOT_SERIAL_UART") == "y" and
                    boot_conf.get("CONFIG_MCUBOOT_BOOT_BLUETOOTH") != "y" and
                    boot_conf.get("CONFIG_BT") != "y",
                    "Mesh Probe R2 MCUboot recovery must be UART-only")
    public_key = Path(image_public_key) if image_public_key else sysbuild / "image-public.pem"
    if mcuboot and authentication == "none":
        art.require(image_public_key is None and not public_key.exists(),
                    "unsigned build must not supply an image public key")
    signing = verify_native_signature(build, public_key) if authentication == "ed25519" else None
    domains = {"mcuboot": sysbuild / "mcuboot", "app": build} if mcuboot else {"app": build}
    llext = conf.get("CONFIG_MBS_LLEXT") == "y"
    # Fail before writing a partial product if its required EDK tool is absent.
    edk_command = cli_command(require_explicit=not development) if llext else None
    edk_tool = cli_identity(edk_command) if edk_command else None
    part = output / "firmware" / f"{normalize(version)}-{product_name(product)}"
    art.clean_destination(part)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        segments, images, inputs = [], [], {}
        uf2 = None
        if signing:
            inputs[public_key] = art.digest(art.read(public_key))
            shutil.copyfile(public_key, root / "image-public.pem")
        for domain, directory in domains.items():
            settings = config(directory / "zephyr/.config")
            def number(key):
                return int(settings[key], 0)
            runner = yaml_file(directory / "zephyr/runners.yaml")["config"]
            name = runner["bin_file"]
            art.require(Path(name).name == name, "invalid image binary name")
            selected_path = directory / "zephyr" / name
            data = art.read(selected_path, MAX_IMAGE)
            if mcuboot and domain == "app":
                verify_mcuboot_image(data, authentication)
            for path in (selected_path, directory / "zephyr/.config",
                         directory / "zephyr/zephyr.dts", directory / "zephyr/runners.yaml"):
                inputs[path] = art.digest(art.read(path))
            base = number("CONFIG_FLASH_BASE_ADDRESS") + number("CONFIG_FLASH_LOAD_OFFSET")
            address = base + (0 if mcuboot and domain == "app" else number("CONFIG_ROM_START_OFFSET"))
            size = number("CONFIG_FLASH_LOAD_SIZE")
            art.require(base >= 0 and size > 0 and address >= base and data and
                        address + len(data) <= base + size, f"{domain} binary exceeds partition")
            if not mcuboot:
                art.require(runner.get("uf2_file"), "UF2 build requires BIN and UF2 outputs")
                uf2_path = directory / "zephyr" / runner["uf2_file"]
                uf2_data = art.read(uf2_path, MAX_IMAGE * 2)
                hex_data = art.read(directory / "zephyr" / runner["hex_file"]) if runner.get("hex_file") else None
                uf2 = verify_uf2(uf2_data, data, address, base + size,
                                 int(settings["CONFIG_BUILD_OUTPUT_UF2_FAMILY_ID"], 0), hex_data)
                inputs[uf2_path] = art.digest(uf2_data)
                (root / "app.uf2").write_bytes(uf2_data)
            filename = f"{domain}.bin"
            (root / filename).write_bytes(data)
            if runner.get("hex_file"):
                path = directory / "zephyr" / runner["hex_file"]
                if path.is_file():
                    inputs[path] = art.digest(art.read(path))
                    shutil.copyfile(path, root / f"{domain}.hex")
            images.append({"domain": domain, "file": filename, "address": address,
                           "size": len(data), "sha256": art.digest(data),
                           "partition_address": base, "partition_size": size,
                           "config_sha256": art.digest(art.read(directory / "zephyr/.config")),
                           "devicetree_sha256": art.digest(art.read(directory / "zephyr/zephyr.dts"))})
            segments.append((address, data))
        segments.sort()
        art.require(all(left[0] + len(left[1]) <= right[0] for left, right in zip(segments, segments[1:])),
                    "overlapping boot/application images")
        first = segments[0][0]
        span = segments[-1][0] + len(segments[-1][1]) - first
        art.require(span <= MAX_IMAGE, "full binary span exceeds bound")
        full = bytearray(b"\xff" * span)
        for address, data in segments:
            full[address - first:address - first + len(data)] = data
        if mcuboot:
            (root / "full.bin").write_bytes(full)
            (root / "full.hex").write_text(full_hex(segments), encoding="ascii", newline="\n")
        sbom = generate_spdx(
            build, sysbuild, root,
            {"full_bin_sha256": art.digest(full), "images": images, "provenance": source,
             "target": target, "version": version},
            source_root, not development, domains=domains)
        notices = licensing.firmware(source_root, domains, sbom, sysbuild, root,
                                     {name: config(path / "zephyr/.config") for name, path in domains.items()}, cache)
        record = {"schema": 1, "kind": "firmware", "id": product["id"], "version": version,
                  "format": format_name,
                  "authentication": authentication,
                  "target": target, "soc": soc, "publishable": False, "engineering": development,
                  "capabilities": {"firmware_endpoint": conf.get("CONFIG_MBS_FIRMWARE") == "y", "llext": llext},
                  "provenance": source,
                  "build": {"sysbuild": True, "command": ["west", "build", "--sysbuild", "-b", target, "meshbus/apps/meshbus"],
                            "toolchain": info["cmake"]["toolchain"]["name"],
                            "compiler_version": run([cache(build / "CMakeCache.txt", "CMAKE_C_COMPILER"), "-dumpfullversion"], capture=True),
                            "west_version": run(["west", "--version"], capture=True)},
                  "images": images,
                  "sbom": sbom,
                  "license_materials": notices,
                  "validation": {"build": "passed", "hardware": "not-run", "production_release": "not-qualified"}}
        if mcuboot:
            record["full_bin"] = {"file": "full.bin", "address": first, "fill": 255}
        if uf2:
            record["uf2"] = uf2
        if edk_tool:
            record["edk_tool"] = edk_tool
        if signing:
            record["signing"] = signing
        art.write_json(root / "flash-map.json", record)
        shutil.copyfile(source_root / "LICENSE", root / "LICENSE.txt")
        (root / "NOTICE.txt").write_text(
            ("Meshbus engineering firmware candidate. Not production-qualified. full.bin starts at the address in "
            "flash-map.json; it is not an application-slot image or a DFOTA source. Preserve storage; do not infer "
            "erase-all authorization. See licenses/ and license-materials.json for collected declarations.\n") if mcuboot else
            ("Meshbus UF2 application candidate. Not production-qualified. Install app.uf2 using the existing "
             "compatible UF2 bootloader and SoftDevice. This archive contains only the application; it does not "
             "include or replace the bootloader, SoftDevice or settings. UF2 payload verification is not a "
             "cryptographic signature. See licenses/ and license-materials.json for collected declarations.\n"), encoding="utf-8")
        art.checksums(root)
        art.pack(root, part / f"meshbus-{normalize(version)}-{product_name(product)}-firmware.tar.gz", "firmware")
        shutil.copyfile(root / "flash-map.json", part / "flash-map.json")
        shutil.copyfile(root / "app.bin", part / "app.bin")
        if uf2:
            shutil.copyfile(root / "app.uf2", part / "app.uf2")
            if (root / "app.hex").is_file():
                shutil.copyfile(root / "app.hex", part / "app.hex")
    if edk_command:
        run([*edk_command, "edk", "-d", build, "-o", part, *(["--development"] if development else [])])
        art.require(all(art.digest(art.read(path)) == sha for path, sha in inputs.items()),
                    "EDK export changed the firmware build. Run 'west release build' into a fresh output "
                    "directory; this partial package has no success record.")
        archives = list(part.glob("*-edk.tar.xz"))
        art.require(len(archives) == 1, "LLEXT product must produce exactly one EDK")
        art.verify_sidecar(archives[0])
        manifest = cli_json("edk", "verify", archives[0], command=edk_command)["manifest"]
        art.require(manifest["target"] == target and manifest["host"]["version"] == version,
                    "EDK identity differs from product")
        # The same producer is used by standalone local imports and releases.
        source_command = [*edk_command, "app", "source", "--output", part / "release-source.json",
                          "--edk", archives[0], "--toolchain", info["cmake"]["toolchain"]["path"]]
        if app_sdk:
            source_command += ["--sdk", Path(app_sdk).resolve(strict=True)]
        run(source_command)
    art.write_json(part / "release-part.json", record)
    art.checksums(part)
    return part


def build_products(args):
    workspace = args.workspace.resolve(strict=True)
    art.require((workspace / "meshbus/west.yml").is_file(), "workspace must contain meshbus/west.yml")
    application = workspace / "meshbus/apps/meshbus"
    profile = "dev" if args.development else "prod"
    all_targets = targets(application / "boards")
    art.require(all(any(t["id"] == requested or t["board"] == requested for t in all_targets)
                    for requested in args.target), "unknown target")
    selected_targets = [target for target in all_targets
                        if not args.target or target["id"] in args.target or target["board"] in args.target]
    image_key_arg = getattr(args, "image_signing_key", None)
    version = art.digest(art.read(application / "VERSION"))[:12]
    image_key = image_private_key(image_key_arg, args.build_root.resolve()) if image_key_arg else None
    results, failures = [], []
    for target in selected_targets:
        mode = "ed25519" if image_key else "unsigned"
        directory = args.build_root.resolve() / version / profile / mode / product_name(target)
        stage = "configure"
        print(f"west release: building {target['board']} ({profile})", flush=True)
        try:
            command = ["west", "build", "-p", "always", "--sysbuild", "-b", target["board"],
                       application, "-d", directory]
            definitions = ["-DCONFIG_BUILD_OUTPUT_META=y", "-Dmcuboot_CONFIG_BUILD_OUTPUT_META=y",
                           f"-Dmeshbus_EXTRA_CONF_FILE={application / f'prj.{profile}.conf'}"]
            if image_key:
                definitions += ["-DSB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519=y",
                                f'-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="{image_key}"']
            run([*command, "--cmake-only", "--", *definitions], cwd=workspace)
            app_conf = config(app_build(directory) / "zephyr/.config")
            mcuboot = image_format(app_conf) == "mcuboot"
            if mcuboot:
                authentication = mcuboot_authentication(app_build(directory), app_conf)
                art.require(authentication == ("ed25519" if image_key else "none"),
                            "configured authentication differs from requested signing mode")
            # Configuration selects the image format; build the same configured tree.
            stage = "build"
            run(["west", "build", "-d", directory], cwd=workspace)
            directory.mkdir(parents=True, exist_ok=True)
            if mcuboot and image_key:
                stage = "public-key export"
                run([sys.executable, imgtool(), "getpub", "-k", image_key,
                     "-e", "pem", "-o", directory / "image-public.pem"])
            stage = "package"
            firmware(directory, args.output, args.development, app_sdk=getattr(args, "app_sdk", None))
        except (subprocess.CalledProcessError, OSError, ValueError, RuntimeError, KeyError, StopIteration) as error:
            detail = (f"external tool exited with {error.returncode}"
                      if isinstance(error, subprocess.CalledProcessError) else str(error))
            results.append(f"FAIL {target['board']} [{stage}]: {detail}")
            failures.append(error)
            print(f"west release: {results[-1]}", file=sys.stderr, flush=True)
        else:
            results.append(f"PASS {target['board']}")
    print(f"west release: {len(results) - len(failures)} succeeded, {len(failures)} failed", flush=True)
    for result in results:
        print(f"  {result}", flush=True)
    print(f"Product output: {args.output.resolve()}", flush=True)
    if failures:
        # Preserve the existing error/exit-code contract, after every target
        # has had a chance to produce its independent release part.
        raise failures[0]


def verify_cli_paths(binary, paths):
    """Reject embedded build directories without matching longer filenames."""
    for path in paths:
        for spelling in (str(path), path.as_posix()):
            pattern = re.escape(spelling.encode()) + rb"(?=[/\\\x00]|$)"
            art.require(len(spelling) < 2 or re.search(pattern, binary) is None,
                        "CLI embeds a developer path; check Rust path remapping")


def client_profile(args):
    profile = getattr(args, "profile", "release")
    art.require(profile == "release" or (profile == "ci" and getattr(args, "development", False)),
                "CLI candidates require the release profile")
    return profile


def client(args):
    profile = client_profile(args)
    workspace = args.workspace.resolve(strict=True)
    source_root = workspace / "meshbus"
    crate = source_root / "scripts/meshbus"
    cargo = os.environ.get("CARGO", "cargo")
    env = os.environ.copy()
    if env.get("MESHBUS_PROTO_ROOT"):
        # Normalize before invoking Cargo so build.rs and packaging use one root.
        env["MESHBUS_PROTO_ROOT"] = str((workspace / env["MESHBUS_PROTO_ROOT"]).resolve(strict=True))
    flags = (env["CARGO_ENCODED_RUSTFLAGS"].split("\x1f") if env.get("CARGO_ENCODED_RUSTFLAGS")
             else shlex.split(env.get("RUSTFLAGS", "")))
    flags += [f"--remap-path-prefix={Path.home()}=/build-home", f"--remap-path-prefix={workspace}=/meshbus-firmware"]
    env["CARGO_ENCODED_RUSTFLAGS"] = "\x1f".join(flags)
    host = next(line.removeprefix("host: ") for line in run(["rustc", "-vV"], capture=True, env=env).splitlines()
                if line.startswith("host: "))
    target = args.target or host
    art.require(target in CLIENTS, "unsupported CLI archive target")
    source_snapshot = getattr(args, "source_snapshot", None)
    if source_snapshot:
        import cli_source
        source, proto = cli_source.verify(source_snapshot, source_root, workspace)
        art.require(not env.get("MESHBUS_PROTO_ROOT") or Path(env["MESHBUS_PROTO_ROOT"]) == proto,
                    "CLI schema override differs from source snapshot")
        env["MESHBUS_PROTO_ROOT"] = str(proto)
    else:
        source = provenance(workspace, source_root)
    development = getattr(args, "development", False)
    art.require(development or all(not state["dirty"] for state in
                [source["firmware"], *source["projects"].values()]),
                "candidate requires clean committed source; use --development")
    art.require(development or not source["off_manifest"], "candidate dependencies differ from manifest-rev")
    part = args.output / "cli" / target
    art.require(not part.exists() or not any(part.iterdir()), f"output directory is not empty: {part}")
    target_dir = args.cargo_target_dir or Path(env.get("CARGO_TARGET_DIR", workspace / "build-meshbus-cli"))
    command = [cargo, "build", "--locked", "--profile", profile, "--manifest-path", crate / "Cargo.toml",
               "--target-dir", target_dir.resolve(), "--message-format=json-render-diagnostics"]
    command += ["--target", target]
    messages = [json.loads(line) for line in run(command, cwd=workspace, capture=True, env=env).splitlines() if line]
    metadata = json.loads(run([cargo, "metadata", "--offline", "--locked", "--format-version", "1",
                               "--filter-platform", target,
                               "--manifest-path", crate / "Cargo.toml"], cwd=workspace, capture=True, env=env))
    package = next(p for p in metadata["packages"] if Path(p["manifest_path"]).resolve() == crate / "Cargo.toml")
    binaries = [Path(m["executable"]) for m in messages if m.get("reason") == "compiler-artifact"
                and m.get("package_id") == package["id"] and m.get("executable")]
    scripts = [m for m in messages if m.get("reason") == "build-script-executed"
               and m.get("package_id") == package["id"]]
    art.require(len(binaries) == 1 and len(scripts) == 1, "Cargo did not identify the CLI and its descriptor build output")
    art.require(dict(scripts[0]["env"]).get("MESHBUS_BUILD_TARGET") == target,
                "Cargo compiled a different target; check the configured --target and cross toolchain")
    descriptor = Path(scripts[0]["out_dir"]) / "meshbus.pb"
    runtime_packages, build_tools = licensing.cargo_packages(metadata, package["id"])
    # Use the same explicit schema root as build.rs, including user overrides.
    proto_root = Path(env["MESHBUS_PROTO_ROOT"]) if env.get("MESHBUS_PROTO_ROOT") else Path(
        run(["west", "list", "meshbus-protobufs", "-f", "{abspath}"], cwd=workspace, capture=True))
    if not proto_root.is_absolute():
        proto_root = workspace / proto_root
    binary = art.read(binaries[0])
    verify_cli_paths(binary, (workspace, Path.home()))
    art.clean_destination(part)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        executable = "meshbus.exe" if "windows" in target else "meshbus"
        (root / executable).write_bytes(binary)
        shutil.copyfile(source_root / "LICENSE", root / "LICENSE.txt")
        (root / "THIRD-PARTY-NOTICES.txt").write_text(
            art.notice_text(crate / "NOTICE"), encoding="utf-8")
        (root / "README.txt").write_text(
            "Meshbus Rust CLI. Use meshbus --help. Offline package tools and device commands require no Python/west/protoc. "
            "Extension builds require external CMake, Ninja and Zephyr SDK. Linux USB/serial permissions and Windows probe "
            "drivers are OS prerequisites. Firmware/EDK production requires a standard west workspace. "
            "This candidate has no production code signature or notarization.\n", encoding="utf-8")
        art.collect_licenses(source_root, root, runtime_packages)
        generated = licensing.copy_component(proto_root, root, "meshbus-protobufs",
                                             [p.relative_to(proto_root).as_posix()
                                              for p in (proto_root / "meshbus").glob("*.proto")])
        art.write_json(root / "build-tools.json", {
            "scope": "Target-filtered Cargo build/proc-macro graph; tools are not bundled. "
                     "Generated material requires separate review.", "packages": build_tools})
        art.write_json(root / "generated-materials.json", {
            "protobuf_descriptor": {**generated, "sha256": art.digest(art.read(descriptor))}})
        record = {"schema": 1, "kind": "cli", "target": target, "version": package["version"], "publishable": False,
                  "provenance": source, "build_host": host, "cross_compiled": target != host,
                  "development": development, "build_profile": profile,
                  "source_snapshot": ({"sha256": art.digest(art.read(source_snapshot / "cli-source.json")),
                                       "scope": "Inherited prepared graph; local SDK and schema verified"}
                                      if source_snapshot else None),
                  "cargo_lock_sha256": art.digest(art.read(crate / "Cargo.lock")),
                  "protobuf_descriptor_sha256": art.digest(art.read(descriptor)),
                  "binary": {"file": executable, "sha256": art.digest(binary)},
                  "validation": {"code_signing": "not-run", "notarization": "not-run", "hardware": "not-run"}}
        art.write_json(root / "manifest.json", record)
        art.checksums(root)
        extension = "zip" if "windows" in target else "tar.gz"
        art.pack(root, part / f"meshbus-{package['version']}-{target}.{extension}", "meshbus")
    art.write_json(part / "release-part.json", record)
    art.checksums(part)
    return part


def assemble(args):
    source, output = args.input.resolve(strict=True), args.output.resolve()
    art.require(not output.is_relative_to(source), "assembly output must be outside input tree")
    files = art.files(source)
    inventory = {target["board"]: target for target in targets()}
    products, records, versions, product_paths = set(), [], set(), {}
    edk_command, edk_tool = None, None
    for path in (p for p in files if p.name == "release-part.json"):
        part = path.parent
        art.verify_checksums(part)
        record = json.loads(art.read(path, 4 * 1024 * 1024))
        art.require(record.get("schema") == 1 and record.get("publishable") is False, "invalid candidate record")
        if record["kind"] == "firmware":
            identity = record["target"]
            expected = inventory.get(identity)
            art.require(expected and expected["id"] == record["id"] and "role" not in record,
                        "product device identity differs from matrix")
            image = next(i for i in record["images"] if i["domain"] == "app")
            art.require(art.digest(art.read(part / "app.bin", MAX_IMAGE)) == image["sha256"],
                        "retained application differs from product image")
            authentication = record_authentication(record)
            if record.get("format") == "uf2":
                art.require(authentication == "none" and "full_bin" not in record and
                            [i["domain"] for i in record["images"]] == ["app"],
                            "UF2 part must contain an application only")
                uf2 = record["uf2"]
                verified = verify_uf2(art.read(part / "app.uf2", MAX_IMAGE * 2),
                                      art.read(part / "app.bin", MAX_IMAGE), image["address"],
                                      image["partition_address"] + image["partition_size"], uf2["family_id"],
                                      art.read(part / "app.hex") if "hex_sha256" in uf2 else None)
                art.require(verified == uf2, "retained UF2 differs from product metadata")
            else:
                art.require(record.get("format") == "mcuboot", "unknown firmware image format")
                verify_mcuboot_image(art.read(part / "app.bin", MAX_IMAGE), authentication)
                art.require(image["address"] == image["partition_address"] and
                            len(art.read(part / "app.bin", MAX_IMAGE)) <= image["partition_size"],
                            "MCUboot application exceeds recorded partition")
            firmware_archives = list(part.glob("*-firmware.tar.gz"))
            art.require(len(firmware_archives) == 1, "product must have exactly one firmware archive")
            if record["capabilities"]["llext"]:
                archives = list(part.glob("*-edk.tar.xz"))
                art.require(len(archives) == 1, "LLEXT product must have exactly one EDK")
                if edk_command is None:
                    edk_command = cli_command(require_explicit=True)
                    edk_tool = cli_identity(edk_command)
                art.require(record.get("edk_tool") == edk_tool,
                            "EDK verifier differs from the recorded packaging tool")
                manifest = cli_json("edk", "verify", archives[0], command=edk_command)["manifest"]
                art.require(manifest["target"] == record["target"] and manifest["host"]["version"] == record["version"],
                            "EDK identity differs from product")
            art.require(identity not in products, "duplicate product")
            products.add(identity)
            versions.add(record["version"])
            product_paths[identity] = part
        elif record["kind"] == "cli":
            raise ValueError("CLI parts belong to the separate CLI release train")
        else:
            raise ValueError("unknown release part")
        records.append(record)
    art.require(products == set(inventory), "release is missing required firmware targets")
    art.require(len(versions) == 1, "mixed firmware versions")
    for path in files:
        if path.name.endswith((".tar.gz", ".tar.xz", ".zip")):
            art.verify_sidecar(path)
    deltas, transfer_ids = [], set()
    for path in args.delta_package:
        art.require(args.manifest_public_key and args.image_public_key, "both public keys required for delta packages")
        verified = cli_json("firmware", "package", "verify", path, "--manifest-public-key", args.manifest_public_key,
                            "--image-public-key", args.image_public_key, "--json")
        manifest = cli_json("firmware", "package", "inspect", path, "--json")["manifest"]
        role = {1: "repeater", 2: "room", 3: "sensor"}.get(manifest["role"])
        product = next((r for r in records if r["kind"] == "firmware" and r["capabilities"]["firmware_endpoint"]
                        and r["target"].endswith(f"/mb_{role}") and r["target"].split("/")[0] == manifest["board_id"]), None)
        art.require(product, "delta has no matching endpoint product")
        art.require(art.digest(art.read(product_paths[product["target"]] / "app.bin")) == manifest["target_sha256"],
                    "delta target is not the assembled product image")
        transfer_id = verified["transfer_id"]
        art.require(verified.get("verified") is True and re.fullmatch(r"[0-9a-f]{64}", transfer_id)
                    and transfer_id not in transfer_ids, "invalid or duplicate delta transfer")
        transfer_ids.add(transfer_id)
        deltas.append((path, verified, art.files(path)))
    art.clean_destination(output)
    for path in files:
        destination = output / path.relative_to(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
    for directory, record, entries in deltas:
        destination = output / "dfota" / record["transfer_id"]
        art.require(not destination.exists(), "duplicate delta destination")
        for path in entries:
            dest = destination / path.relative_to(directory)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, dest)
    assets = [{"path": p.relative_to(output).as_posix(), "size": p.stat().st_size, "sha256": art.digest(art.read(p))}
              for p in art.files(output)]
    art.write_json(output / "release.json", {"schema": 1, "firmware_version": next(iter(versions)), "publishable": False,
                   "products": records, "dfota": [r for _, r, _ in deltas], "assets": assets,
                   "unverified_gates": (["production-signing"] if any(
                       record_authentication(r) == "ed25519" for r in records) else []) +
                       ["hardware-qualification", "public-publication"]})
    art.checksums(output)


def add_arguments(parser):
    sub = parser.add_subparsers(dest="release_command", required=True)
    sub.add_parser("matrix", help="Print the product target inventory")
    build = sub.add_parser("build", help="Build selected products with sysbuild and package them")
    build.add_argument("--workspace", type=Path, default=Path("."))
    build.add_argument("--target", action="append", default=[],
                       help="Device ID or one ordinary fully qualified board target; repeatable")
    build.add_argument("--build-root", type=Path, required=True)
    build.add_argument("--image-signing-key", type=Path,
                       help="Opt into Ed25519 MCUboot authentication with a private PEM; default is hash-only")
    fw = sub.add_parser("firmware", help="Package an existing sysbuild without rebuilding firmware")
    fw.add_argument("--build-dir", type=Path, required=True)
    fw.add_argument("--image-public-key", type=Path,
                    help="Verification public PEM; defaults to <sysbuild>/image-public.pem")
    for command in (build, fw):
        command.add_argument("--output", type=Path, required=True)
        command.add_argument("--development", action="store_true",
                             help="allow engineering packages from dirty/off-manifest sources"
                             + (" and build with prj.dev.conf instead of prj.prod.conf" if command is build else ""))
        command.add_argument("--app-sdk", type=Path,
                             help="Local sdk-arduboy directory included in the generated app release source")
    cli = sub.add_parser("cli", help="Build and archive the Rust CLI for a configured Cargo target")
    cli.add_argument("--workspace", type=Path, default=Path("."))
    cli.add_argument("--output", type=Path, required=True)
    cli.add_argument("--target", choices=sorted(CLIENTS), help="Cargo target; cross targets require a configured linker/SDK")
    cli.add_argument("--cargo-target-dir", type=Path)
    cli.add_argument("--source-snapshot", type=Path,
                     help="Use verified CI provenance and only the pinned schema checkout")
    cli.add_argument("--profile", choices=("release", "ci"), default="release",
                     help="CI profile is permitted only with --development")
    cli.add_argument("--development", action="store_true", help="allow dirty/off-manifest engineering CLI packages")
    assembly = sub.add_parser("assemble", help="Verify and aggregate all discovered firmware targets")
    assembly.add_argument("--input", type=Path, required=True)
    assembly.add_argument("--output", type=Path, required=True)
    assembly.add_argument("--delta-package", type=Path, action="append", default=[])
    assembly.add_argument("--manifest-public-key", type=Path)
    assembly.add_argument("--image-public-key", type=Path)
    return parser


def execute(args):
    try:
        if args.release_command == "matrix":
            print(json.dumps({"include": targets()}, indent=2))
        elif args.release_command == "build":
            build_products(args)
        elif args.release_command == "firmware":
            print(json.dumps({"part": str(firmware(args.build_dir, args.output, args.development,
                                                   args.image_public_key, args.app_sdk))}))
        elif args.release_command == "cli":
            print(json.dumps({"part": str(client(args))}))
        elif args.release_command == "assemble":
            assemble(args)
    except subprocess.CalledProcessError as error:
        print(f"west release: external tool exited with {error.returncode}", file=sys.stderr)
        return error.returncode if error.returncode > 0 else 128 - error.returncode
    except (OSError, ValueError, RuntimeError, KeyError, StopIteration) as error:
        print(f"west release: {error}", file=sys.stderr)
        return 1
    return 0


class Release(WestCommand):
    def __init__(self):
        super().__init__("release", "Build and archive product candidates",
                         "Python developer tools. Never publishes; EDK/DFOTA operations use the existing Rust meshbus CLI.")

    def do_add_parser(self, parser_adder):
        return add_arguments(parser_adder.add_parser(self.name, description=self.description))

    def do_run(self, args, unknown_args):
        raise SystemExit(execute(args))


if __name__ == "__main__":
    raise SystemExit(execute(add_arguments(argparse.ArgumentParser(description=__doc__)).parse_args()))
