# SPDX-License-Identifier: Apache-2.0
"""Exercise product artifacts and command boundaries without building firmware."""
import argparse
from contextlib import redirect_stdout, redirect_stderr
import io
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SCRIPTS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SCRIPTS / "release"))
import release  # noqa: E402
import artifacts as art  # noqa: E402
import meshbus_cli  # noqa: E402


def hash_image(payload):
    """Small hash-only MCUboot fixture; native imgtool is exercised below."""
    header = struct.pack("<IIHHIIBBHII", 0x96F3B83D, 0, 32, 0, len(payload), 0, 1, 2, 3, 0, 0)
    body = header + payload
    return body + struct.pack("<HHHH", 0x6907, 40, 0x10, 32) + bytes.fromhex(art.digest(body))


SIGNED_CONFIG = {"CONFIG_BOOTLOADER_MCUBOOT": "y", "CONFIG_BOOT_SIGNATURE_TYPE_ED25519": "y",
                 "CONFIG_BOOT_VALIDATE_SLOT0": "y", "CONFIG_MCUBOOT_SIGNATURE_KEY_FILE": "test.pem",
                 "CONFIG_BOOT_SIGNATURE_KEY_FILE": "test.pem"}


def copy_board_metadata(workspace):
    destination = workspace / "meshbus/apps/meshbus/boards"
    shutil.copytree(SCRIPTS.parent / "apps/meshbus/boards", destination, dirs_exist_ok=True)
    for profile_dir in destination.glob("*/*"):
        relative = profile_dir.relative_to(destination)
        definition = SCRIPTS.parent / "boards" / relative / "board.yml"
        if definition.is_file():
            hardware = workspace / "meshbus/boards" / relative
            hardware.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(definition, hardware / "board.yml")
    zephyr = workspace / "zephyr"
    if not zephyr.exists():
        zephyr.symlink_to(SCRIPTS.parent.parent / "zephyr", target_is_directory=True)


