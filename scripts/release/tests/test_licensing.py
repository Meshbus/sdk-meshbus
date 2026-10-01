# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Distribution selection, attribution and fail-closed packaging checks."""
import json
from pathlib import Path
import sys
import tempfile
import tarfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import artifacts as art
import licensing


class LicensingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.output = self.root / "output"

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def test_cargo_selection_excludes_dev_tools_and_unreachable_platforms(self):
        names = ["cli", "runtime", "shared", "build", "macro", "macro-child", "dev", "other-platform"]
        packages = [{"id": name, "name": name, "targets": [{"kind": ["proc-macro" if name == "macro" else "lib"]}]}
                    for name in names]
        nodes = {name: {"id": name, "deps": []} for name in names}
        for parent, child, kind in [("cli", "runtime", None), ("cli", "build", "build"),
                                    ("cli", "macro", None), ("cli", "dev", "dev"),
                                    ("runtime", "shared", None), ("build", "shared", None),
                                    ("macro", "macro-child", None)]:
            nodes[parent]["deps"].append({"pkg": child, "dep_kinds": [{"kind": kind}]})
        runtime, tools = licensing.cargo_packages({"packages": packages, "resolve": {"nodes": list(nodes.values())}}, "cli")
        self.assertEqual({p["name"] for p in runtime if p["distribution_scope"] == "runtime"}, {"cli", "runtime", "shared"})
        self.assertEqual({p["name"] for p in runtime if p["distribution_scope"] == "code-generator"}, {"macro", "macro-child"})
        self.assertEqual({p["name"] for p in tools}, {"build"})
        with self.assertRaisesRegex(ValueError, "resolve graph missing"):
            licensing.cargo_packages({"packages": packages}, "cli")

    def test_component_collects_nested_license_and_source_copyright(self):
        root = self.root / "component"
        self.write("component/LICENSE", "Main grant")
        self.write("component/src/vendor/LICENSE", "Vendor grant")
        self.write("component/src/vendor/a.c", "/* Copyright Original Author\nPermission to use. */\nint x;")
        self.write("component/tests/LICENSE", "Unselected test license")
        entry = licensing.copy_component(root, self.output, "component", ["./src/vendor/a.c"])
        self.assertIn("licenses/component/src/vendor/LICENSE", entry["materials"])
        self.assertNotIn("licenses/component/tests/LICENSE", entry["materials"])
        self.assertIn("Original Author", (self.output / "licenses/component/SOURCE-NOTICES.txt").read_text())
        self.assertNotIn("int x", (self.output / "licenses/component/SOURCE-NOTICES.txt").read_text())

    def test_component_rejects_missing_notice_and_escaping_path(self):
        root = self.root / "component"
        root.mkdir()
        with self.assertRaisesRegex(ValueError, "no license materials"):
            licensing.copy_component(root, self.output, "component")
        self.write("component/LICENSE", "Permission")
        with self.assertRaisesRegex(ValueError, "unsafe artifact path"):
            licensing.copy_component(root, self.output, "component", ["../outside.c"])
        self.write("outside.c", "Private")
        (root / "link.c").symlink_to(self.root / "outside.c")
        with self.assertRaisesRegex(ValueError, "escapes component"):
            licensing.copy_component(root, self.output, "component", ["link.c"])

    def test_cargo_honors_nested_license_file_and_cli_notice_needs_apache(self):
        self.write("repo/LICENSE", "Meshbus Apache terms")
        self.write("repo/scripts/meshbus/NOTICE", "Derived BSD notice")
        self.write("crate/legal/terms.txt", "Author and complete grant")
        packages = [{"name": "meshbus-cli", "version": "1", "license": "Apache-2.0",
                     "manifest_path": str(self.root / "repo/scripts/meshbus/Cargo.toml")},
                    {"name": "crate", "version": "1", "license_file": "legal/terms.txt",
                     "manifest_path": str(self.root / "crate/Cargo.toml")}]
        art.collect_licenses(self.root / "repo", self.output, packages)
        self.assertEqual((self.output / "licenses/meshbus-cli-1/Apache-2.0.txt").read_text(), "Meshbus Apache terms")
        self.assertEqual((self.output / "licenses/crate-1/legal/terms.txt").read_text(), "Author and complete grant")

    def test_cargo_missing_license_fails(self):
        (self.root / "crate").mkdir()
        with self.assertRaisesRegex(ValueError, "has no license text"):
            art.collect_licenses(self.root, self.output, [{"name": "unknown", "version": "1", "license": "MIT",
                                                         "manifest_path": str(self.root / "crate/Cargo.toml")}])

    def test_combined_notice_deduplicates_terms_retains_authors_and_mpl_source(self):
        self.write("output/licenses/a-1/LICENSE", "Complete shared permission\n")
        self.write("output/licenses/b-2/LICENSE", "Complete shared permission\n")
        self.write("output/licenses/b-2/NOTICE", "Copyright Original Author\n")
        self.write("output/REUSE.toml", "metadata, not permission")
        art.write_json(self.output / "dependencies.json", [
            {"name": "b", "version": "2", "license": "MPL-2.0"}])
        text = licensing.combined_notice(self.output).decode()
        self.assertEqual(text.count("Complete shared permission"), 1)
        self.assertIn("licenses/a-1/LICENSE", text)
        self.assertIn("licenses/b-2/LICENSE", text)
        self.assertIn("Copyright Original Author", text)
        self.assertIn("https://crates.io/api/v1/crates/b/2/download", text)
        self.assertNotIn("metadata, not permission", text)

    def test_edk_repack_preserves_empty_compiler_include_directory(self):
        (self.output / "include/empty").mkdir(parents=True)
        self.write("output/include/header.h", "/* Copyright Header Author */")
        archive = self.root / "edk.tar.xz"
        art.pack(self.output, archive, "llext-edk")
        with tarfile.open(archive) as stream:
            self.assertTrue(stream.getmember("llext-edk/include/empty").isdir())

    def test_edk_does_not_silently_drop_complex_license_obligations(self):
        marker = "SPDX" + "-License-Identifier: "
        for expression in ('Apache-2.0 AND (MIT OR BSD-2-Clause)', 'Apache-2.0 AND MIT OR BSD-3-Clause'):
            self.write('output/include/a.h', f'/* {marker}{expression} */')
            with self.assertRaisesRegex(ValueError, 'unsupported exported-header license expression'):
                licensing.edk_license_ids(self.output)

    def test_font_terms_exclude_unselected_restricted_license(self):
        module = self.font_fixture()
        path = module / "fonts/sources.json"
        records = json.loads(path.read_text())
        name = "u8g2-texts-cc-by-nc-sa-3-0-txt"
        text = "Unselected noncommercial terms"
        self.write(f"u8g2/fonts/notices/{name}.txt", text)
        records["notices"].append({"id": name, "sha256": art.digest(text.encode())})
        art.write_json(path, records)
        licensing.font_notices(module, {"u8g2_font_alpha_tr"}, self.output)
        self.assertFalse((self.output / "licenses/u8g2/fonts" / (name + ".txt")).exists())

    def test_selected_font_requires_its_supplemental_terms(self):
        module = self.font_fixture()
        path = module / "fonts/catalog.json"
        catalog = json.loads(path.read_text())
        catalog["groups"]["group"]["license"] = "CC-BY-SA-3.0"
        art.write_json(path, catalog)
        with self.assertRaisesRegex(ValueError, 'missing supplemental font terms'):
            licensing.font_notices(module, {"u8g2_font_alpha_tr"}, self.output)

    def font_fixture(self):
        self.write("u8g2/LICENSE", "BSD U8g2 core")
        catalog = {"families": {"alpha": {"group": "group"}, "beta": {"group": "unused"}},
                   "groups": {"group": {"notice": "alpha", "license": "MIT", "status": "documented"},
                              "unused": {"notice": "beta", "license": "other", "status": "review"}}}
        self.write("u8g2/fonts/catalog.json", json.dumps(catalog))
        records = []
        for name, text in [("alpha", "Alpha attribution and permission"), ("beta", "Unselected font"),
                           ("u8g2-texts-mit", "Complete MIT terms")]:
            self.write(f"u8g2/fonts/notices/{name}.txt", text)
            records.append({"id": name, "sha256": art.digest(text.encode())})
        self.write("u8g2/fonts/sources.json", json.dumps({"notices": records}))
        self.write("u8g2/src/u8g2_fonts.c", "/*\nFontname: Alpha\nCopyright: Font Author\n*/\n"
                   "const uint8_t u8g2_font_alpha_tr[1] = {0};\n")
        return self.root / "u8g2"

    def test_fonts_select_families_keep_authors_and_verify_upstream_hashes(self):
        module = self.font_fixture()
        entries = licensing.font_notices(module, {"u8g2_font_alpha_tr"}, self.output)
        self.assertIn("Font Author", entries[0]["attribution"])
        destination = self.output / "licenses/u8g2/fonts"
        self.assertTrue((destination / "alpha.txt").is_file())
        self.assertTrue((destination / "u8g2-texts-mit.txt").is_file())
        self.assertFalse((destination / "beta.txt").exists())
        self.write("u8g2/fonts/notices/alpha.txt", "truncated")
        with self.assertRaisesRegex(ValueError, "changed font notice"):
            licensing.font_notices(module, {"u8g2_font_alpha_tr"}, self.output)
        with self.assertRaisesRegex(ValueError, "unmapped selected font"):
            licensing.font_notices(module, {"u8g2_font_unknown_tr"}, self.output)

    def test_spdx_paths_use_each_component_base_and_both_images(self):
        self.write("spdx/app/app.spdx", "##### Package: app\nPackageName: app-sources\nFileName: ./src/main.c\n")
        self.write("spdx/app/zephyr.spdx", "##### Package: zephyr\nPackageName: zephyr-sources\n"
                   "FileName: ./zephyr/kernel/main.c\n##### Package: unused\nPackageName: unused-sources\n")
        self.write("spdx/mcuboot/app.spdx", "##### Package: app\nPackageName: app-sources\nFileName: ./main.c\n")
        self.write("spdx/mcuboot/zephyr.spdx", "##### Package: tinycrypt\nPackageName: tinycrypt-sources\nFileName: ./src/a.c\n")
        result = licensing.spdx_sources(self.root / "spdx", {"app": Path("apps/meshbus"), "mcuboot": Path("boot/zephyr")},
                                        Path("zephyr"))
        self.assertEqual(result, {"meshbus": {"apps/meshbus/src/main.c"}, "zephyr": {"kernel/main.c"},
                                  "mcuboot": {"boot/zephyr/main.c"}, "tinycrypt": {"./src/a.c"}})

    def test_firmware_generated_collection_includes_schema_fonts_dictionary_and_boot(self):
        self.font_fixture()
        for name in ("meshbus", "mcuboot", "zephyr", "meshbus-protobufs", "zui"):
            self.write(f"{name}/LICENSE", name + " terms")
        self.write("zui/src/dicts/LICENSE", "ISC Zeke Sikelianos permission")
        self.write("meshbus-protobufs/meshbus/a.proto", "// Copyright Schema Author\nsyntax = 'proto3';")
        self.write("spdx/app/app.spdx", "##### Package: app\nPackageName: app-sources\nFileName: ./main.c\n")
        self.write("meshbus/apps/meshbus/main.c", "/* Copyright App Author */\nint main();")
        self.write("spdx/app/zephyr.spdx", "##### Package: zephyr\nPackageName: zephyr-sources\nFileName: ./zephyr/main.c\n")
        self.write("zephyr/main.c", "/* Copyright Zephyr */\nint x;")
        self.write("spdx/mcuboot/app.spdx", "##### Package: app\nPackageName: app-sources\nFileName: ./main.c\n")
        self.write("mcuboot/boot/zephyr/main.c", "/* Copyright MCUboot */\nint x;")
        self.write("spdx/mcuboot/zephyr.spdx", "")
        domains = {name: self.root / f"build/{name}" for name in ("app", "mcuboot")}
        for name in domains:
            self.write(f"build/{name}/zephyr_modules.txt", "\n".join(
                f'"{component}":"{self.root / component}":"unused"' for component in
                ("meshbus", "mcuboot", "u8g2", "meshbus-protobufs", "zui")))
        def cache(path, key):
            if key == "ZEPHYR_BASE":
                return str(self.root / "zephyr")
            return str(self.root / ("meshbus/apps/meshbus" if path.parent.name == "app" else "mcuboot/boot/zephyr"))
        configs = {"app": {"CONFIG_MBS": "y", "CONFIG_U8G2": "y", "CONFIG_ZUI": "y", "CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE": "y",
                            "CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_BUILTIN_ENGLISH_DICT": "y"}, "mcuboot": {}}
        with patch.object(licensing, "linked_fonts", return_value={"u8g2_font_alpha_tr"}), \
                patch("west.util.west_topdir", return_value=str(self.root)):
            result = licensing.firmware(self.root / "meshbus", domains, {
                "status": "generated", "private_retention": {"path": "spdx"}}, self.root, self.output, configs, cache)
        self.assertEqual(result["selection"], "spdx-source-components")
        self.assertTrue((self.output / "licenses/mcuboot/LICENSE").exists())
        self.assertTrue((self.output / "licenses/zui/SUBTLEX-LICENSE.txt").exists())
        self.assertTrue((self.output / "licenses/u8g2/fonts/alpha.txt").exists())
        self.assertIn("Schema Author", (self.output / "licenses/meshbus-protobufs/SOURCE-NOTICES.txt").read_text())
        self.assertFalse((self.output / "licenses/unused").exists())

    def test_development_without_spdx_is_explicitly_partial(self):
        self.write("meshbus/LICENSE", "Apache terms")
        result = licensing.firmware(self.root / "meshbus", {}, {"status": "not-generated"},
                                    self.root, self.output, {}, None)
        self.assertEqual(result["selection"], "partial-no-spdx")

    def test_external_zui_without_spdx_keeps_terms_and_dictionary(self):
        self.write("meshbus/LICENSE", "Apache terms")
        self.write("zui/LICENSE", "ZUI Apache terms")
        self.write("zui/src/dicts/LICENSE", "ISC Zeke Sikelianos permission")
        build = self.root / "build/app"
        modules = self.write("build/app/zephyr_modules.txt",
                             f'"zui":"{self.root / "zui"}":"unused"')
        conf = {"app": {"CONFIG_ZUI": "y", "CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE": "y",
                        "CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_BUILTIN_ENGLISH_DICT": "y"}}
        licensing.firmware(self.root / "meshbus", {"app": build}, {"status": "not-generated"},
                           self.root, self.output, conf, None)
        self.assertEqual((self.output / "licenses/zui/LICENSE").read_text(), "ZUI Apache terms")
        self.assertEqual((self.output / "licenses/zui/SUBTLEX-LICENSE.txt").read_text(),
                         "ISC Zeke Sikelianos permission")
        manifest = json.loads((self.output / "license-materials.json").read_text())
        entry = next(item for item in manifest["components"] if item["component"] == "zui")
        self.assertIn("licenses/zui/SUBTLEX-LICENSE.txt", entry["materials"])
        modules.unlink()
        with self.assertRaisesRegex(ValueError, "ZUI module missing"):
            licensing.firmware(self.root / "meshbus", {"app": build}, {"status": "not-generated"},
                               self.root, self.root / "missing-zui", conf, None)

    def test_custom_dictionary_keeps_adjacent_grant_or_fails(self):
        self.write("meshbus/LICENSE", "Apache terms")
        dictionary = self.write("dictionary/words.txt", "word 10\n")
        conf = {"app": {"CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE": "y",
                         "CONFIG_ZUI_TEXT_EDITOR_PREDICTIVE_DICT_SOURCE": str(dictionary)}}
        self.write("dictionary/LICENSE", "Dictionary author and complete grant")
        licensing.firmware(self.root / "meshbus", {}, {"status": "not-generated"},
                           self.root, self.output, conf, None)
        self.assertEqual((self.output / "licenses/dictionary-app/LICENSE").read_text(),
                         "Dictionary author and complete grant")
        (self.root / "dictionary/LICENSE").unlink()
        with self.assertRaisesRegex(ValueError, "no license materials"):
            licensing.firmware(self.root / "meshbus", {}, {"status": "not-generated"},
                               self.root, self.root / "missing", conf, None)

    def test_header_only_hal_preserves_grant_and_standard_terms(self):
        self.write("hal/source.c", "/* Copyright HAL Author\nSPDX-License-Identifier: Apache-2.0 */\nint x;")
        self.write("standards/Apache-2.0.txt", "Complete Apache terms")
        licensing.copy_component(self.root / "hal", self.output, "hal", ["source.c"], [self.root / "standards"])
        self.assertEqual((self.output / "licenses/hal/LICENSES/Apache-2.0.txt").read_text(), "Complete Apache terms")
        self.assertIn("HAL Author", (self.output / "licenses/hal/SOURCE-NOTICES.txt").read_text())

    def runtime_fixture(self, name="sdk"):
        prefix = self.root / name / "gnu/arm-zephyr-eabi"
        compiler = self.write(f"{name}/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc", "fixture compiler")
        self.write(f"{name}/sdk_version", "1.0.1\n")
        for owner, filenames in (("gcc", ("COPYING3", "COPYING.RUNTIME")),
                                 ("picolibc", ("COPYING.picolibc", "COPYING.NEWLIB", "COPYING.GPL2"))):
            for filename in filenames:
                self.write(f"{name}/gnu/arm-zephyr-eabi/share/licenses/{owner}/{filename}", filename + " fixture terms")
        libraries = [self.write(f"{name}/gnu/arm-zephyr-eabi/lib/{library}", library + " fixture bytes")
                     for library in ("libc.a", "libgcc.a")]
        build = self.root / (name + "-build")
        self.write(f"{name}-build/zephyr/zephyr.map", "\n".join("LOAD " + str(p) for p in libraries))
        conf = {"CONFIG_PICOLIBC_USE_TOOLCHAIN": "y", "CONFIG_LIBGCC_RTLIB": "y"}
        return prefix, compiler, build, conf

    def test_runtime_notices_follow_linker_archives_and_bind_installed_bytes(self):
        prefix, compiler, build, conf = self.runtime_fixture()
        entries, inputs = licensing.toolchain_runtimes({"app": build}, {"app": conf},
                                                      lambda *args: str(compiler), self.output)
        self.assertEqual({p["component"] for p in entries}, {"toolchain-gcc-runtime", "toolchain-picolibc"})
        self.assertEqual(inputs[0]["sdk_version"], "1.0.1")
        self.assertEqual(inputs[0]["libraries"], [{"path": "lib/" + name,
                         "sha256": art.digest((prefix / "lib" / name).read_bytes())} for name in ("libc.a", "libgcc.a")])
        self.assertEqual((self.output / "licenses/toolchain-picolibc/COPYING.picolibc").read_bytes(),
                         (prefix / "share/licenses/picolibc/COPYING.picolibc").read_bytes())

    def test_missing_runtime_permission_text_fails_collection(self):
        prefix, compiler, build, conf = self.runtime_fixture()
        (prefix / "share/licenses/picolibc/COPYING.picolibc").unlink()
        with self.assertRaises(FileNotFoundError):
            licensing.toolchain_runtimes({"app": build}, {"app": conf}, lambda *args: str(compiler), self.output)

    def test_missing_configured_runtime_archive_fails_collection(self):
        prefix, compiler, build, conf = self.runtime_fixture()
        (build / "zephyr/zephyr.map").write_text("LOAD " + str(prefix / "lib/libgcc.a"))
        with self.assertRaisesRegex(ValueError, "missing configured Picolibc"):
            licensing.toolchain_runtimes({"app": build}, {"app": conf}, lambda *args: str(compiler), self.output)

    def test_unknown_toolchain_archive_requires_review(self):
        prefix, compiler, build, conf = self.runtime_fixture()
        self.write("sdk/gnu/arm-zephyr-eabi/lib/libunknown.a", "unknown library")
        with (build / "zephyr/zephyr.map").open("a") as stream:
            stream.write("\nLOAD " + str(prefix / "lib/libunknown.a"))
        with self.assertRaisesRegex(ValueError, "unreviewed toolchain runtime"):
            licensing.toolchain_runtimes({"app": build}, {"app": conf}, lambda *args: str(compiler), self.output)

    def test_domains_cannot_silently_replace_different_runtime_terms(self):
        first, compiler, build, conf = self.runtime_fixture()
        second, other_compiler, other_build, other_conf = self.runtime_fixture("other-sdk")
        (second / "share/licenses/picolibc/COPYING.picolibc").write_text("different fixture terms")
        def cache(path, key):
            return str(compiler if path.parent == build else other_compiler)
        with self.assertRaisesRegex(ValueError, "conflicting toolchain runtime terms"):
            licensing.toolchain_runtimes({"app": build, "mcuboot": other_build},
                                        {"app": conf, "mcuboot": other_conf}, cache, self.output)


if __name__ == "__main__":
    unittest.main()
