# SPDX-License-Identifier: Apache-2.0
"""Exercise product artifacts and command boundaries without building firmware."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SCRIPTS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SCRIPTS / "release"))
import release
import artifacts as art
import meshbus_cli


def copy_board_metadata(workspace):
    destination = workspace / "meshbus/apps/meshbus/boards/products.yml"
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(SCRIPTS.parent / "apps/meshbus/boards/products.yml", destination)


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="release tests ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        copy_board_metadata(self.root)

    def product(self, llext=False, oversize=False, dirty=True, off_manifest=False):
        art.write_json(self.root / "meshbus/apps/meshbus/boards/products.yml", {"products": [
            {"id": "fixture_board", "board": "fixture_board/soc/cpu"}]})
        sysbuild = self.root / "sysbuild"
        build = sysbuild / "meshbus"
        for name, base, data in [("mcuboot", 0x1000, b"boot"), ("meshbus", 0x2000, b"application")]:
            directory = sysbuild / name / "zephyr"
            directory.mkdir(parents=True)
            (directory / ".config").write_text(
                f"CONFIG_FLASH_BASE_ADDRESS=0\nCONFIG_FLASH_LOAD_OFFSET={base}\n"
                f"CONFIG_FLASH_LOAD_SIZE={1 if oversize else 4096}\nCONFIG_ROM_START_OFFSET=0x20\n")
            (directory / "zephyr.dts").write_text("/dts-v1/;\n")
            (directory / "runners.yaml").write_text("config:\n  bin_file: zephyr.signed.bin\n")
            (directory / "zephyr.signed.bin").write_bytes(data)
        (sysbuild / "domains.yaml").write_text("domains: []\n")
        (self.root / "meshbus/LICENSE").write_text("test license\n")
        source = {"firmware": {"revision": None if dirty else "f" * 40, "dirty": dirty},
                  "off_manifest": ["meshbus"] if off_manifest else [],
                  "projects": {"meshbus": {"revision": "a" * 40, "dirty": False}}}
        conf = {"CONFIG_MESHBUS_FIRMWARE": "y", "CONFIG_MESHBUS_LLEXT": "y" if llext else "n"}
        ctx = (build, {"cmake": {"toolchain": {"name": "zephyr"}}}, conf,
               "fixture_board/soc/cpu", "1.2.3", self.root / "meshbus", source)
        self.enterContext(patch.object(release, "context", return_value=ctx))
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
        self.assertEqual((first / "app.bin").read_bytes(), b"application")
        with tarfile.open(one) as archive:
            merged = archive.extractfile("firmware/full.bin").read()
            self.assertEqual(merged[:4], b"boot")
            self.assertEqual(merged[4:4096], b"\xff" * 4092)
            self.assertEqual(merged[4096:], b"application")
            mapping = json.load(archive.extractfile("firmware/flash-map.json"))
            self.assertEqual(mapping["full_bin"]["address"], 0x1000)
            self.assertEqual(mapping["id"], "fixture_board")
            self.assertNotIn("role", mapping)
            self.assertEqual(first.name, "1.2.3-fixture_board")
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
        with self.assertRaisesRegex(ValueError, "production C2 package requires"):
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
                    "target": "idea_mesh_tracker_c2/nrf54l15/cpuapp",
                    "version": "1.2.3"}
        with patch.object(release, "run", side_effect=generate) as invoked:
            result = release.generate_spdx(app, sysbuild, output, identity, self.root, True)
        self.assertEqual(invoked.call_count, 2)
        self.assertEqual(result["status"], "generated")
        self.assertEqual(len(result["documents"]), 1)
        self.assertEqual(result["private_retention"]["documents"], 8)
        public = (output / "SBOM.spdx").read_text()
        self.assertIn("PackageName: meshbus-sdk", public)
        self.assertIn("PackageVersion: 1.2.3", public)
        self.assertIn("github.com/zephyrproject-rtos/zephyr@" + "b" * 40, public)
        self.assertNotIn("github.com/Meshbus/sdk-meshbus", public)
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
        for target in release.release_targets():
            part = source / "firmware" / f"1.2.3-{target['id']}"
            part.mkdir(parents=True)
            image = target["board"].encode()
            (part / "app.bin").write_bytes(image)
            record = {"schema": 1, "kind": "firmware", "publishable": False,
                      "id": target["id"], "target": target["board"], "version": "1.2.3",
                      "images": [{"domain": "app", "sha256": art.digest(image)}],
                      "capabilities": {"llext": False, "firmware_endpoint": False}}
            art.write_json(part / "release-part.json", record)
            archive = part / "test-firmware.tar.gz"
            archive.write_bytes(b"archive fixture")
            art.sidecar(archive)
            art.checksums(part)
        return argparse.Namespace(input=source, output=self.root / "assembled", delta_package=[],
                                  manifest_public_key=None, image_public_key=None)

    def test_assembly_requires_only_the_c2_firmware_target(self):
        args = self.assembly_parts()
        with patch.object(release, "cli_command", side_effect=AssertionError("no format tools needed")):
            release.assemble(args)
        art.verify_checksums(args.output)
        index = json.loads((args.output / "release.json").read_text())
        self.assertEqual([record["target"] for record in index["products"]],
                         ["idea_mesh_tracker_c2/nrf54l15/cpuapp"])
        self.assertFalse(index["publishable"])

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

    def test_assembly_requires_the_c2_target(self):
        args = self.assembly_parts()
        shutil.rmtree(args.input / "firmware/1.2.3-idea_mesh_tracker_c2")
        with self.assertRaisesRegex(ValueError, "missing required firmware targets"):
            release.assemble(args)
        self.assertFalse(args.output.exists())

    def test_assembly_rejects_wrong_device_identity_and_role_field(self):
        args = self.assembly_parts()
        part = args.input / "firmware/1.2.3-idea_mesh_tracker_c2"
        path = part / "release-part.json"
        record = json.loads(path.read_text())
        for field, value in [("id", "c2-client"), ("role", "room")]:
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

    def test_native_build_preserves_key_and_exports_public_pem(self):
        args = argparse.Namespace(workspace=self.root, target=[], build_root=self.root / "build",
                                  output=self.root / "parts", development=False, image_signing_key=None)
        real_run = release.run
        keys = []

        def run(command, **kwargs):
            if command[:2] == ["west", "build"]:
                definition = next(str(arg) for arg in command if str(arg).startswith("-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="))
                key = Path(definition.split("=", 1)[1].strip('"'))
                self.assertTrue(key.read_bytes() == self.private.read_bytes())
                keys.append(key)
                return None
            return real_run(command, **kwargs)

        def package(*_):
            self.assertTrue(keys[-1].exists(), "key must remain available during EDK export")

        args.image_signing_key = self.private
        with patch.object(release, "run", side_effect=run), \
                patch.object(release, "firmware", side_effect=package):
            release.build_products(args)
        self.assertEqual(keys, [self.private.resolve()])
        self.assertTrue(self.private.exists())
        public = next(args.build_root.rglob("image-public.pem"))
        self.assertEqual(public.read_bytes(), self.public.read_bytes())

    def test_native_build_failure_preserves_caller_key(self):
        args = argparse.Namespace(workspace=self.root, target=[], build_root=self.root / "build",
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


class EntryTests(unittest.TestCase):
    def test_signing_key_argument_rejects_pem_contents_without_echoing_them(self):
        with self.assertRaises(ValueError) as raised:
            release.image_private_key("-----BEGIN PRIVATE KEY-----\nnot-a-real-key\n", Path("unused-build").resolve())
        self.assertNotIn("not-a-real-key", str(raised.exception))

    def test_current_and_legacy_sysbuild_domains_and_direct_image_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("meshbus", "app"):
                sysbuild = root / name
                image = sysbuild / name
                (image / "zephyr").mkdir(parents=True)
                (image / "zephyr/.config").write_text("CONFIG_TEST=y\n")
                self.assertEqual(release.app_build(sysbuild), image)
                self.assertEqual(release.app_build(image), image)

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

    def test_licenses_use_source_repository_without_outer_license(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary)
            repo = workspace / "meshbus"
            licenses = repo / "scripts/meshbus/assets/licenses"
            licenses.mkdir(parents=True)
            (repo / "LICENSE").write_text("Apache test license\n")
            (licenses / "protoc-bin-vendored-MIT.txt").write_text("MIT test license\n")
            packages = []
            for name, license_id in (("meshbus-cli", "Apache-2.0"),
                                     ("protoc-bin-vendored", "MIT")):
                crate = workspace / "crates" / name
                crate.mkdir(parents=True)
                packages.append({"name": name, "version": "1.0.0", "license": license_id,
                                 "manifest_path": str(crate / "Cargo.toml")})
            output = workspace / "output"
            art.collect_licenses(repo, output, {"packages": packages})
            self.assertEqual((output / "licenses/meshbus-cli-1.0.0/Apache-2.0.txt").read_text(),
                             "Apache test license\n")
            self.assertEqual((output / "licenses/protoc-bin-vendored-1.0.0/protoc-bin-vendored-MIT.txt").read_text(),
                             "MIT test license\n")

    def test_matrix_identifies_one_firmware_per_device(self):
        expected = [{"id": "idea_mesh_tracker_c2",
                     "board": "idea_mesh_tracker_c2/nrf54l15/cpuapp"}]
        self.assertEqual(release.targets(), expected)
        self.assertEqual(release.release_targets(), expected)
        self.assertEqual(release.product_name(expected[0]), "idea_mesh_tracker_c2")

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
                              "\n".join(json.dumps(m) for m in messages), json.dumps({"packages": [package]})]):
                with self.assertRaisesRegex(ValueError, "different target"):
                    release.client(args)
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
        with patch.object(bridge, "cli_command", return_value=["CLI with spaces"]):
            with patch.object(bridge.subprocess, "run", return_value=subprocess.CompletedProcess([], 7)) as invoked:
                with self.assertRaises(SystemExit) as error:
                    command.do_run(parsed, unknown)
                self.assertEqual(error.exception.code, 7)
                self.assertEqual(invoked.call_args.args[0], ["CLI with spaces", *arguments])

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
            for selector in ["idea_mesh_tracker_c2", "idea_mesh_tracker_c2/nrf54l15/cpuapp"]:
                args.target = [selector]
                with patch.object(release, "run") as invoked, patch.object(release, "firmware") as packaged, \
                        patch.object(release, "imgtool", return_value=Path("imgtool.py")):
                    release.build_products(args)
                self.assertEqual(invoked.call_count, 2)
                packaged.assert_called_once()
                command = invoked.call_args_list[0].args[0]
                self.assertIn("--sysbuild", command)
                self.assertIn(workspace.resolve() / "meshbus/apps/meshbus", command)
                self.assertEqual(command[command.index("-b") + 1], "idea_mesh_tracker_c2/nrf54l15/cpuapp")
                self.assertIn("-DCONFIG_BUILD_OUTPUT_META=y", command)
                self.assertIn("-Dmcuboot_CONFIG_BUILD_OUTPUT_META=y", command)
                directories.append(command[command.index("-d") + 1])
            self.assertEqual(directories[0], directories[1])
            self.assertEqual(directories[0].name, "idea_mesh_tracker_c2")

    def test_native_build_requires_a_file_even_with_legacy_environment_set(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary)
            (workspace / "meshbus/apps/meshbus").mkdir(parents=True)
            (workspace / "meshbus/west.yml").write_text("manifest: {}\n")
            (workspace / "meshbus/apps/meshbus/VERSION").write_text("VERSION_MAJOR = 1\n")
            copy_board_metadata(workspace)
            args = argparse.Namespace(workspace=workspace, target=[], build_root=workspace / "build",
                                      output=workspace / "parts", development=False, image_signing_key=None)
            with patch.dict(os.environ, {"MESHBUS_IMAGE_PRIVATE_KEY": "unused legacy value"}):
                with self.assertRaisesRegex(ValueError, "requires --image-signing-key"):
                    release.build_products(args)


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="device discovery ")
        self.addCleanup(self.temp.cleanup)
        self.workspace = Path(self.temp.name)
        self.boards = self.workspace / "meshbus/apps/meshbus/boards"
        self.boards.mkdir(parents=True)

    def profiles(self, products):
        path = self.boards / "products.yml"
        art.write_json(path, {"products": products})
        return path

    def test_discovery_tracks_device_profiles(self):
        a = {"id": "a_board", "board": "a_board/soc/cpu"}
        b = {"id": "b_board", "board": "b_board/soc/cpu"}
        self.profiles([b, a])
        self.assertEqual(release.targets(self.boards), [a, b])
        self.profiles([b])
        self.assertEqual(release.targets(self.boards), [b])

    def test_duplicate_device_outputs_fail(self):
        a = {"id": "sample_board", "board": "sample_board/soc/cpu"}
        for duplicate in [a, {**a, "board": "sample_board/soc/other_cpu"}]:
            with self.subTest(duplicate=duplicate):
                self.profiles([a, duplicate])
                with self.assertRaisesRegex(ValueError, "duplicate device identity"):
                    release.targets(self.boards)

    def test_empty_invalid_and_role_qualified_metadata_fail(self):
        for products in [[], [{"id": "sample_board", "board": "sample_board"}],
                         [{"id": "sample_board", "board": "sample_board/../cpu"}],
                         [{"id": "sample_board", "board": "other_board/soc/cpu"}],
                         [{"id": "sample_board", "board": "sample_board/soc/cpu/mb_client"}],
                         [{"id": "sample_board", "board": "sample_board/soc/cpu", "role": "chat"}]]:
            with self.subTest(products=products):
                self.profiles(products)
                with self.assertRaises(ValueError):
                    release.targets(self.boards)
        path = self.profiles([])
        path.write_text("products: [broken\n")
        with self.assertRaisesRegex(ValueError, "invalid YAML"):
            release.targets(self.boards)

    def test_build_uses_the_selected_workspaces_device_metadata(self):
        self.profiles([{"id": "sample_board", "board": "sample_board/soc/cpu"}])
        (self.workspace / "meshbus/west.yml").write_text("manifest: {}\n")
        (self.workspace / "meshbus/apps/meshbus/VERSION").write_text("VERSION_MAJOR = 1\n")
        args = argparse.Namespace(workspace=self.workspace, target=["sample_board"],
                                  build_root=self.workspace / "build", output=self.workspace / "parts", development=True)
        with patch.object(release, "run") as invoked, patch.object(release, "firmware"):
            release.build_products(args)
        invoked.assert_called_once()
        command = invoked.call_args.args[0]
        self.assertEqual(command[command.index("-b") + 1], "sample_board/soc/cpu")


if __name__ == "__main__":
    unittest.main()