def add_profile(workspace, name="fixture_board", vendor="test", qualifiers=("soc/cpu",)):
    hardware = workspace / "meshbus/boards" / vendor / name
    hardware.mkdir(parents=True, exist_ok=True)
    variants = []
    for qualifier in qualifiers:
        children = variants
        for part in qualifier.split("/"):
            match = next((v for v in children if v["name"] == part), None)
            if match is None:
                match = {"name": part, "variants": []}
                children.append(match)
            children = match["variants"]
    soc_directory = workspace / "meshbus/soc/test"
    soc_directory.mkdir(parents=True, exist_ok=True)
    art.write_json(soc_directory / "soc.yml", {"socs": [{"name": v["name"]} for v in variants]})
    art.write_json(hardware / "board.yml", {"board": {"name": name, "full_name": name,
                   "vendor": vendor, "socs": variants}})
    directory = workspace / "meshbus/apps/meshbus/boards" / vendor / name
    directory.mkdir(parents=True, exist_ok=True)
    for qualifier in qualifiers:
        (directory / f"{name}_{qualifier.replace('/', '_')}.conf").write_text("# APP profile\n")
    return directory


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="release tests ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        copy_board_metadata(self.root)

    def product(self, llext=False, oversize=False, dirty=True, off_manifest=False):
        add_profile(self.root)
        sysbuild = self.root / "sysbuild"
        build = sysbuild / "meshbus"
        for name, base, data in [("mcuboot", 0x1000, b"boot"), ("meshbus", 0x2000, hash_image(b"application"))]:
            directory = sysbuild / name / "zephyr"
            directory.mkdir(parents=True)
            (directory / ".config").write_text(
                f"CONFIG_FLASH_BASE_ADDRESS=0\nCONFIG_FLASH_LOAD_OFFSET={base}\n"
                f"CONFIG_FLASH_LOAD_SIZE={1 if oversize else 4096}\nCONFIG_ROM_START_OFFSET=0\n"
                "CONFIG_BOOT_SIGNATURE_TYPE_NONE=y\nCONFIG_BOOT_VALIDATE_SLOT0=y\n")
            (directory / "zephyr.dts").write_text("/dts-v1/;\n")
            (directory / "runners.yaml").write_text("config:\n  bin_file: zephyr.signed.bin\n")
            (directory / "zephyr.signed.bin").write_bytes(data)
        (sysbuild / "domains.yaml").write_text("domains: []\n")
        (self.root / "meshbus/LICENSE").write_text("test license\n")
        source = {"firmware": {"revision": None if dirty else "f" * 40, "dirty": dirty},
                  "off_manifest": ["meshbus"] if off_manifest else [],
                  "projects": {"meshbus": {"revision": "a" * 40, "dirty": False}}}
        conf = {"CONFIG_BOOTLOADER_MCUBOOT": "y", "CONFIG_MCUBOOT_GENERATE_UNSIGNED_IMAGE": "y",
                "CONFIG_SOC": "soc",
                "CONFIG_MBS_FIRMWARE": "y", "CONFIG_MBS_LLEXT": "y" if llext else "n"}
        ctx = (build, {"cmake": {"toolchain": {"name": "zephyr", "path": "/fixture/toolchain"}}}, conf,
               "fixture_board/soc/cpu", "1.2.3", self.root / "meshbus", source)
        self.enterContext(patch.object(release, "context", return_value=ctx))
        self.enterContext(patch.object(release, "verify_native_signature", return_value={"verified": True}))
        self.enterContext(patch.object(release, "cache", return_value="compiler"))
        self.enterContext(patch.object(release, "run", return_value="test-version"))
        return sysbuild

    def test_product_layout_matches_domain_bytes_and_is_reproducible(self):
        build = self.product()
        with patch.object(release, "cli_command", side_effect=AssertionError("Rust must not run")):
            first = release.firmware(build, self.root / "first", True)
            second = release.firmware(build, self.root / "second", True)
        art.verify_checksums(first)
        one = next(first.glob("*.tar.gz"))
        two = next(second.glob("*.tar.gz"))
        self.assertEqual(one.read_bytes(), two.read_bytes())
        self.assertEqual((first / "app.bin").read_bytes(), hash_image(b"application"))
        with tarfile.open(one) as archive:
            merged = archive.extractfile("firmware/full.bin").read()
            self.assertEqual(merged[:4], b"boot")
            self.assertEqual(merged[4:4096], b"\xff" * 4092)
            self.assertEqual(merged[4096:], hash_image(b"application"))
            mapping = json.load(archive.extractfile("firmware/flash-map.json"))
            self.assertEqual(mapping["full_bin"]["address"], 0x1000)
            self.assertEqual(mapping["id"], "fixture_board")
            self.assertEqual(mapping["soc"], "soc")
            self.assertEqual(mapping, json.loads((first / "flash-map.json").read_text()))
            self.assertNotIn("role", mapping)
            self.assertEqual(first.name, "1.2.3-fixture_board_soc_cpu")
            self.assertFalse(mapping["publishable"])
        with self.assertRaisesRegex(ValueError, "not empty"):
            release.firmware(build, self.root / "first", True)

    def test_dirty_source_rejected_without_development(self):
        build = self.product()
        with self.assertRaisesRegex(ValueError, "clean committed"):
            release.firmware(build, self.root / "output", False)
        self.assertFalse((self.root / "output").exists())

    def test_off_manifest_source_rejected_without_development(self):
        build = self.product(dirty=False, off_manifest=True)
        with self.assertRaisesRegex(ValueError, "differ from the resolved manifest: meshbus"):
            release.firmware(build, self.root / "output", False)
        self.assertFalse((self.root / "output").exists())

    def test_provenance_detects_resolved_manifest_difference(self):
        listing = f"meshbus\t{self.root}"
        state = {"revision": "a" * 40, "dirty": False,
                 "manifest_revision": "b" * 40}
        with patch.object(release, "run", return_value=listing):
            with patch.object(release, "git_state", return_value=state) as git_state:
                result = release.provenance(self.root, self.root)
        self.assertEqual(result["off_manifest"], ["meshbus"])
        self.assertNotIn("manifest_revision", result["projects"]["meshbus"])
        git_state.assert_any_call(str(self.root), include_manifest=True)

    def test_partition_overflow_has_no_success_record(self):
        build = self.product(oversize=True)
        with self.assertRaisesRegex(ValueError, "exceeds partition"):
            release.firmware(build, self.root / "output", True)
        self.assertEqual(list((self.root / "output").rglob("release-part.json")), [])

    def test_unsigned_archive_has_no_public_key_or_signing_claim(self):
        build = self.product()
        with patch.object(release, "verify_native_signature", side_effect=AssertionError("must not authenticate")):
            part = release.firmware(build, self.root / "unsigned", True)
        record = json.loads((part / "release-part.json").read_text())
        self.assertEqual(record["authentication"], "none")
        self.assertNotIn("signing", record)
        with tarfile.open(next(part.glob("*.tar.gz"))) as archive:
            self.assertFalse(any(name.endswith(".pem") for name in archive.getnames()))
            self.assertEqual(json.load(archive.extractfile("firmware/flash-map.json")), record)

    def test_clean_unsigned_product_does_not_require_development_mode(self):
        build = self.product(dirty=False)
        with patch.object(release, "generate_spdx", return_value={"status": "generated"}) as spdx, \
                patch.object(release.licensing, "firmware", return_value={"selection": "fixture"}):
            part = release.firmware(build, self.root / "unsigned-prod", False)
        record = json.loads((part / "release-part.json").read_text())
        self.assertEqual(record["authentication"], "none")
        self.assertFalse(record["engineering"])
        self.assertNotIn("signing", record)
        self.assertTrue(spdx.call_args.args[5], "production still requires SPDX metadata")

    def test_unsigned_package_rejects_stale_public_key(self):
        build = self.product()
        (build / "image-public.pem").write_text("stale public key")
        with self.assertRaisesRegex(ValueError, "unsigned build must not supply"):
            release.firmware(build, self.root / "unsigned", True)

    def test_unsigned_package_rejects_hash_corruption(self):
        build = self.product()
        image = build / "meshbus/zephyr/zephyr.signed.bin"
        data = bytearray(image.read_bytes())
        data[32] ^= 1
        image.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "image hash"):
            release.firmware(build, self.root / "unsigned", True)
        self.assertFalse(list((self.root / "unsigned").rglob("release-part.json")))

    def test_mcuboot_configuration_cannot_disable_validation_or_mismatch_modes(self):
        build = self.product() / "meshbus"
        app = release.context(build)[2]
        boot = build.parent / "mcuboot/zephyr/.config"
        for value, error in [
                ("CONFIG_BOOT_SIGNATURE_TYPE_NONE=y\n", "validate the primary"),
                ("CONFIG_BOOT_SIGNATURE_TYPE_ED25519=y\nCONFIG_BOOT_VALIDATE_SLOT0=y\n", "modes differ"),
                ("CONFIG_BOOT_SIGNATURE_TYPE_RSA=y\nCONFIG_BOOT_VALIDATE_SLOT0=y\n", "hash-only or Ed25519")]:
            with self.subTest(value=value):
                boot.write_text(value)
                with self.assertRaisesRegex(ValueError, error):
                    release.mcuboot_authentication(build, app)

    def test_soc_identity_must_come_from_matching_generated_configuration(self):
        build = self.product()
        conf = release.context(build)[2]
        for soc in ["", "different_soc"]:
            conf["CONFIG_SOC"] = soc
            with self.subTest(soc=soc), self.assertRaisesRegex(ValueError, "CONFIG_SOC"):
                release.firmware(build, self.root / "invalid-soc", True)
        self.assertFalse(list((self.root / "invalid-soc").rglob("flash-map.json")))

    def test_required_edk_failure_cannot_complete_product(self):
        build = self.product(llext=True)
        with patch.object(release, "cli_command", return_value=["meshbus"]), \
                patch.object(release, "cli_identity", return_value={"version": "meshbus 1.0.0", "sha256": "a" * 64}):
            with patch.object(release, "run", side_effect=["compiler", "west", subprocess.CalledProcessError(9, "meshbus")]):
                with self.assertRaises(subprocess.CalledProcessError):
                    release.firmware(build, self.root / "output", True)
        self.assertEqual(list((self.root / "output").rglob("release-part.json")), [])

    def test_required_edk_must_exist_and_verify_before_success_record(self):
        build = self.product(llext=True)
        identity = {"version": "meshbus 1.0.0", "sha256": "a" * 64}
        with patch.object(release, "cli_command", return_value=["meshbus"]), \
                patch.object(release, "cli_identity", return_value=identity):
            with self.assertRaisesRegex(ValueError, "exactly one EDK"):
                release.firmware(build, self.root / "missing", True)
        self.assertEqual(list((self.root / "missing").rglob("release-part.json")), [])

        def export(command, **kwargs):
            if command[:2] == ["meshbus", "edk"]:
                destination = Path(command[command.index("-o") + 1])
                archive = destination / "test-edk.tar.xz"
                archive.write_bytes(b"EDK fixture")
                art.sidecar(archive)
            elif command[:3] == ["meshbus", "app", "source"]:
                self.assertEqual(command[command.index("--toolchain") + 1], "/fixture/toolchain")
                art.write_json(Path(command[command.index("--output") + 1]), {"schema": 1})
            return "test-version"

        manifest = {"target": "fixture_board/soc/cpu",
                    "host": {"version": "1.2.3"}}
        with patch.object(release, "cli_command", return_value=["meshbus"]), \
                patch.object(release, "cli_identity", return_value=identity), \
                patch.object(release, "run", side_effect=export), \
                patch.object(release, "cli_json", return_value={"manifest": manifest}):
            part = release.firmware(build, self.root / "valid", True)
        record = json.loads((part / "release-part.json").read_text())
        self.assertEqual(record["edk_tool"], identity)
        self.assertEqual(json.loads((part / "release-source.json").read_text())["schema"], 1)
        art.verify_checksums(part)

    def test_intel_hex_handles_unaligned_64k_crossing(self):
        expected = bytes(range(35))
        text = release.full_hex([(0xfff9, expected)])
        actual, upper = {}, 0
        for line in text.splitlines():
            record = bytes.fromhex(line[1:])
            self.assertEqual(sum(record) % 256, 0)
            length, address, kind = record[0], int.from_bytes(record[1:3], "big"), record[3]
            if kind == 4:
                upper = int.from_bytes(record[4:-1], "big") << 16
            if kind == 0:
                self.assertLessEqual(address + length, 65536)
                actual.update({upper + address + i: b for i, b in enumerate(record[4:-1])})
        self.assertEqual(bytes(actual[i] for i in range(0xfff9, 0xfff9 + len(expected))), expected)

    def test_edk_rebuild_cannot_mix_old_image_with_new_inputs(self):
        build = self.product(llext=True)
        def tool(command, **kwargs):
            if command[0] == "meshbus":
                (build / "meshbus/zephyr/zephyr.signed.bin").write_bytes(b"recompiled")
            return "version"
        with patch.object(release, "cli_command", return_value=["meshbus"]), \
                patch.object(release, "cli_identity", return_value={"version": "meshbus 1.0.0", "sha256": "a" * 64}):
            with patch.object(release, "run", side_effect=tool):
                with self.assertRaisesRegex(ValueError, "EDK export changed"):
                    release.firmware(build, self.root / "output", True)
        self.assertEqual(list((self.root / "output").rglob("release-part.json")), [])

    def test_spdx_requires_both_images_and_rejects_host_paths(self):
        sysbuild = self.product()
        app = sysbuild / "meshbus"
        boot = sysbuild / "mcuboot"
        with self.assertRaisesRegex(ValueError, "production package requires"):
            release.generate_spdx(app, sysbuild, self.root / "missing", {}, self.root, True)
        with self.assertRaisesRegex(ValueError, "must enable.*together"):
            with (app / "zephyr/.config").open("a") as config_file:
                config_file.write("CONFIG_BUILD_OUTPUT_META=y\n")
            release.generate_spdx(app, sysbuild, self.root / "partial", {}, self.root, False)

        with (boot / "zephyr/.config").open("a") as config_file:
            config_file.write("CONFIG_BUILD_OUTPUT_META=y\n")

        def generate(command, **kwargs):
            destination = Path(command[command.index("--spdx-dir") + 1])
            destination.mkdir(parents=True, exist_ok=True)
            header = ("SPDXVersion: SPDX-2.3\nDataLicense: CC0-1.0\n"
                      "SPDXID: SPDXRef-DOCUMENT\nCreated: 2026-01-01T00:00:00Z\n")
            for name in release.SPDX_DOCUMENTS:
                text = header
                if name == "zephyr.spdx":
                    text += ("\n##### Package: meshbus-sources\n\n"
                             "PackageName: meshbus-sources\n"
                             "PackageVersion: " + "a" * 40 + "\n"
                             "PackageSupplier: Organization: Meshbus\n"
                             "PackageDownloadLocation: git+https://github.com/Meshbus/sdk-meshbus@" + "a" * 40 + "\n"
                             "ExternalRef: PACKAGE-MANAGER purl pkg:github/Meshbus/sdk-meshbus@" + "a" * 40 + "\n"
                             "PackageLicenseConcluded: Apache-2.0\n"
                             "PackageLicenseDeclared: NOASSERTION\n"
                             "FileName: ./private.c\n"
                             "\n##### Package: zephyr-sources\n\n"
                             "PackageName: zephyr-sources\n"
                             "PackageVersion: " + "b" * 40 + "\n"
                             "PackageSupplier: Organization: The Zephyr Project\n"
                             "PackageDownloadLocation: NOASSERTION\n"
                             "PackageLicenseConcluded: Apache-2.0\n"
                             "PackageLicenseDeclared: Apache-2.0\n"
                             "FileName: ./kernel.c\n")
                if name == "modules-deps.spdx":
                    text += ("\n##### Package: zephyr-deps\n\n"
                             "PackageName: zephyr-deps\n"
                             "PackageVersion: " + "b" * 40 + "\n"
                             "PackageSupplier: Organization: The Zephyr Project\n"
                             "PackageDownloadLocation: git+https://github.com/zephyrproject-rtos/zephyr@" + "b" * 40 + "\n"
                             "ExternalRef: PACKAGE-MANAGER purl pkg:github/zephyrproject-rtos/zephyr@" + "b" * 40 + "\n"
                             "PackageLicenseConcluded: NOASSERTION\n")
                (destination / name).write_text(text)

        output = self.root / "valid"
        output.mkdir()
        identity = {"build": "test", "full_bin_sha256": "c" * 64,
                    "target": "mesh_probe_r2/nrf54l15/cpuapp",
                    "version": "1.2.3"}
        with patch.object(release, "run", side_effect=generate) as invoked:
            result = release.generate_spdx(app, sysbuild, output, identity, self.root, True)
        self.assertEqual(invoked.call_count, 2)
        self.assertEqual(result["status"], "generated")
        self.assertEqual(len(result["documents"]), 1)
        self.assertEqual(result["private_retention"]["documents"], 8)
        public = (output / "SBOM.spdx").read_text()
        self.assertIn("PackageName: meshbus-sdk\n", public)
        self.assertIn("PackageVersion: 1.2.3", public)
        self.assertIn("github.com/zephyrproject-rtos/zephyr@" + "b" * 40, public)
        self.assertIn("PackageVersion: " + "a" * 40, public)
        self.assertIn("PackageDownloadLocation: git+https://github.com/Meshbus/sdk-meshbus@" + "a" * 40, public)
        self.assertIn("ExternalRef: PACKAGE-MANAGER purl pkg:github/Meshbus/sdk-meshbus@" + "a" * 40, public)
        self.assertNotIn("Private Meshbus", public)
        self.assertNotIn("private firmware", public)
        self.assertNotIn("FileName:", public)
        raw = sysbuild / result["private_retention"]["path"]
        art.verify_checksums(raw)
        self.assertIn("github.com/Meshbus/sdk-meshbus", (raw / "app/zephyr.spdx").read_text())

        def leak(command, **kwargs):
            destination = Path(command[command.index("--spdx-dir") + 1])
            destination.mkdir(parents=True, exist_ok=True)
            for name in release.SPDX_DOCUMENTS:
                (destination / name).write_text(f"SPDXVersion: SPDX-2.3\n{self.root}\n")

        (self.root / "leak").mkdir()
        with patch.object(release, "run", side_effect=leak):
            with self.assertRaisesRegex(ValueError, "developer path"):
                release.generate_spdx(app, sysbuild, self.root / "leak", identity, self.root, True)

    def test_public_spdx_rejects_local_or_credential_bearing_source_locations(self):
        revision = "a" * 40
        source = ("Created: 2026-01-01T00:00:00Z\n##### Package: meshbus-sources\n"
                  "PackageName: meshbus-sources\nPackageVersion: " + revision + "\n"
                  "PackageDownloadLocation: {location}\nFileName: ./app.c\n")
        identity = {"version": "1.0.0", "target": "test", "seed": "test", "full_bin_sha256": "b" * 64}
        for location in ("file:///tmp/checkout", "/home/builder/meshbus", "C:\\build\\meshbus",
                         "git+https://user:secret@example.com/meshbus@" + revision,
                         "https://example.com/meshbus?access_token=secret",
                         "https://example.com/meshbus#secret"):
            with self.subTest(location=location), self.assertRaisesRegex(ValueError, "credential-free"):
                release.public_spdx({("app", "zephyr.spdx"): source.format(location=location).encode()}, identity)

        documents = {("app", "zephyr.spdx"): source.format(
            location="git+git@github.com:Meshbus/sdk-meshbus@" + revision).encode()}
        public, _ = release.public_spdx(documents, identity)
        self.assertIn(("git+ssh://git@github.com/Meshbus/sdk-meshbus@" + revision).encode(), public)
        reference = ("ExternalRef: PACKAGE-MANAGER purl pkg:github/Meshbus/sdk-meshbus@" + revision
                     + "?repository_url=https%3A%2F%2Fgithub.com%2FMeshbus%2Fsdk-meshbus#subsys\n").encode()
        documents[("app", "zephyr.spdx")] += reference
        public, _ = release.public_spdx(documents, identity)
        self.assertIn(reference, public)

    def test_public_spdx_rejects_conflicting_meshbus_source_revisions(self):
        def source(revision):
            return ("Created: 2026-01-01T00:00:00Z\n##### Package: meshbus-sources\n"
                    "PackageName: meshbus-sources\nPackageVersion: " + revision + "\n"
                    "PackageDownloadLocation: git+https://github.com/Meshbus/sdk-meshbus@" + revision + "\n"
                    "FileName: ./app.c\n").encode()
        identity = {"version": "1.0.0", "target": "test", "seed": "test", "full_bin_sha256": "b" * 64}
        with self.assertRaisesRegex(ValueError, "multiple revisions"):
            release.public_spdx({("app", "zephyr.spdx"): source("a" * 40),
                                 ("mcuboot", "zephyr.spdx"): source("c" * 40)}, identity)

    def test_checksums_reject_tampering_unlisted_files_and_traversal(self):
        root = self.root / "part"
        root.mkdir()
        (root / "payload").write_bytes(b"hello")
        art.checksums(root)
        art.verify_checksums(root)
        (root / "payload").write_bytes(b"broken")
        with self.assertRaisesRegex(ValueError, "mismatch"):
            art.verify_checksums(root)
        art.checksums(root)
        (root / "extra").write_bytes(b"not indexed")
        with self.assertRaisesRegex(ValueError, "complete part"):
            art.verify_checksums(root)
        (root / "SHA256SUMS").write_text("0" * 64 + "  ../secret\n")
        with self.assertRaisesRegex(ValueError, "unsafe"):
            art.verify_checksums(root)

    @unittest.skipIf(os.name == "nt", "Windows runner may not allow symlink creation")
    def test_symlink_part_rejected(self):
        (self.root / "link").symlink_to("outside")
        with self.assertRaisesRegex(ValueError, "symlink"):
            art.files(self.root)

    def test_zip_is_deterministic_and_portable(self):
        root = self.root / "input"
        root.mkdir()
        (root / "meshbus.exe").write_bytes(b"executable")
        for name in ("one.zip", "two.zip"):
            art.pack(root, self.root / name, "meshbus")
            art.verify_sidecar(self.root / name)
        self.assertEqual((self.root / "one.zip").read_bytes(), (self.root / "two.zip").read_bytes())
        with zipfile.ZipFile(self.root / "one.zip") as archive:
            self.assertEqual(archive.read("meshbus/meshbus.exe"), b"executable")

    def assembly_parts(self):
        source = self.root / "parts"
        for target in release.targets():
            part = source / "firmware" / f"1.2.3-{target['id']}"
            part.mkdir(parents=True)
            image = hash_image(target["board"].encode())
            (part / "app.bin").write_bytes(image)
            record = {"schema": 1, "kind": "firmware", "publishable": False,
                      "id": target["id"], "target": target["board"], "version": "1.2.3",
                      "format": "mcuboot", "authentication": "none",
                      "images": [{"domain": "app", "sha256": art.digest(image), "address": 0x20000,
                                  "partition_address": 0x20000, "partition_size": 4096}],
                      "capabilities": {"llext": False, "firmware_endpoint": False}}
            art.write_json(part / "release-part.json", record)
            archive = part / "test-firmware.tar.gz"
            archive.write_bytes(b"archive fixture")
            art.sidecar(archive)
            art.checksums(part)
        return argparse.Namespace(input=source, output=self.root / "assembled", delta_package=[],
                                  manifest_public_key=None, image_public_key=None)

    def test_assembly_requires_all_discovered_firmware_targets(self):
        args = self.assembly_parts()
        with patch.object(release, "cli_command", side_effect=AssertionError("no format tools needed")):
            release.assemble(args)
        art.verify_checksums(args.output)
        index = json.loads((args.output / "release.json").read_text())
        self.assertEqual([record["target"] for record in index["products"]],
                         ["mesh_probe_r1/nrf52840", "mesh_probe_r2/nrf54l15/cpuapp",
                          "tracker_t1000_e/nrf52840",
                          "wio_tracker_l1/nrf52840"])
        self.assertNotIn("production-signing", index["unverified_gates"])
        self.assertFalse(index["publishable"])

    def test_assembly_rejects_authentication_metadata_conflicts(self):
        args = self.assembly_parts()
        path = next(args.input.rglob("release-part.json"))
        original = json.loads(path.read_text())
        cases = [({**original, "signing": {"verified": True}}, "unsigned record"),
                 ({k: v for k, v in original.items() if k != "authentication"}, "missing authentication"),
                 ({**original, "authentication": None}, "invalid authentication"),
                 ({k: v for k, v in original.items() if k != "format"}, "unknown firmware image format"),
                 ({**original, "authentication": "ed25519"}, "signing metadata"),
                 ({**original, "authentication": "ed25519", "signing": {
                     "verified": True, "app_sha256": original["images"][0]["sha256"]}}, "Ed25519 authentication")]
        for record, error in cases:
            with self.subTest(error=error):
                art.write_json(path, record)
                art.checksums(path.parent)
                with self.assertRaisesRegex(ValueError, error):
                    release.assemble(args)

    def test_release_records_require_an_explicit_authentication_mode(self):
        for image_format in ("mcuboot", "uf2"):
            for signing in (None, {"verified": True, "app_sha256": "a" * 64}):
                record = {"format": image_format,
                          "images": [{"domain": "app", "sha256": "a" * 64}]}
                if signing is not None:
                    record["signing"] = signing
                with self.subTest(format=image_format, signing=signing), \
                        self.assertRaisesRegex(ValueError, "missing authentication"):
                    release.record_authentication(record)

    def test_assembly_rechecks_image_hash_after_outer_checksums_are_replaced(self):
        args = self.assembly_parts()
        path = next(args.input.rglob("release-part.json"))
        image = path.parent / "app.bin"
        data = bytearray(image.read_bytes())
        data[32] ^= 1
        image.write_bytes(data)
        record = json.loads(path.read_text())
        record["images"][0]["sha256"] = art.digest(data)
        art.write_json(path, record)
        art.checksums(path.parent)
        with self.assertRaisesRegex(ValueError, "image hash"):
            release.assemble(args)

    def test_firmware_assembly_rejects_cli_parts(self):
        args = self.assembly_parts()
        target = sorted(release.CLIENTS)[0]
        part = args.input / "cli" / target
        part.mkdir(parents=True)
        art.write_json(part / "release-part.json", {"schema": 1, "kind": "cli", "target": target,
                       "version": "0.3.1", "publishable": False})
        art.checksums(part)
        with self.assertRaisesRegex(ValueError, "separate CLI release train"):
            release.assemble(args)
        self.assertFalse(args.output.exists())

    def test_assembly_rejects_wrong_retained_image_even_with_updated_checksums(self):
        args = self.assembly_parts()
        image = next(args.input.rglob("app.bin"))
        image.write_bytes(b"wrong image")
        art.checksums(image.parent)
        with self.assertRaisesRegex(ValueError, "retained application"):
            release.assemble(args)
        self.assertFalse(args.output.exists())

    def test_assembly_requires_the_mesh_probe_r2_target(self):
        args = self.assembly_parts()
        shutil.rmtree(args.input / "firmware/1.2.3-mesh_probe_r2")
        with self.assertRaisesRegex(ValueError, "missing required firmware targets"):
            release.assemble(args)
        self.assertFalse(args.output.exists())

    def test_assembly_rejects_wrong_device_identity_and_role_field(self):
        args = self.assembly_parts()
        part = args.input / "firmware/1.2.3-mesh_probe_r2"
        path = part / "release-part.json"
        record = json.loads(path.read_text())
        for field, value in [("id", "mesh_probe_r2-client"), ("role", "room")]:
            art.write_json(path, {**record, field: value})
            art.checksums(part)
            with self.assertRaisesRegex(ValueError, "device identity differs"):
                release.assemble(args)
            self.assertFalse(args.output.exists())

    def test_delta_matches_the_role_image_not_the_last_part_for_a_board(self):
        args = self.assembly_parts()
        delta = self.root / "delta"
        delta.mkdir()
        (delta / "patch.newp").write_bytes(b"delta fixture")
        args.delta_package = [delta]
        args.manifest_public_key = args.image_public_key = self.root / "public-key"
        transfer = "a" * 64
        manifest = {"role": 1, "board_id": "fixture_board",
                    "target_sha256": art.digest(b"fixture_board/soc/cpu")}
        with patch.object(release, "cli_json", side_effect=[{"verified": True, "transfer_id": transfer},
                                                          {"manifest": manifest}]):
            with self.assertRaisesRegex(ValueError, "no matching endpoint product"):
                release.assemble(args)
        self.assertFalse(args.output.exists())


