# SPDX-License-Identifier: Apache-2.0
"""Exercise the product's CMake key admission without reading or signing keys."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CMAKE = shutil.which("cmake")


@unittest.skipUnless(CMAKE, "CMake is required for sysbuild policy checks")
class ImageSigningPolicyTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="meshbus key policy ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.mcuboot = self.root / "mcuboot"
        self.mcuboot.mkdir()
        for name in ("root-rsa-2048.pem", "root-ec-p256.pem", "root-ed25519.pem"):
            (self.mcuboot / name).touch()
        self.owner = self.root / "owner" / "root-ed25519.pem"
        self.owner.parent.mkdir()
        self.owner.touch()
        profile = self.root / "app/cmake/board_profile.cmake"
        profile.parent.mkdir(parents=True)
        profile.write_text(
            "function(meshbus_board_profile app_dir output)\n"
            '  set(${output} "${app_dir}/boards/fixture.conf" PARENT_SCOPE)\n'
            "endfunction()\n")

    def configure(self, key, *, mcuboot=True, unsigned=False):
        script = self.root / "policy.cmake"
        script.write_text(
            "cmake_minimum_required(VERSION 3.20)\n"
            f'set(APP_DIR "{self.root.as_posix()}/app")\n'
            f'set(ZEPHYR_MCUBOOT_MODULE_DIR "{self.mcuboot.as_posix()}")\n'
            f'set(OWNER_KEY "{self.owner.as_posix()}")\n'
            f"set(SB_CONFIG_BOOTLOADER_MCUBOOT {int(mcuboot)})\n"
            f"set(SB_CONFIG_BOOT_SIGNATURE_TYPE_NONE {int(unsigned)})\n"
            f"set(SB_CONFIG_BOOT_SIGNATURE_KEY_FILE [==[{key}]==])\n"
            # Board discovery and profile forwarding do not participate in key admission.
            "function(zephyr_file)\nendfunction()\n"
            f'include("{ROOT.as_posix()}/apps/meshbus/sysbuild.cmake")\n')
        return subprocess.run([CMAKE, "-P", str(script)], cwd=self.root,
                              capture_output=True, text=True, check=False)

    def assert_example_rejected(self, key):
        result = self.configure(key)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("not an MCUboot example key", " ".join(result.stderr.split()))

    def test_example_keys_are_rejected_after_variable_expansion(self):
        for name in ("root-rsa-2048.pem", "root-ec-p256.pem", "root-ed25519.pem"):
            for directory in (self.mcuboot.as_posix(), "${ZEPHYR_MCUBOOT_MODULE_DIR}",
                              "@ZEPHYR_MCUBOOT_MODULE_DIR@"):
                with self.subTest(name=name, directory=directory):
                    self.assert_example_rejected(f"{directory}/{name}")

    def test_example_key_symlink_is_rejected(self):
        link = self.root / "alias.pem"
        link.symlink_to(self.mcuboot / "root-ed25519.pem")
        self.assert_example_rejected(link.as_posix())

    def test_example_keys_are_rejected_in_each_list_position(self):
        example = "${ZEPHYR_MCUBOOT_MODULE_DIR}/root-ed25519.pem"
        for key in (f"  {example}  ", f"${{OWNER_KEY}}, {example}",
                    f" {example} ,${{OWNER_KEY}}"):
            with self.subTest(key=key):
                self.assert_example_rejected(key)

    def test_owner_paths_and_lists_remain_accepted(self):
        for key in (self.owner.as_posix(), "${OWNER_KEY}", "@OWNER_KEY@",
                    " ${OWNER_KEY} , ${OWNER_KEY} "):
            with self.subTest(key=key):
                result = self.configure(key)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_authenticated_build_still_requires_an_explicit_key(self):
        result = self.configure("")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires an explicit image key", result.stderr)

    def test_unsigned_and_non_mcuboot_builds_do_not_require_keys(self):
        for mcuboot, unsigned in ((True, True), (False, False)):
            with self.subTest(mcuboot=mcuboot, unsigned=unsigned):
                result = self.configure("", mcuboot=mcuboot, unsigned=unsigned)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
