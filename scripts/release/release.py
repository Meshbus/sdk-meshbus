# SPDX-License-Identifier: Apache-2.0
"""Developer product orchestration. Never publishes or changes device policy.

West and the standalone CI entry point share this parser and implementation.
EDK/DFOTA format operations are delegated to an existing Rust meshbus CLI.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

from west.commands import WestCommand

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import artifacts as art
from meshbus_cli import cli_command

CLIENTS = {
    "aarch64-apple-darwin", "x86_64-apple-darwin",
    "aarch64-pc-windows-msvc", "x86_64-pc-windows-msvc",
    "aarch64-unknown-linux-gnu", "x86_64-unknown-linux-gnu",
}
GA_FIRMWARE_TARGETS = {
    "idea_mesh_tracker_c2/nrf54l15/cpuapp",
}
MAX_IMAGE = 16 * 1024 * 1024
MAX_HOST_TOOL = 256 * 1024 * 1024
SPDX_DOCUMENTS = {"app.spdx", "build.spdx", "modules-deps.spdx", "zephyr.spdx"}


def targets(boards=None):
    """Read firmware-owned device profiles, independent of MeshCore roles."""
    boards = Path(boards) if boards is not None else Path(__file__).resolve().parents[2] / "apps/meshbus/boards"
    path = boards / "products.yml"
    metadata = yaml_file(path)
    art.require(isinstance(metadata, dict) and isinstance(metadata.get("products"), list),
                f"invalid device profile metadata: {path}")
    result, ids, targets_seen = [], set(), set()
    for product in metadata["products"]:
        art.require(isinstance(product, dict) and set(product) == {"id", "board"},
                    f"device profile requires id and board: {path}")
        name, target = product["id"], product["board"]
        art.require(isinstance(name, str) and re.fullmatch(r"[a-z0-9_]+", name)
                    and isinstance(target, str)
                    and re.fullmatch(r"[a-z0-9_]+(?:/[a-z0-9_]+)+", target)
                    and target.split("/")[0] == name
                    and not any(q.startswith("mb_") for q in target.split("/")[1:]),
                    f"invalid ordinary device target: {path}")
        art.require(name not in ids and target not in targets_seen,
                    f"duplicate device identity: {name}")
        ids.add(name)
        targets_seen.add(target)
        result.append(dict(product))
    art.require(result, f"no Meshbus device profiles found under {boards}")
    return sorted(result, key=lambda product: product["board"])


def release_targets(boards=None):
    """Return the explicit firmware GA product set; other variants are fixtures."""
    selected = [target for target in targets(boards) if target["board"] in GA_FIRMWARE_TARGETS]
    art.require({target["board"] for target in selected} == GA_FIRMWARE_TARGETS,
                "firmware GA target is missing from board metadata")
    return selected


def product_name(product):
    return product["id"]


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
    """Accept the current sysbuild domain, legacy app domain, or image directory."""
    build = Path(build)
    for name in ("meshbus", "app"):
        if (build / name / "zephyr/.config").is_file():
            return build / name
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


def public_spdx(raw_documents, identity):
    components = {}
    created = set()
    private_values = set()

    def packages(data):
        for block in re.split(r"(?=^##### Package:)", data.decode("utf-8"), flags=re.M):
            fields = {}
            for line in block.splitlines():
                if ": " in line:
                    key, value = line.split(": ", 1)
                    fields.setdefault(key, []).append(value)
            if "PackageName" in fields:
                yield fields

    def merge(entry, fields, include_licenses=True):
        if include_licenses:
            entry["declared"].update(fields.get("PackageLicenseDeclared", []))
            entry["licenses"].update(fields.get("PackageLicenseConcluded", []))
        entry["suppliers"].update(fields.get("PackageSupplier", []))
        entry["versions"].update(fields.get("PackageVersion", []))
        entry["locations"].update(value for value in fields.get("PackageDownloadLocation", [])
                                  if value != "NOASSERTION")
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
            private = component == "meshbus"
            public_name = "meshbus-sdk" if private else component
            entry = components.setdefault(public_name, {
                "declared": set(), "external_refs": set(), "licenses": set(),
                "locations": set(), "private": private, "suppliers": set(), "versions": set(),
            })
            art.require(entry["private"] == private, f"conflicting SPDX component identity: {public_name}")
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
                merge(components[public_name], fields, include_licenses=False)

    for entry in components.values():
        if entry["private"]:
            private_values.update(entry["versions"] | entry["locations"] | entry["external_refs"])

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
        f"PackageComment: <text>C2 target {target}; private firmware source location and revision omitted.</text>",
    ]
    for name in sorted(components):
        entry = components[name]
        versions = set(entry["versions"])
        locations = set(entry["locations"])
        suppliers = set(entry["suppliers"])
        art.require(entry["private"] or len(versions) <= 1, f"multiple revisions for SPDX component: {name}")
        art.require(entry["private"] or len(locations) <= 1, f"multiple locations for SPDX component: {name}")
        lines += ["", f"##### Package: {name}", "", f"PackageName: {name}",
                  f"SPDXID: {package_ids[name]}"]
        if entry["private"]:
            lines += [f"PackageVersion: {version}", "PackageSupplier: Organization: Meshbus",
                      "PackageDownloadLocation: NOASSERTION"]
        else:
            if versions:
                lines.append(f"PackageVersion: {next(iter(versions))}")
            lines += [f"PackageSupplier: {next(iter(suppliers)) if len(suppliers) == 1 else 'NOASSERTION'}",
                      f"PackageDownloadLocation: {next(iter(locations)) if locations else 'NOASSERTION'}"]
            lines += [f"ExternalRef: {value}" for value in sorted(entry["external_refs"])]
        lines += ["FilesAnalyzed: false", f"PackageLicenseConcluded: {expression(entry['licenses'])}",
                  f"PackageLicenseDeclared: {expression(entry['declared'])}",
                  "PackageCopyrightText: NOASSERTION", "PrimaryPackagePurpose: LIBRARY"]
        if entry["private"]:
            lines.append("PackageComment: <text>Private Meshbus SDK source location and revision omitted; "
                         f"distributed as part of firmware {version}.</text>")
    data = ("\n".join(lines) + "\n").encode()
    art.require(b"FileName:" not in data and b"FileChecksum:" not in data,
                "public SPDX must not expose source-file inventory")
    art.require(all(value.encode() not in data for value in private_values),
                "public SPDX exposes a private SDK location or revision")
    return data, sorted(components)


def generate_spdx(build, sysbuild, output, identity, source_root, required):
    domains = {"app": build, "mcuboot": sysbuild / "mcuboot"}
    enabled = {name: config(path / "zephyr/.config").get("CONFIG_BUILD_OUTPUT_META") == "y"
               for name, path in domains.items()}
    art.require(not any(enabled.values()) or all(enabled.values()),
                "APP and MCUboot must enable CONFIG_BUILD_OUTPUT_META together")
    if not all(enabled.values()):
        art.require(not required, "production C2 package requires APP and MCUboot SPDX metadata")
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
    art.require(Path(app_name).name == app_name and app_name.endswith(".signed.bin"),
                "package requires the native signed application image")
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


def firmware(build_dir, output, development, image_public_key=None, app_sdk=None):
    build, info, conf, target, version, source_root, source = context(build_dir)
    product = next((t for t in targets(source_root / "apps/meshbus/boards") if t["board"] == target), None)
    art.require(product, "not a qualified product target")
    release_target = target in GA_FIRMWARE_TARGETS
    clean = all(not p["dirty"] for p in [source["firmware"], *source["projects"].values()])
    art.require(development or clean, "candidate requires clean committed source; use --development")
    art.require(development or not source["off_manifest"],
                "candidate checkouts differ from the resolved manifest: " +
                ", ".join(source["off_manifest"]))
    art.require(development or release_target, "qualification fixtures require --development")
    sysbuild = build.parent
    art.require((sysbuild / "domains.yaml").is_file() and
                (sysbuild / "mcuboot/zephyr/.config").is_file(), "product requires full sysbuild and MCUboot output")
    if release_target:
        boot_conf = config(sysbuild / "mcuboot/zephyr/.config")
        art.require(conf.get("CONFIG_MCUBOOT_SIGNATURE_KEY_FILE"),
                    "application signing key configuration is missing")
        art.require(boot_conf.get("CONFIG_BOOT_SIGNATURE_TYPE_ED25519") == "y" and
                    boot_conf.get("CONFIG_BOOT_VALIDATE_SLOT0") == "y",
                    "C2 MCUboot must verify Ed25519 authorization on every boot")
        art.require(boot_conf.get("CONFIG_MCUBOOT_SERIAL") == "y" and
                    boot_conf.get("CONFIG_BOOT_SERIAL_UART") == "y" and
                    boot_conf.get("CONFIG_MCUBOOT_BOOT_BLUETOOTH") != "y" and
                    boot_conf.get("CONFIG_BT") != "y",
                    "C2 MCUboot recovery must be UART-only")
    public_key = Path(image_public_key) if image_public_key else sysbuild / "image-public.pem"
    signing = verify_native_signature(build, public_key) if release_target or image_public_key else None
    llext = conf.get("CONFIG_MBS_LLEXT") == "y"
    # Fail before writing a partial product if its required EDK tool is absent.
    edk_command = cli_command(require_explicit=not development) if llext else None
    edk_tool = cli_identity(edk_command) if edk_command else None
    part = output / "firmware" / f"{normalize(version)}-{product_name(product)}"
    art.clean_destination(part)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        segments, images, inputs = [], [], {}
        if signing:
            inputs[public_key] = art.digest(art.read(public_key))
            shutil.copyfile(public_key, root / "image-public.pem")
        for domain, directory in [("mcuboot", sysbuild / "mcuboot"), ("app", build)]:
            settings = config(directory / "zephyr/.config")
            number = lambda key: int(settings[key], 0)
            runner = yaml_file(directory / "zephyr/runners.yaml")["config"]
            name = runner["bin_file"]
            selected_path = directory / "zephyr" / name
            signed_image = "signed" in name
            data = art.read(selected_path, MAX_IMAGE)
            if release_target and domain == "app":
                art.require(name.endswith(".signed.bin"), "C2 package requires the signed application image")
            for path in (selected_path, directory / "zephyr/.config",
                         directory / "zephyr/zephyr.dts", directory / "zephyr/runners.yaml"):
                inputs[path] = art.digest(art.read(path))
            base = number("CONFIG_FLASH_BASE_ADDRESS") + number("CONFIG_FLASH_LOAD_OFFSET")
            address = base + (0 if signed_image else number("CONFIG_ROM_START_OFFSET"))
            size = number("CONFIG_FLASH_LOAD_SIZE")
            art.require(base >= 0 and size > 0 and address >= base and data and
                        address + len(data) <= base + size, f"{domain} binary exceeds partition")
            filename = f"{domain}.bin"
            (root / filename).write_bytes(data)
            if runner.get("hex_file"):
                path = directory / "zephyr" / runner["hex_file"]
                if path.is_file():
                    shutil.copyfile(path, root / f"{domain}.hex")
            images.append({"domain": domain, "file": filename, "address": address,
                           "size": len(data), "sha256": art.digest(data),
                           "partition_address": base, "partition_size": size,
                           "config_sha256": art.digest(art.read(directory / "zephyr/.config")),
                           "devicetree_sha256": art.digest(art.read(directory / "zephyr/zephyr.dts"))})
            segments.append((address, data))
        segments.sort()
        art.require(segments[0][0] + len(segments[0][1]) <= segments[1][0], "overlapping boot/application images")
        first = segments[0][0]
        span = segments[-1][0] + len(segments[-1][1]) - first
        art.require(span <= MAX_IMAGE, "full binary span exceeds bound")
        full = bytearray(b"\xff" * span)
        for address, data in segments:
            full[address - first:address - first + len(data)] = data
        (root / "full.bin").write_bytes(full)
        (root / "full.hex").write_text(full_hex(segments), encoding="ascii", newline="\n")
        sbom = generate_spdx(
            build, sysbuild, root,
            {"full_bin_sha256": art.digest(full), "images": images, "provenance": source,
             "target": target, "version": version},
            source_root, release_target and not development)
        record = {"schema": 1, "kind": "firmware", "id": product["id"], "version": version,
                  "target": target, "publishable": False, "engineering": development,
                  "capabilities": {"firmware_endpoint": conf.get("CONFIG_MBS_FIRMWARE") == "y", "llext": llext},
                  "provenance": source,
                  "build": {"sysbuild": True, "command": ["west", "build", "--sysbuild", "-b", target, "meshbus/apps/meshbus"],
                            "toolchain": info["cmake"]["toolchain"]["name"],
                            "compiler_version": run([cache(build / "CMakeCache.txt", "CMAKE_C_COMPILER"), "-dumpfullversion"], capture=True),
                            "west_version": run(["west", "--version"], capture=True)},
                  "images": images, "full_bin": {"file": "full.bin", "address": first, "fill": 255},
                  "sbom": sbom,
                  "validation": {"build": "passed", "hardware": "not-run", "production_release": "not-qualified"}}
        if edk_tool:
            record["edk_tool"] = edk_tool
        if signing:
            record["signing"] = signing
        art.write_json(root / "flash-map.json", record)
        shutil.copyfile(source_root / "LICENSE", root / "LICENSE.txt")
        (root / "NOTICE.txt").write_text(
            "Meshbus engineering firmware candidate. Not production-qualified. full.bin starts at the address in "
            "flash-map.json; it is not an application-slot image or a DFOTA source. Preserve storage; do not infer "
            "erase-all authorization. Component SPDX licenses apply.\n", encoding="utf-8")
        art.checksums(root)
        art.pack(root, part / f"meshbus-{normalize(version)}-{product_name(product)}-firmware.tar.gz", "firmware")
        shutil.copyfile(root / "app.bin", part / "app.bin")
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
    all_targets = targets(application / "boards")
    art.require(all(any(t["id"] == requested or t["board"] == requested for t in all_targets)
                    for requested in args.target), "unknown target")
    selected_targets = [target for target in all_targets
                        if not args.target or target["id"] in args.target or target["board"] in args.target]
    if not args.target:
        selected_targets = release_targets(application / "boards")
    art.require(args.development or all(target["board"] in GA_FIRMWARE_TARGETS for target in selected_targets),
                "qualification fixtures require --development")
    release_selected = any(target["board"] in GA_FIRMWARE_TARGETS for target in selected_targets)
    image_key_arg = getattr(args, "image_signing_key", None)
    needs_key = release_selected or image_key_arg
    version = art.digest(art.read(application / "VERSION"))[:12]
    image_key = image_private_key(image_key_arg, args.build_root.resolve()) if needs_key else None
    for target in selected_targets:
        directory = args.build_root.resolve() / version / product_name(target)
        command = ["west", "build", "-p", "always", "--sysbuild", "-b", target["board"],
                   application, "-d", directory]
        definitions = []
        if image_key:
            definitions.append(f'-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="{image_key}"')
        if target["board"] in GA_FIRMWARE_TARGETS:
            definitions += ["-DCONFIG_BUILD_OUTPUT_META=y", "-Dmcuboot_CONFIG_BUILD_OUTPUT_META=y"]
        if definitions:
            command += ["--", *definitions]
        run(command, cwd=workspace)
        if image_key:
            directory.mkdir(parents=True, exist_ok=True)
            run([sys.executable, imgtool(), "getpub", "-k", image_key,
                 "-e", "pem", "-o", directory / "image-public.pem"])
        firmware(directory, args.output, args.development, app_sdk=getattr(args, "app_sdk", None))


def client(args):
    workspace = args.workspace.resolve(strict=True)
    source_root = workspace / "meshbus"
    crate = source_root / "scripts/meshbus"
    cargo = os.environ.get("CARGO", "cargo")
    env = os.environ.copy()
    flags = (env["CARGO_ENCODED_RUSTFLAGS"].split("\x1f") if env.get("CARGO_ENCODED_RUSTFLAGS")
             else shlex.split(env.get("RUSTFLAGS", "")))
    flags += [f"--remap-path-prefix={Path.home()}=/build-home", f"--remap-path-prefix={workspace}=/meshbus-firmware"]
    env["CARGO_ENCODED_RUSTFLAGS"] = "\x1f".join(flags)
    host = next(line.removeprefix("host: ") for line in run(["rustc", "-vV"], capture=True, env=env).splitlines()
                if line.startswith("host: "))
    target = args.target or host
    art.require(target in CLIENTS and target == host, "CLI archives must be built on their supported native target")
    part = args.output / "cli" / target
    art.require(not part.exists() or not any(part.iterdir()), f"output directory is not empty: {part}")
    target_dir = args.cargo_target_dir or Path(env.get("CARGO_TARGET_DIR", workspace / "build-meshbus-cli"))
    command = [cargo, "build", "--locked", "--release", "--manifest-path", crate / "Cargo.toml",
               "--target-dir", target_dir.resolve(), "--message-format=json-render-diagnostics"]
    if args.target:
        command += ["--target", target]
    messages = [json.loads(line) for line in run(command, cwd=workspace, capture=True, env=env).splitlines() if line]
    metadata = json.loads(run([cargo, "metadata", "--offline", "--locked", "--format-version", "1",
                               "--manifest-path", crate / "Cargo.toml"], cwd=workspace, capture=True, env=env))
    package = next(p for p in metadata["packages"] if Path(p["manifest_path"]).resolve() == crate / "Cargo.toml")
    binaries = [Path(m["executable"]) for m in messages if m.get("reason") == "compiler-artifact"
                and m.get("package_id") == package["id"] and m.get("executable")]
    scripts = [m for m in messages if m.get("reason") == "build-script-executed"
               and m.get("package_id") == package["id"]]
    art.require(len(binaries) == 1 and len(scripts) == 1, "Cargo did not identify the CLI and its descriptor build output")
    art.require(dict(scripts[0]["env"]).get("MESHBUS_BUILD_TARGET") == target,
                "Cargo compiled a different target; remove build.target overrides or pass the native --target")
    descriptor = Path(scripts[0]["out_dir"]) / "meshbus.pb"
    binary = art.read(binaries[0])
    for path in (workspace, Path.home()):
        for spelling in (str(path), path.as_posix()):
            art.require(len(spelling) < 2 or spelling.encode() not in binary,
                        "CLI embeds a developer path; check Rust path remapping")
    art.clean_destination(part)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        executable = "meshbus.exe" if "windows" in target else "meshbus"
        (root / executable).write_bytes(binary)
        shutil.copyfile(source_root / "LICENSE", root / "LICENSE.txt")
        shutil.copyfile(crate / "THIRD-PARTY-NOTICES.txt", root / "THIRD-PARTY-NOTICES.txt")
        (root / "README.txt").write_text(
            "Meshbus Rust CLI. Use meshbus --help. Offline package tools and device commands require no Python/west/protoc. "
            "Extension builds require external CMake, Ninja and Zephyr SDK. Linux USB/serial permissions and Windows probe "
            "drivers are OS prerequisites. Firmware/EDK production requires a standard west workspace. "
            "This candidate has no production code signature or notarization.\n", encoding="utf-8")
        art.collect_licenses(source_root, root, metadata)
        record = {"schema": 1, "kind": "cli", "target": target, "version": package["version"], "publishable": False,
                  "provenance": provenance(workspace, workspace), "cargo_lock_sha256": art.digest(art.read(crate / "Cargo.lock")),
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
    products, records, versions, product_paths = set(), [], set(), {}
    edk_command, edk_tool = None, None
    for path in (p for p in files if p.name == "release-part.json"):
        part = path.parent
        art.verify_checksums(part)
        record = json.loads(art.read(path, 4 * 1024 * 1024))
        art.require(record.get("schema") == 1 and record.get("publishable") is False, "invalid candidate record")
        if record["kind"] == "firmware":
            identity = record["target"]
            expected = next((t for t in release_targets() if t["board"] == identity), None)
            art.require(expected and expected["id"] == record["id"] and "role" not in record,
                        "product device identity differs from matrix")
            image = next(i for i in record["images"] if i["domain"] == "app")
            art.require(art.digest(art.read(part / "app.bin", MAX_IMAGE)) == image["sha256"],
                        "retained application differs from product image")
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
    art.require(products == {t["board"] for t in release_targets()}, "release is missing required firmware targets")
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
                   "unverified_gates": ["production-signing", "hardware-qualification", "public-publication"]})
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
                       help="Ed25519 private PEM file passed to Zephyr native build signing")
    fw = sub.add_parser("firmware", help="Package an existing sysbuild without rebuilding firmware")
    fw.add_argument("--build-dir", type=Path, required=True)
    fw.add_argument("--image-public-key", type=Path,
                    help="Verification public PEM; defaults to <sysbuild>/image-public.pem")
    for command in (build, fw):
        command.add_argument("--output", type=Path, required=True)
        command.add_argument("--development", action="store_true")
        command.add_argument("--app-sdk", type=Path,
                             help="Local sdk-arduboy directory included in the generated app release source")
    cli = sub.add_parser("cli", help="Explicitly build and archive the native Rust CLI")
    cli.add_argument("--workspace", type=Path, default=Path("."))
    cli.add_argument("--output", type=Path, required=True)
    cli.add_argument("--target", choices=sorted(CLIENTS), help="Native Cargo target (no cross compilation)")
    cli.add_argument("--cargo-target-dir", type=Path)
    assembly = sub.add_parser("assemble", help="Verify and aggregate the firmware GA product set")
    assembly.add_argument("--input", type=Path, required=True)
    assembly.add_argument("--output", type=Path, required=True)
    assembly.add_argument("--delta-package", type=Path, action="append", default=[])
    assembly.add_argument("--manifest-public-key", type=Path)
    assembly.add_argument("--image-public-key", type=Path)
    return parser


def execute(args):
    try:
        if args.release_command == "matrix":
            print(json.dumps({"include": release_targets()}, indent=2))
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