def uf2_bytes(binary, address=0x2000, family=0xADA52840):
    count = (len(binary) + 255) // 256
    return b"".join(
        struct.pack("<8I", 0x0A324655, 0x9E5D5157, 0x2000, address + i * 256,
                    256, i, count, family) + binary[i * 256:(i + 1) * 256].ljust(256, b"\0") +
        bytes(220) + struct.pack("<I", 0x0AB16F30)
        for i in range(count))


class UF2Tests(unittest.TestCase):
    setUp = ArtifactTests.setUp

    def test_native_hex_conversion_with_sparse_regions(self):
        spec = importlib.util.spec_from_file_location(
            "native_uf2", SCRIPTS.parent.parent / "zephyr/scripts/build/uf2conv.py")
        converter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(converter)
        converter.familyid = 0xADA52840
        hex_data = release.full_hex([(0x2000, b"first"), (0x2020, b"second"),
                                 (0x2400, b"last")]).encode()
        binary = bytearray(b"\xff" * 0x404)
        binary[:5] = b"first"
        binary[0x20:0x26] = b"second"
        binary[0x400:] = b"last"
        uf2 = converter.convert_from_hex_to_uf2(hex_data.decode())
        verified = release.verify_uf2(uf2, binary, 0x2000, 0x3000, converter.familyid, hex_data)
        self.assertEqual(verified["hex_sha256"], art.digest(hex_data))
        self.assertEqual(len(uf2), 1024)
        binary[6] = 0
        with self.assertRaisesRegex(ValueError, "HEX payload differs"):
            release.verify_uf2(uf2, binary, 0x2000, 0x3000, converter.familyid, hex_data)
        with self.assertRaisesRegex(ValueError, "invalid application HEX"):
            release.verify_uf2(uf2, binary, 0x2000, 0x3000, converter.familyid, b"invalid")

    def test_spdx_release_version_for_same_checkout_keeps_source_revision(self):
        revision = "b" * 40 + "-off"
        location = "git+https://example.com/library@" + revision
        source = ("Created: 2026-01-01T00:00:00Z\n##### Package: library-sources\n"
                  "PackageName: library-sources\nPackageVersion: " + revision +
                  "\nPackageDownloadLocation: " + location + "\nFileName: ./lib.c\n")
        dependency = ("##### Package: library-deps\nPackageName: library-deps\n"
                      "PackageVersion: 4.1.1\nPackageDownloadLocation: " + location +
                      "\nExternalRef: PACKAGE-MANAGER purl pkg:generic/library@4.1.1\n")
        identity = {"version": "1.0.0", "target": "test", "seed": "test", "full_bin_sha256": "a" * 64}
        documents = {("app", "zephyr.spdx"): source.encode(),
                     ("app", "modules-deps.spdx"): dependency.encode()}
        public, _ = release.public_spdx(documents, identity)
        self.assertIn(("PackageVersion: " + revision).encode(), public)
        self.assertIn(b"pkg:generic/library@4.1.1", public)
        documents[("app", "modules-deps.spdx")] = dependency.replace(revision, "c" * 40).encode()
        with self.assertRaisesRegex(ValueError, "multiple revisions"):
            release.public_spdx(documents, identity)

    def product(self, llext=False, dirty=True):
        root = ArtifactTests.product(self, llext=llext, dirty=dirty)
        shutil.rmtree(root / "mcuboot")
        context = list(release.context(root))
        context[2] = {**context[2], "CONFIG_BOOTLOADER_MCUBOOT": "n", "CONFIG_BUILD_OUTPUT_UF2": "y"}
        self.enterContext(patch.object(release, "context", return_value=tuple(context)))
        app = root / "meshbus/zephyr"
        (app / ".config").write_text(
            "CONFIG_BUILD_OUTPUT_UF2=y\nCONFIG_BUILD_OUTPUT_UF2_FAMILY_ID=0xada52840\n"
            "CONFIG_FLASH_BASE_ADDRESS=0\nCONFIG_FLASH_LOAD_OFFSET=0x2000\n"
            "CONFIG_FLASH_LOAD_SIZE=4096\nCONFIG_ROM_START_OFFSET=0\n")
        (app / "runners.yaml").write_text("config:\n  bin_file: zephyr.bin\n  uf2_file: zephyr.uf2\n")
        binary = bytes(range(256)) + b"application tail"
        (app / "zephyr.bin").write_bytes(binary)
        (app / "zephyr.uf2").write_bytes(uf2_bytes(binary))
        return root

    def test_application_only_archive_needs_no_bootloader_or_signing_key(self):
        build = self.product()
        with patch.object(release, "verify_native_signature", side_effect=AssertionError("must not sign UF2")):
            part = release.firmware(build, self.root / "parts", True)
        record = json.loads((part / "release-part.json").read_text())
        self.assertEqual(record["format"], "uf2")
        self.assertTrue(record["uf2"]["verified"])
        self.assertEqual([i["domain"] for i in record["images"]], ["app"])
        self.assertNotIn("signing", record)
        self.assertNotIn("full_bin", record)
        with tarfile.open(next(part.glob("*.tar.gz"))) as archive:
            names = archive.getnames()
            self.assertIn("firmware/app.uf2", names)
            self.assertNotIn("firmware/full.bin", names)
            self.assertNotIn("firmware/mcuboot.bin", names)
            self.assertNotIn("firmware/image-public.pem", names)
        art.verify_checksums(part)

    def test_bad_uf2_cannot_complete_a_package(self):
        build = self.product()
        uf2 = build / "meshbus/zephyr/zephyr.uf2"
        data = bytearray(uf2.read_bytes())
        data[40] ^= 1
        uf2.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "payload differs"):
            release.firmware(build, self.root / "parts", True)
        self.assertFalse(list((self.root / "parts").rglob("release-part.json")))

    def test_missing_uf2_cannot_complete_a_package(self):
        build = self.product()
        (build / "meshbus/zephyr/zephyr.uf2").unlink()
        with self.assertRaises(OSError):
            release.firmware(build, self.root / "parts", True)
        self.assertFalse(list((self.root / "parts").rglob("release-part.json")))

    def test_uf2_export_mutation_cannot_complete_a_package(self):
        build = self.product(llext=True)
        def run(command, **kwargs):
            if command[:2] == ["meshbus", "edk"]:
                (build / "meshbus/zephyr/zephyr.uf2").write_bytes(b"changed by export")
            return "test-version"
        with patch.object(release, "cli_command", return_value=["meshbus"]), \
                patch.object(release, "cli_identity", return_value={"sha256": "a" * 64}), \
                patch.object(release, "run", side_effect=run):
            with self.assertRaisesRegex(ValueError, "EDK export changed"):
                release.firmware(build, self.root / "parts", True)
        self.assertFalse(list((self.root / "parts").rglob("release-part.json")))

    def test_uf2_production_requires_app_spdx(self):
        build = self.product(dirty=False)
        with self.assertRaisesRegex(ValueError, "production package requires SPDX"):
            release.firmware(build, self.root / "parts", False)

    def test_mesh_probe_r2_cannot_bypass_signing_by_claiming_uf2(self):
        build = self.product()
        context = list(release.context(build))
        context[3] = "mesh_probe_r2/nrf54l15/cpuapp"
        context[2] = {**context[2], "CONFIG_SOC": "nrf54l15"}
        with patch.object(release, "context", return_value=tuple(context)):
            with self.assertRaisesRegex(ValueError, "Mesh Probe R2 requires MCUboot"):
                release.firmware(build, self.root / "parts", True)

    def test_protocol_rejects_wrong_addresses_family_flags_counts_and_payload(self):
        binary = bytes(range(256)) + b"tail"
        original = uf2_bytes(binary)
        self.assertTrue(release.verify_uf2(original, binary, 0x2000, 0x3000, 0xADA52840)["verified"])
        for offset, value in [(0, 0), (8, 0), (12, 0), (16, 128), (20, 1), (24, 3),
                              (28, 123), (40, 0), (512 + 40, 123), (508, 0)]:
            with self.subTest(offset=offset):
                data = bytearray(original)
                struct.pack_into("<I", data, offset, value)
                with self.assertRaises(ValueError):
                    release.verify_uf2(data, binary, 0x2000, 0x3000, 0xADA52840)
        with self.assertRaisesRegex(ValueError, "outside application partition"):
            release.verify_uf2(original, binary, 0x2000, 0x2000 + len(binary), 0xADA52840)
        with self.assertRaises(ValueError):
            release.verify_uf2(original[:-1], binary, 0x2000, 0x3000, 0xADA52840)

    def test_assembly_revalidates_uf2_even_with_updated_file_checksums(self):
        build = self.product()
        part = release.firmware(build, self.root / "parts", True)
        args = argparse.Namespace(input=self.root / "parts", output=self.root / "assembled",
                                  delta_package=[], manifest_public_key=None, image_public_key=None)
        with patch.object(release, "targets", return_value=[{"id": "fixture_board", "board": "fixture_board/soc/cpu"}]):
            release.assemble(args)
            data = bytearray((part / "app.uf2").read_bytes())
            data[40] ^= 1
            (part / "app.uf2").write_bytes(data)
            art.checksums(part)
            args.output = self.root / "bad-assembly"
            with self.assertRaisesRegex(ValueError, "payload differs"):
                release.assemble(args)
            self.assertFalse(args.output.exists())

    def test_build_without_key_uses_generated_uf2_config(self):
        (self.root / "meshbus/west.yml").write_text("manifest: {}\n")
        (self.root / "meshbus/apps/meshbus/VERSION").write_text("VERSION_MAJOR = 1\n")
        args = argparse.Namespace(workspace=self.root, target=["tracker_t1000_e"],
                                  build_root=self.root / "build", output=self.root / "parts",
                                  development=True, image_signing_key=None)
        with patch.object(release, "run") as run, patch.object(release, "firmware") as package, \
                patch.object(release, "config", return_value={"CONFIG_BUILD_OUTPUT_UF2": "y"}), \
                patch.object(release, "image_private_key", side_effect=AssertionError("no key needed")), \
                patch.object(release, "imgtool", side_effect=AssertionError("no signer needed")):
            release.build_products(args)
        self.assertEqual(run.call_count, 2)
        self.assertIn("--cmake-only", run.call_args_list[0].args[0])
        self.assertNotIn("--cmake-only", run.call_args_list[1].args[0])
        self.assertFalse(any("SIGNATURE_KEY" in str(arg) for call in run.call_args_list for arg in call.args[0]))
        package.assert_called_once()


class SigningTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="native signing tests ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.private = self.root / "private.pem"
        self.public = self.root / "public.pem"
        self.tool = release.imgtool()
        release.run([sys.executable, self.tool, "keygen", "-k", self.private, "-t", "ed25519"])
        self.private.chmod(0o600)
        release.run([sys.executable, self.tool, "getpub", "-k", self.private,
                     "-e", "pem", "-o", self.public])
        raw = self.root / "public.raw"
        release.run([sys.executable, self.tool, "getpub", "-k", self.public,
                     "-e", "raw", "-o", raw])
        self.sysbuild = self.root / "sysbuild"
        app = self.sysbuild / "meshbus/zephyr"
        boot = self.sysbuild / "mcuboot/zephyr"
        app.mkdir(parents=True)
        boot.mkdir(parents=True)
        (app / ".config").write_text("CONFIG_BOOTLOADER_MCUBOOT=y\n")
        (app / "runners.yaml").write_text("config:\n  bin_file: zephyr.signed.bin\n")
        (app / "zephyr.bin").write_bytes(bytes(32) + b"test application")
        release.run([sys.executable, self.tool, "sign", "-k", self.private,
                     "-v", "1.2.3", "-H", "32", "-S", "4096", "--align", "1",
                     "--overwrite-only", app / "zephyr.bin", app / "zephyr.signed.bin"])
        (boot / "runners.yaml").write_text("config:\n  bin_file: zephyr.bin\n")
        (boot / "zephyr.bin").write_bytes(b"boot-prefix" + raw.read_bytes())
        release.run([sys.executable, self.tool, "getpub", "-k", self.public,
                     "-o", boot / "autogen-pubkey.c"])
        (self.root / "meshbus/apps/meshbus").mkdir(parents=True)
        (self.root / "meshbus/west.yml").write_text("manifest: {}\n")
        (self.root / "meshbus/apps/meshbus/VERSION").write_text("VERSION_MAJOR = 1\n")
        copy_board_metadata(self.root)

    def test_native_verification_rejects_wrong_key_and_tampering(self):
        result = release.verify_native_signature(self.sysbuild, self.public)
        self.assertTrue(result["verified"])
        self.assertEqual(result["method"], "zephyr-imgtool")
        other = self.root / "other.pem"
        release.run([sys.executable, self.tool, "keygen", "-t", "ed25519", "-k", other])
        release.run([sys.executable, self.tool, "getpub", "-k", other,
                     "-e", "pem", "-o", self.root / "other-public.pem"])
        with self.assertRaisesRegex(ValueError, "verification key differs"):
            release.verify_native_signature(self.sysbuild, self.root / "other-public.pem")
        signed = self.sysbuild / "meshbus/zephyr/zephyr.signed.bin"
        data = bytearray(signed.read_bytes())
        data[32] ^= 1
        signed.write_bytes(data)
        with self.assertRaises(subprocess.CalledProcessError):
            release.verify_native_signature(self.sysbuild, self.public)

    def test_native_unsigned_and_signed_images_have_distinct_authentication(self):
        app = self.sysbuild / "meshbus/zephyr"
        unsigned = app / "hash-only.bin"
        release.run([sys.executable, self.tool, "sign", "-v", "1.2.3", "-H", "32",
                     "-S", "4096", "--align", "1", "--overwrite-only",
                     app / "zephyr.bin", unsigned])
        release.verify_mcuboot_image(unsigned.read_bytes(), "none")
        signed = (app / "zephyr.signed.bin").read_bytes()
        release.verify_mcuboot_image(signed, "ed25519")
        with self.assertRaisesRegex(ValueError, "contains authentication"):
            release.verify_mcuboot_image(signed, "none")
        with self.assertRaisesRegex(ValueError, "Ed25519 authentication"):
            release.verify_mcuboot_image(unsigned.read_bytes(), "ed25519")
        record = {"format": "mcuboot", "authentication": "ed25519",
                  "images": [{"domain": "app", "sha256": art.digest(signed)}],
                  "signing": release.verify_native_signature(self.sysbuild, self.public)}
        self.assertEqual(release.record_authentication(record), "ed25519")
        del record["signing"]
        with self.assertRaisesRegex(ValueError, "signing metadata"):
            release.record_authentication(record)

    def test_mcuboot_parser_rejects_truncated_and_forged_tlv_sizes(self):
        image = hash_image(b"payload")
        for end in (0, 31, 32, len(image) - 1):
            with self.subTest(end=end), self.assertRaises(ValueError):
                release.verify_mcuboot_image(image[:end], "none")
        for offset, value in ((8, 0xffff), (10, 0xffff), (12, 0xffff), (len(image) - 34, 0xffff)):
            bad = bytearray(image)
            struct.pack_into("<H", bad, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                release.verify_mcuboot_image(bad, "none")

    def test_default_build_uses_unsigned_mode_without_exporting_a_key(self):
        args = argparse.Namespace(workspace=self.root, target=["mesh_probe_r2"],
                                  build_root=self.root / "build", output=self.root / "parts",
                                  development=False, image_signing_key=None)
        unsigned = {"CONFIG_BOOTLOADER_MCUBOOT": "y", "CONFIG_BOOT_SIGNATURE_TYPE_NONE": "y",
                    "CONFIG_MCUBOOT_GENERATE_UNSIGNED_IMAGE": "y", "CONFIG_BOOT_VALIDATE_SLOT0": "y"}
        with patch.object(release, "config", return_value=unsigned), \
                patch.object(release, "run") as invoked, patch.object(release, "firmware"):
            release.build_products(args)
        self.assertEqual(invoked.call_count, 2)
        configure = invoked.call_args_list[0].args[0]
        directory = configure[configure.index("-d") + 1]
        self.assertEqual(directory.parent.name, "unsigned")
        self.assertFalse(any("SIGNATURE_KEY" in str(arg) for arg in configure))
        self.assertFalse(list(args.build_root.rglob("*.pem")))

    def test_native_build_preserves_key_and_exports_public_pem(self):
        args = argparse.Namespace(workspace=self.root, target=["mesh_probe_r2"], build_root=self.root / "build",
                                  output=self.root / "parts", development=False, image_signing_key=None)
        real_run = release.run
        keys = []

        def run(command, **kwargs):
            if command[:2] == ["west", "build"] and "--cmake-only" in command:
                self.assertIn("-DSB_CONFIG_BOOT_SIGNATURE_TYPE_ED25519=y", command)
                directory = command[command.index("-d") + 1]
                self.assertEqual(directory.parent.name, "ed25519")
                definition = next(str(arg) for arg in command if str(arg).startswith("-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="))
                key = Path(definition.split("=", 1)[1].strip('"'))
                self.assertTrue(key.read_bytes() == self.private.read_bytes())
                keys.append(key)
                return None
            if command[:2] == ["west", "build"]:
                return None
            return real_run(command, **kwargs)

        def package(*_, app_sdk=None):
            self.assertTrue(keys[-1].exists(), "key must remain available during EDK export")

        args.image_signing_key = self.private
        with patch.object(release, "run", side_effect=run), \
                patch.object(release, "config", return_value=SIGNED_CONFIG), \
                patch.object(release, "firmware", side_effect=package):
            release.build_products(args)
        self.assertEqual(keys, [self.private.resolve()])
        self.assertTrue(self.private.exists())
        public = next(args.build_root.rglob("image-public.pem"))
        self.assertEqual(public.read_bytes(), self.public.read_bytes())

    def test_native_build_failure_preserves_caller_key(self):
        args = argparse.Namespace(workspace=self.root, target=["mesh_probe_r2"], build_root=self.root / "build",
                                  output=self.root / "parts", development=True, image_signing_key=self.private)
        original = self.private.read_bytes()
        with patch.object(release, "run", side_effect=RuntimeError("build failed")):
            with self.assertRaisesRegex(RuntimeError, "build failed"):
                release.build_products(args)
        self.assertTrue(self.private.read_bytes() == original)

    @unittest.skipUnless(os.name == "posix", "POSIX private key permissions")
    def test_explicit_key_preserves_file_permission_checks(self):
        self.private.chmod(0o644)
        with self.assertRaisesRegex(ValueError, "must not be group- or world-accessible"):
            release.image_private_key(self.private, self.sysbuild)



class BuildContinuationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.workspace = Path(temporary.name)
        application = self.workspace / "meshbus/apps/meshbus"
        application.mkdir(parents=True)
        (self.workspace / "meshbus/west.yml").write_text("manifest: {}\n")
        (application / "VERSION").write_text("VERSION_MAJOR = 1\n")
        self.inventory = [{"id": name, "board": name + "/soc"} for name in ("first", "middle", "last")]
        self.args = argparse.Namespace(release_command="build", workspace=self.workspace, target=[],
                                       build_root=self.workspace / "build", output=self.workspace / "parts",
                                       development=True, image_signing_key=None)

    def exercise(self, failed_board=None, failed_stage=None, interrupt=False):
        configured, packaged = [], []
        output = io.StringIO()

        def fail(board, stage):
            if board == failed_board and stage == failed_stage:
                if interrupt:
                    raise KeyboardInterrupt()
                if stage in ("configure", "build"):
                    raise subprocess.CalledProcessError(7, ["west", "build"])
                raise ValueError("package validation failed")

        def run(command, **kwargs):
            if "--cmake-only" in command:
                board = command[command.index("-b") + 1].split("/")[0]
                configured.append(board)
                fail(board, "configure")
            else:
                board = Path(command[command.index("-d") + 1]).name.removesuffix("_soc")
                fail(board, "build")

        def package(directory, destination, *args, **kwargs):
            board = directory.name.removesuffix("_soc")
            fail(board, "package")
            part = destination / "firmware" / board
            part.mkdir(parents=True, exist_ok=True)
            (part / "artifact.bin").write_bytes(board.encode())
            packaged.append(board)
            return part

        with patch.object(release, "targets", return_value=self.inventory), \
                patch.object(release, "run", side_effect=run), \
                patch.object(release, "config", return_value={"CONFIG_BUILD_OUTPUT_UF2": "y"}), \
                patch.object(release, "firmware", side_effect=package), \
                redirect_stdout(output), redirect_stderr(output):
            if interrupt:
                with self.assertRaises(KeyboardInterrupt):
                    release.execute(self.args)
                self.assertEqual(configured, ["first"])
                return
            code = release.execute(self.args)
        expected = [entry["id"] for entry in self.inventory if entry["id"] != failed_board]
        self.assertEqual(configured, ["first", "middle", "last"])
        self.assertEqual(packaged, expected)
        for board in expected:
            self.assertEqual((self.args.output / "firmware" / board / "artifact.bin").read_bytes(), board.encode())
        self.assertEqual(code, 0 if failed_board is None else 1 if failed_stage == "package" else 7)
        self.assertIn(f"{len(expected)} succeeded, {3 - len(expected)} failed", output.getvalue())
        if failed_board:
            self.assertIn(f"FAIL {failed_board}/soc [{failed_stage}]", output.getvalue())

    def test_failure_at_each_position_and_stage_preserves_other_artifacts(self):
        for board in ("first", "middle", "last"):
            for stage in ("configure", "build", "package"):
                with self.subTest(board=board, stage=stage):
                    self.exercise(board, stage)

    def test_all_success_returns_zero(self):
        self.exercise()

    def test_build_profiles_use_app_fragments_and_separate_directories(self):
        directories = []
        for development, profile in ((True, "dev"), (False, "prod")):
            with self.subTest(profile=profile):
                self.args.development = development
                with patch.object(release, "targets", return_value=self.inventory[:1]), \
                        patch.object(release, "run") as invoked, \
                        patch.object(release, "config", return_value={"CONFIG_BUILD_OUTPUT_UF2": "y"}), \
                        patch.object(release, "firmware") as packaged, redirect_stdout(io.StringIO()):
                    release.build_products(self.args)
                configure, build = [call.args[0] for call in invoked.call_args_list]
                fragment = self.workspace.resolve() / f"meshbus/apps/meshbus/prj.{profile}.conf"
                self.assertIn(f"-Dmeshbus_EXTRA_CONF_FILE={fragment}", configure)
                self.assertFalse(any(str(arg).startswith(("-DCONF_FILE=", "-DEXTRA_CONF_FILE=",
                                                         "-Dmcuboot_EXTRA_CONF_FILE=")) for arg in configure))
                directory = configure[configure.index("-d") + 1]
                self.assertEqual(directory.parent.name, "unsigned")
                self.assertEqual(directory.parent.parent.name, profile)
                self.assertEqual(directory.name, "first_soc")
                self.assertEqual(build, ["west", "build", "-d", directory])
                packaged.assert_called_once_with(directory, self.args.output, development, app_sdk=None)
                directories.append(directory)
        self.assertNotEqual(*directories)

    def test_user_interrupt_stops_immediately(self):
        self.exercise("first", "build", interrupt=True)


class EntryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def test_cli_path_check_uses_directory_boundaries(self):
        paths = (Path('/root'), Path('/work'))
        release.verify_cli_paths(b'/opt/cargo/probe-rs/root_memory_interface.rs\x00/workflow.rs', paths)
        for binary in (b'/root/.cargo/source.rs', b'/work/meshbus/main.rs', b'/root\x00', b'/work'):
            with self.subTest(binary=binary), self.assertRaisesRegex(ValueError, 'developer path'):
                release.verify_cli_paths(binary, paths)
        with self.assertRaisesRegex(ValueError, 'developer path'):
            release.verify_cli_paths(b'C:\\Users\\builder\\source.rs', (Path('C:\\Users\\builder'),))

    def test_signing_key_argument_rejects_pem_contents_without_echoing_them(self):
        with self.assertRaises(ValueError) as raised:
            release.image_private_key("-----BEGIN PRIVATE KEY-----\nnot-a-real-key\n", Path("unused-build").resolve())
        self.assertNotIn("not-a-real-key", str(raised.exception))

    def test_sysbuild_domain_and_direct_image_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "meshbus"
            (image / "zephyr").mkdir(parents=True)
            (image / "zephyr/.config").write_text("CONFIG_TEST=y\n")
            self.assertEqual(release.app_build(root), image)
            self.assertEqual(release.app_build(image), image)

            other = root / "separate-output"
            (other / "app/zephyr").mkdir(parents=True)
            (other / "app/zephyr/.config").write_text("CONFIG_TEST=y\n")
            self.assertEqual(release.app_build(other), other)
            self.assertEqual(release.app_build(other / "app"), other / "app")

    def test_manifest_source_has_provenance_without_dependency_tracking_ref(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary)
            repo, dependency = workspace / "meshbus", workspace / "zephyr"
            repo.mkdir()
            dependency.mkdir()
            listing = f"manifest\t{repo}\nzephyr\t{dependency}"

            def state(path, include_manifest=False):
                value = {"revision": "a" * 40, "dirty": False}
                if include_manifest:
                    self.assertEqual(Path(path), dependency)
                    value["manifest_revision"] = "b" * 40
                return value

            with patch.object(release, "run", return_value=listing), \
                    patch.object(release, "git_state", side_effect=state):
                result = release.provenance(workspace, repo)
            self.assertEqual(result["projects"]["meshbus"], result["firmware"])
            self.assertEqual(result["off_manifest"], ["zephyr"])

    def test_component_notices_preserve_attribution(self):
        text = art.notice_text(SCRIPTS / "meshbus/NOTICE")
        for attribution in ("Colin Percival", "Matthew Endsley", "Pieter-Jan Briers", "Erik Moqvist", "Stepan Koltsov"):
            self.assertIn(attribution, text)
        self.assertIn("Redistribution and use in source and binary forms", text)
        self.assertIn("CONSEQUENTIAL DAMAGES", text)
        self.assertIn("Permission is hereby granted", text)
        self.assertIn("Zeke Sikelianos", art.notice_text(SCRIPTS.parent.parent / "modules/lib/zui/src/dicts/LICENSE"))

    def test_notice_rejects_missing_empty_and_symlink(self):
        path = self.root / "NOTICE"
        with self.assertRaisesRegex(ValueError, "missing notice"):
            art.notice_text(path)
        path.write_text(" ")
        with self.assertRaisesRegex(ValueError, "empty notice"):
            art.notice_text(path)
        link = self.root / "link"
        link.symlink_to(path)
        with self.assertRaisesRegex(ValueError, "not a regular file"):
            art.notice_text(link)

    def test_client_archive_selects_runtime_and_generated_materials(self):
        workspace = self.root.resolve()
        repo = workspace / "meshbus"
        crate = repo / "scripts/meshbus"
        crate.mkdir(parents=True)
        shutil.copyfile(SCRIPTS.parent / "LICENSE", repo / "LICENSE")
        shutil.copytree(SCRIPTS.parent / "LICENSES", repo / "LICENSES")
        shutil.copyfile(SCRIPTS / "meshbus/NOTICE", crate / "NOTICE")
        (crate / "Cargo.lock").write_text("fixture lock\n")
        (workspace / "meshbus-cli").write_bytes(b"fixture CLI executable")
        (workspace / "meshbus.pb").write_bytes(b"fixture descriptors")
        proto = workspace / "schemas"
        proto.mkdir()
        (proto / "LICENSE").write_text("Schema copyright and permission\n")
        package = {"id": "meshbus-cli", "name": "meshbus-cli", "version": "1.0.0",
                   "license": "Apache-2.0", "manifest_path": str(crate / "Cargo.toml"), "targets": []}
        packages = [package]
        nodes = [{"id": "meshbus-cli", "deps": []}]
        for name, license_id, kind in (("clipboard-win", "BSL-1.0", None),
                                       ("protoc-bin-vendored", "MIT", "build")):
            dependency = workspace / name
            dependency.mkdir()
            packages.append({"id": name, "name": name, "version": "1.0.0", "license": license_id,
                             "manifest_path": str(dependency / "Cargo.toml"), "targets": []})
            nodes.append({"id": name, "deps": []})
            nodes[0]["deps"].append({"pkg": name, "dep_kinds": [{"kind": kind, "target": None}]})
        target = "aarch64-pc-windows-msvc"
        messages = [
            {"reason": "compiler-artifact", "package_id": "meshbus-cli",
             "executable": str(workspace / "meshbus-cli")},
            {"reason": "build-script-executed", "package_id": "meshbus-cli",
             "out_dir": str(workspace), "env": [["MESHBUS_BUILD_TARGET", target]]},
        ]
        args = argparse.Namespace(workspace=workspace, target=target,
                                  output=workspace / "output", cargo_target_dir=None)
        clean_source = {"firmware": {"dirty": False}, "projects": {}, "off_manifest": []}
        with patch.object(release, "run", side_effect=["host: x86_64-unknown-linux-gnu",
                          "\n".join(json.dumps(m) for m in messages),
                          json.dumps({"packages": packages, "resolve": {"nodes": nodes}})]), \
                patch.dict(os.environ, {"MESHBUS_PROTO_ROOT": str(proto)}), \
                patch.object(release, "provenance", return_value=clean_source):
            part = release.client(args)
        record = json.loads((part / "release-part.json").read_text())
        self.assertTrue(record["cross_compiled"])
        art.verify_checksums(part)
        with zipfile.ZipFile(next(part.glob("*.zip"))) as archive:
            self.assertEqual(archive.read("meshbus/THIRD-PARTY-NOTICES.txt"), (crate / "NOTICE").read_bytes())
            self.assertEqual(archive.read("meshbus/licenses/clipboard-win-1.0.0/BSL-1.0.txt"),
                             (repo / "LICENSES/BSL-1.0.txt").read_bytes())
            self.assertEqual(archive.read("meshbus/licenses/meshbus-protobufs/LICENSE"),
                             (proto / "LICENSE").read_bytes())
            self.assertIn("meshbus/licenses/meshbus-cli-1.0.0/Apache-2.0.txt", archive.namelist())
            self.assertFalse(any("licenses/protoc-bin-vendored" in name for name in archive.namelist()))
            self.assertEqual([p["name"] for p in json.loads(archive.read("meshbus/dependencies.json"))],
                             ["clipboard-win", "meshbus-cli"])
            self.assertEqual(json.loads(archive.read("meshbus/build-tools.json"))["packages"][0]["name"],
                             "protoc-bin-vendored")

    def test_matrix_identifies_one_firmware_per_device(self):
        expected = [{"id": "mesh_probe_r1", "board": "mesh_probe_r1/nrf52840"},
                    {"id": "mesh_probe_r2",
                     "board": "mesh_probe_r2/nrf54l15/cpuapp"},
                    {"id": "tracker_t1000_e", "board": "tracker_t1000_e/nrf52840"},
                    {"id": "wio_tracker_l1", "board": "wio_tracker_l1/nrf52840"}]
        self.assertEqual(release.targets(), expected)
        self.assertEqual(release.product_name(expected[0]), "mesh_probe_r1_nrf52840")

    def test_client_rejects_a_cargo_target_override_instead_of_mislabeling_archive(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary).resolve()
            package = {"id": "meshbus-cli", "manifest_path": str(workspace / "meshbus/scripts/meshbus/Cargo.toml")}
            messages = [
                {"reason": "compiler-artifact", "package_id": package["id"], "executable": "unexpected-binary"},
                {"reason": "build-script-executed", "package_id": package["id"], "out_dir": "unexpected-output",
                 "env": [["MESHBUS_BUILD_TARGET", "x86_64-unknown-linux-gnu"]]},
            ]
            args = argparse.Namespace(workspace=workspace, target=None, output=workspace / "output", cargo_target_dir=None)
            with patch.object(release, "run", side_effect=["host: aarch64-apple-darwin",
                              "\n".join(json.dumps(m) for m in messages), json.dumps({"packages": [package]})]), \
                    patch.object(release, "provenance", return_value={
                        "firmware": {"dirty": False}, "projects": {}, "off_manifest": []}):
                with self.assertRaisesRegex(ValueError, "different target"):
                    release.client(args)
            self.assertFalse(args.output.exists())

    def test_cli_fast_profile_is_development_only(self):
        self.assertEqual(release.client_profile(argparse.Namespace(development=True, profile="ci")), "ci")
        self.assertEqual(release.client_profile(argparse.Namespace()), "release")
        with self.assertRaisesRegex(ValueError, "release profile"):
            release.client_profile(argparse.Namespace(development=False, profile="ci"))

    def test_cli_candidate_rejects_dirty_sources_before_compiling(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = argparse.Namespace(workspace=Path(temporary), target=None,
                                      output=Path(temporary) / "out", cargo_target_dir=None)
            with patch.object(release, "run", return_value="host: x86_64-unknown-linux-gnu") as run, \
                    patch.object(release, "provenance", return_value={
                        "firmware": {"dirty": True}, "projects": {}, "off_manifest": []}):
                with self.assertRaisesRegex(ValueError, "clean committed source"):
                    release.client(args)
            self.assertEqual(run.call_count, 1)
            self.assertFalse(args.output.exists())

    def test_help_matrix_and_missing_cli_do_not_run_cargo(self):
        with patch.object(release.subprocess, "run", side_effect=AssertionError("unexpected subprocess")):
            with self.assertRaises(SystemExit) as error:
                release.add_arguments(argparse.ArgumentParser()).parse_args(["--help"])
            self.assertEqual(error.exception.code, 0)
            args = release.add_arguments(argparse.ArgumentParser()).parse_args(["matrix"])
            self.assertEqual(release.execute(args), 0)
            with patch.dict(os.environ, {"MESHBUS_CLI": "/nonexistent/meshbus"}):
                with self.assertRaisesRegex(RuntimeError, "does not name"):
                    meshbus_cli.cli_command()

    def test_production_format_tool_requires_an_explicit_absolute_path(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "require MESHBUS_CLI"):
                meshbus_cli.cli_command(require_explicit=True)
        with patch.dict(os.environ, {"MESHBUS_CLI": "meshbus"}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "absolute executable path"):
                meshbus_cli.cli_command(require_explicit=True)

    def test_bridge_preserves_arguments_and_exit_code(self):
        spec = importlib.util.spec_from_file_location("west_meshbus_adapter", SCRIPTS / "meshbus/meshbus.py")
        bridge = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(bridge)
        command = bridge.Meshbus()
        parser = argparse.ArgumentParser()
        command.do_add_parser(parser.add_subparsers(dest="command"))
        arguments = ["llext", "--llext-sdk", "EDK with spaces", "extension", "--", "-DTEST=a b"]
        parsed, unknown = parser.parse_known_args(["meshbus", *arguments])
        with patch.object(bridge, "cli_command", return_value=["CLI with spaces"]) as resolve:
            with patch.object(bridge.subprocess, "run", return_value=subprocess.CompletedProcess([], 7)) as invoked:
                with self.assertRaises(SystemExit) as error:
                    command.do_run(parsed, unknown)
                self.assertEqual(error.exception.code, 7)
                self.assertEqual(invoked.call_args.args[0], ["CLI with spaces", *arguments])
                resolve.assert_called_once_with(auto_build=True)

    def test_board_id_and_qualified_target_select_the_same_device_firmware(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary)
            (workspace / "meshbus/apps/meshbus").mkdir(parents=True)
            (workspace / "meshbus/west.yml").write_text("manifest: {}\n")
            (workspace / "meshbus/apps/meshbus/VERSION").write_text("VERSION_MAJOR = 1\n")
            copy_board_metadata(workspace)
            image_key = workspace / "development-ed25519.pem"
            image_key.write_text("test key placeholder\n")
            image_key.chmod(0o600)
            args = argparse.Namespace(workspace=workspace, target=[],
                                      build_root=workspace / "build", output=workspace / "parts",
                                      development=True, image_signing_key=image_key)
            directories = []
            for selector in ["mesh_probe_r2", "mesh_probe_r2/nrf54l15/cpuapp"]:
                args.target = [selector]
                with patch.object(release, "run") as invoked, patch.object(release, "firmware") as packaged, \
                        patch.object(release, "imgtool", return_value=Path("imgtool.py")), \
                        patch.object(release, "config", return_value=SIGNED_CONFIG):
                    release.build_products(args)
                self.assertEqual(invoked.call_count, 3)
                packaged.assert_called_once()
                command = invoked.call_args_list[0].args[0]
                self.assertIn("--sysbuild", command)
                self.assertIn(workspace.resolve() / "meshbus/apps/meshbus", command)
                self.assertEqual(command[command.index("-b") + 1], "mesh_probe_r2/nrf54l15/cpuapp")
                self.assertIn("-DCONFIG_BUILD_OUTPUT_META=y", command)
                self.assertIn("-Dmcuboot_CONFIG_BUILD_OUTPUT_META=y", command)
                directories.append(command[command.index("-d") + 1])
            self.assertEqual(directories[0], directories[1])
            self.assertEqual(directories[0].name, "mesh_probe_r2_nrf54l15_cpuapp")

    def test_native_build_rejects_authentication_without_a_key_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary)
            (workspace / "meshbus/apps/meshbus").mkdir(parents=True)
            (workspace / "meshbus/west.yml").write_text("manifest: {}\n")
            (workspace / "meshbus/apps/meshbus/VERSION").write_text("VERSION_MAJOR = 1\n")
            copy_board_metadata(workspace)
            args = argparse.Namespace(workspace=workspace, target=[], build_root=workspace / "build",
                                      output=workspace / "parts", development=False, image_signing_key=None)
            with patch.object(release, "run"), \
                    patch.object(release, "config", return_value=SIGNED_CONFIG):
                with self.assertRaisesRegex(ValueError, "configured authentication differs"):
                    release.build_products(args)


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="device discovery ")
        self.addCleanup(self.temp.cleanup)
        self.workspace = Path(self.temp.name)
        self.boards = self.workspace / "meshbus/apps/meshbus/boards"
        self.boards.mkdir(parents=True)
        (self.workspace / "zephyr").symlink_to(SCRIPTS.parent.parent / "zephyr", target_is_directory=True)

    def test_discovery_tracks_app_profiles_and_ignores_companions(self):
        directory = add_profile(self.workspace, "a_board")
        add_profile(self.workspace, "b_board")
        for suffix in [".overlay", "_mcuboot.conf", "_mcuboot.overlay"]:
            (directory / ("a_board_soc_cpu" + suffix)).write_text("# companion\n")
        (directory / "README.md").write_text("documentation\n")
        a = {"id": "a_board", "board": "a_board/soc/cpu"}
        b = {"id": "b_board", "board": "b_board/soc/cpu"}
        self.assertEqual(release.targets(self.boards), [a, b])
        shutil.rmtree(directory)
        self.assertEqual(release.targets(self.boards), [b])

    def test_multiple_qualifiers_have_distinct_output_names(self):
        add_profile(self.workspace, qualifiers=("soc/cpu", "soc/other_cpu"))
        products = release.targets(self.boards)
        self.assertEqual([p["board"] for p in products],
                         ["fixture_board/soc/cpu", "fixture_board/soc/other_cpu"])
        self.assertEqual(len({release.product_name(p) for p in products}), 2)

    def test_vendor_mismatch_is_rejected(self):
        directory = add_profile(self.workspace)
        destination = self.boards / "wrong/fixture_board"
        destination.parent.mkdir()
        directory.rename(destination)
        with self.assertRaisesRegex(ValueError, "vendor mismatch"):
            release.targets(self.boards)

    def test_unknown_qualifier_and_role_qualifier_are_rejected(self):
        directory = add_profile(self.workspace)
        profile = directory / "fixture_board_soc_cpu.conf"
        profile.rename(directory / "fixture_board_soc_unknown.conf")
        with self.assertRaisesRegex(ValueError, "exactly one ordinary"):
            release.targets(self.boards)
        shutil.rmtree(directory)
        add_profile(self.workspace, qualifiers=("soc/cpu/mb_client",))
        with self.assertRaisesRegex(ValueError, "exactly one ordinary"):
            release.targets(self.boards)

    def test_normalized_qualifier_collision_is_rejected(self):
        add_profile(self.workspace, qualifiers=("soc/cpu_variant", "soc/cpu/variant"))
        with self.assertRaisesRegex(ValueError, "exactly one ordinary"):
            release.targets(self.boards)

    def test_output_collision_across_boards_is_rejected(self):
        add_profile(self.workspace, name="a_b", qualifiers=("soc/cpu",))
        add_profile(self.workspace, name="a", qualifiers=("b/soc/cpu",))
        # Both SoCs must exist in the shared fixture hardware model.
        art.write_json(self.workspace / "meshbus/soc/test/soc.yml",
                       {"socs": [{"name": "soc"}, {"name": "b"}]})
        with self.assertRaisesRegex(ValueError, "output collision"):
            release.targets(self.boards)

    def test_unknown_board_is_rejected(self):
        add_profile(self.workspace)
        shutil.rmtree(self.workspace / "meshbus/boards/test/fixture_board")
        with self.assertRaisesRegex(ValueError, "unknown board"):
            release.targets(self.boards)

    def test_empty_flat_deep_and_orphan_profiles_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "no Meshbus APP profiles"):
            release.targets(self.boards)
        flat = self.boards / "fixture_board_soc_cpu.conf"
        flat.write_text("")
        with self.assertRaisesRegex(ValueError, "boards/<vendor>/<board>"):
            release.targets(self.boards)
        flat.unlink()
        directory = add_profile(self.workspace)
        deep = directory / "nested"
        deep.mkdir()
        extra = deep / "extra.conf"
        extra.write_text("")
        with self.assertRaisesRegex(ValueError, "boards/<vendor>/<board>"):
            release.targets(self.boards)
        extra.unlink()
        (directory / "fixture_board_soc_other_mcuboot.overlay").write_text("")
        with self.assertRaisesRegex(ValueError, "no matching APP profile"):
            release.targets(self.boards)

    def test_symlink_profiles_are_rejected(self):
        directory = add_profile(self.workspace)
        (directory / "alias.conf").symlink_to(directory / "fixture_board_soc_cpu.conf")
        with self.assertRaisesRegex(ValueError, "symlink"):
            release.targets(self.boards)

    def test_build_uses_selected_workspace_profiles(self):
        add_profile(self.workspace, "sample_board")
        (self.workspace / "meshbus/west.yml").write_text("manifest: {}\n")
        (self.workspace / "meshbus/apps/meshbus/VERSION").write_text("VERSION_MAJOR = 1\n")
        key = self.workspace / "test-key.pem"
        key.write_text("test placeholder\n")
        key.chmod(0o600)
        args = argparse.Namespace(workspace=self.workspace, target=[],
                                  build_root=self.workspace / "build", output=self.workspace / "parts",
                                  development=True, image_signing_key=key)
        with patch.object(release, "run") as invoked, patch.object(release, "firmware"), \
                patch.object(release, "imgtool", return_value=Path("imgtool.py")), \
                patch.object(release, "config", return_value=SIGNED_CONFIG):
            release.build_products(args)
        command = invoked.call_args_list[0].args[0]
        self.assertEqual(command[command.index("-b") + 1], "sample_board/soc/cpu")


if __name__ == "__main__":
    unittest.main()
