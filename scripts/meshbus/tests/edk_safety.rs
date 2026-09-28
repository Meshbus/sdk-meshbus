// SPDX-License-Identifier: Apache-2.0
use meshbus_cli::{archive, edk, host};
use std::fs;
mod support;

fn namespace_context(build: &std::path::Path) -> edk::ContextData {
    edk::ContextData {
        build: build.into(),
        info: serde_json::Value::Null,
        config: [("CONFIG_ZUI".into(), "y".into())].into(),
        target: String::new(),
        version: String::new(),
        firmware: build.into(),
        workspace: build.into(),
        provenance: serde_json::Value::Null,
    }
}

#[test]
fn host_metadata_requires_v1() {
    let temp = tempfile::tempdir().unwrap();
    let context = namespace_context(temp.path());
    let generated = temp.path().join("zephyr/include/generated");
    fs::create_dir_all(&generated).unwrap();
    let header = generated.join("mbs_llext_metadata_version.h");
    for version in [0, 1, 2] {
        fs::write(
            &header,
            format!("#define MBS_LLEXT_METADATA_VERSION {version}U\n"),
        )
        .unwrap();
        assert_eq!(context.metadata_version().is_ok(), version == 1);
    }
}

#[test]
fn explicitly_disabled_llext_is_rejected() {
    let temp = tempfile::tempdir().unwrap();
    let mut context = namespace_context(temp.path());
    context.config.insert("CONFIG_MBS_LLEXT".into(), "n".into());
    context
        .config
        .insert("CONFIG_MESHBUS_LLEXT".into(), "y".into());
    assert!(context.require_llext().is_err());
}

fn fixture(root: &std::path::Path) {
    support::public_headers(root);
    let paths = [
        (
            "cmake.cflags",
            "set(LLEXT_CFLAGS \"-I${CMAKE_CURRENT_LIST_DIR}/include/meshbus/include;-I${CMAKE_CURRENT_LIST_DIR}/include/build/original/generated;-I${CMAKE_CURRENT_LIST_DIR}/include/meshbus/private\")\n",
        ),
        (
            "Makefile.cflags",
            "LLEXT_CFLAGS = \"-I$(LLEXT_EDK_INSTALL_DIR)/include/meshbus/include\"\n",
        ),
        (
            "include/meshbus/include/clock/clock.h",
            "#include \"meshbus/test.pb.h\"\n",
        ),
        ("include/meshbus/private/secret.h", "private\n"),
        ("include/meshbus/include/settings/settings.h", "private\n"),
        (
            "include/build/original/generated/meshbus/test.pb.h",
            "/* protobuf */\n",
        ),
        (
            "include/build/original/generated/meshbus/test.pb.c",
            "private implementation\n",
        ),
        (
            "include/build/original/generated/CMakeFiles/nanopb.dir/temporary.h",
            "private\n",
        ),
        (
            "include/zephyr/include/generated/zephyr/autoconf.h",
            "#define CONFIG_MCUBOOT_SIGNATURE_KEY_FILE \"/tmp/private-signing-key.pem\"\n#define CONFIG_MCUBOOT_ENCRYPTION_KEY_FILE \"/tmp/private-encryption-key.pem\"\n",
        ),
        (
            "include/zephyr/include/generated/zephyr/devicetree_generated.h",
            "/* /workspace/build/original/zephyr/zephyr.dts.pre */\n",
        ),
        (
            "include/meshbus/include/clock/timestamp.h",
            "/* public capability */\n",
        ),
        (
            "include/meshbus/include/gnss/heading.h",
            "/* public capability */\n",
        ),
        (
            "include/meshbus/include/llext/metadata.h",
            "/* public capability */\n",
        ),
        (
            "include/meshbus/include/llext/zbus.h",
            "/* public capability */\n",
        ),
    ];
    for (name, data) in paths {
        let path = root.join(name);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, data).unwrap();
    }
}

#[test]
fn external_u8g2_headers_and_include_flags_survive_export() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path();
    fixture(root);
    let include = "include/modules/lib/u8g2/include";
    fs::create_dir_all(root.join(include).join("display")).unwrap();
    for name in ["u8g2.h", "u8x8.h", "u8g2_snapshot.h"] {
        fs::write(
            root.join(include).join("display").join(name),
            "/* public */\n",
        )
        .unwrap();
    }
    for (name, flags) in [
        (
            "cmake.cflags",
            format!("set(LLEXT_CFLAGS \"-I${{CMAKE_CURRENT_LIST_DIR}}/{include}\")\n"),
        ),
        (
            "Makefile.cflags",
            format!("LLEXT_CFLAGS = \"-I$(LLEXT_EDK_INSTALL_DIR)/{include}\"\n"),
        ),
    ] {
        fs::write(root.join(name), flags).unwrap();
    }
    edk::filter(
        root,
        std::path::Path::new("/workspace/build/original"),
        std::path::Path::new("/workspace"),
    )
    .unwrap();
    for name in ["u8g2.h", "u8x8.h", "u8g2_snapshot.h"] {
        assert!(root.join(include).join("display").join(name).is_file());
    }
    for name in ["cmake.cflags", "Makefile.cflags"] {
        assert!(
            fs::read_to_string(root.join(name))
                .unwrap()
                .contains(include)
        );
    }
}

#[test]
fn filter_accepts_flat_public_modules_and_preserves_leaf_headers() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path();
    fixture(root);
    edk::filter(
        root,
        std::path::Path::new("/workspace/build/original"),
        std::path::Path::new("/workspace"),
    )
    .unwrap();
    for header in [
        "clock/clock.h",
        "clock/timestamp.h",
        "gnss/heading.h",
        "llext/metadata.h",
        "llext/zbus.h",
        "firmware/firmware.h",
    ] {
        assert!(
            root.join("include/meshbus/include").join(header).is_file(),
            "{header}"
        );
    }
    assert!(
        root.join("include/build/host/generated/meshbus/test.pb.h")
            .is_file()
    );
    assert!(
        !root
            .join("include/meshbus/include/settings/settings.h")
            .exists()
    );
    assert!(!root.join("include/meshbus/private/secret.h").exists());
}

#[test]
fn public_layout_rejects_noncurrent_roots_and_incomplete_headers() {
    for unsupported in [
        "meshbus",
        "zephyr/meshbus",
        "zephyr/display",
        "zephyr/zui",
        "zui",
    ] {
        let temp = tempfile::tempdir().unwrap();
        fixture(temp.path());
        fs::create_dir_all(
            temp.path()
                .join("include/meshbus/include")
                .join(unsupported),
        )
        .unwrap();
        let error = edk::filter(
            temp.path(),
            std::path::Path::new("/workspace/build/original"),
            std::path::Path::new("/workspace"),
        )
        .unwrap_err();
        assert!(
            error.to_string().contains("unsupported public SDK root"),
            "{unsupported}: {error}"
        );
    }
    for missing in [
        "include/meshbus/include/firmware/firmware.h",
        "include/modules/lib/zui/include/zui/zui.h",
        "include/modules/lib/u8g2/include/display/u8g2.h",
    ] {
        let temp = tempfile::tempdir().unwrap();
        fixture(temp.path());
        fs::remove_file(temp.path().join(missing)).unwrap();
        let error = edk::filter(
            temp.path(),
            std::path::Path::new("/workspace/build/original"),
            std::path::Path::new("/workspace"),
        )
        .unwrap_err();
        assert!(
            error.to_string().contains("missing EDK public header"),
            "{missing}: {error}"
        );
    }
}

#[test]
fn external_ui_headers_survive_without_exporting_private_helpers() {
    let temp = tempfile::tempdir().unwrap();
    fixture(temp.path());
    for private in [
        "include/meshbus/include/drivers/sensor/compass.h",
        "include/modules/lib/zui/include/private.h",
        "include/modules/lib/u8g2/include/private.h",
    ] {
        let path = temp.path().join(private);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, "private\n").unwrap();
    }
    edk::filter(
        temp.path(),
        std::path::Path::new("/workspace/build/original"),
        std::path::Path::new("/workspace"),
    )
    .unwrap();
    for public in [
        "include/meshbus/include/display/display.h",
        "include/modules/lib/zui/include/zui/zui.h",
        "include/modules/lib/u8g2/include/display/u8g2.h",
    ] {
        assert!(temp.path().join(public).is_file());
    }
    for private in [
        "include/meshbus/include/drivers/sensor/compass.h",
        "include/modules/lib/zui/include/private.h",
        "include/modules/lib/u8g2/include/private.h",
    ] {
        assert!(!temp.path().join(private).exists());
    }
}

#[test]
fn filter_accepts_public_generated_directory() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path();
    fixture(root);
    edk::filter(
        root,
        std::path::Path::new("/workspace/build/original"),
        std::path::Path::new("/workspace"),
    )
    .unwrap();
    assert!(
        root.join("include/build/host/generated/meshbus/test.pb.h")
            .is_file()
    );
    assert!(
        !root
            .join("include/build/host/generated/meshbus/test.pb.c")
            .exists()
    );
    assert!(
        !root
            .join("include/build/host/generated/CMakeFiles")
            .exists()
    );
    assert!(
        fs::read_to_string(root.join("cmake.cflags"))
            .unwrap()
            .contains("include/build/host/generated")
    );
}
#[test]
fn public_generated_headers_require_one_match_in_the_host_image() {
    for case in ["missing", "duplicate", "wrong-image", "unsupported-layout"] {
        let temp = tempfile::tempdir().unwrap();
        let root = temp.path();
        fixture(root);
        let header = root.join("include/build/original/generated/meshbus/test.pb.h");
        let alternative = if case == "unsupported-layout" {
            root.join("include/build/original/modules/meshbus/subsys/meshbus/meshbus/test.pb.h")
        } else {
            root.join("include/build/other/generated/meshbus/test.pb.h")
        };
        if case != "missing" {
            fs::create_dir_all(alternative.parent().unwrap()).unwrap();
            fs::copy(&header, alternative).unwrap();
        }
        if ["missing", "wrong-image", "unsupported-layout"].contains(&case) {
            fs::remove_file(header).unwrap();
        }
        let error = edk::filter(
            root,
            std::path::Path::new("/workspace/build/original"),
            std::path::Path::new("/workspace"),
        )
        .unwrap_err()
        .to_string();
        assert!(
            error.contains(if case == "wrong-image" {
                "outside host build"
            } else {
                "must have one match"
            }),
            "{case}: {error}"
        );
    }
}
#[test]
fn public_generated_headers_support_external_builds() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path();
    fixture(root);
    let external = "include/tmp/external/image";
    fs::create_dir_all(root.join(external).parent().unwrap()).unwrap();
    fs::rename(root.join("include/build/original"), root.join(external)).unwrap();
    for name in ["cmake.cflags", "Makefile.cflags"] {
        let path = root.join(name);
        fs::write(
            &path,
            fs::read_to_string(&path)
                .unwrap()
                .replace("include/build/original", external),
        )
        .unwrap();
    }
    let generated_dt = root.join("include/zephyr/include/generated/zephyr/devicetree_generated.h");
    fs::write(
        &generated_dt,
        "/* /tmp/external/image/zephyr/zephyr.dts.pre */\n",
    )
    .unwrap();
    edk::filter(
        root,
        std::path::Path::new("/tmp/external/image"),
        std::path::Path::new("/workspace"),
    )
    .unwrap();
    assert!(
        root.join("include/build/host/generated/meshbus/test.pb.h")
            .is_file()
    );
    assert!(!root.join("include/tmp").exists());
    assert_eq!(
        fs::read_to_string(generated_dt).unwrap(),
        "/* <host-build>/zephyr/zephyr.dts.pre */\n"
    );
}
#[test]
fn filter_and_reproducible_archive() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path().join("llext-edk");
    fixture(&root);
    edk::filter(
        &root,
        std::path::Path::new("/workspace/build/original"),
        std::path::Path::new("/workspace"),
    )
    .unwrap();
    assert!(!root.join("include/meshbus/private/secret.h").exists());
    assert!(
        root.join("include/build/host/generated/meshbus/test.pb.h")
            .is_file()
    );
    assert!(
        !root
            .join("include/build/host/generated/CMakeFiles")
            .exists()
    );
    let autoconf =
        fs::read_to_string(root.join("include/zephyr/include/generated/zephyr/autoconf.h"))
            .unwrap();
    assert!(autoconf.contains("#define CONFIG_MCUBOOT_SIGNATURE_KEY_FILE \"\""));
    assert!(autoconf.contains("#define CONFIG_MCUBOOT_ENCRYPTION_KEY_FILE \"\""));
    assert!(!autoconf.contains("/tmp/private-"));
    let devicetree = fs::read_to_string(
        root.join("include/zephyr/include/generated/zephyr/devicetree_generated.h"),
    )
    .unwrap();
    assert_eq!(devicetree, "/* <host-build>/zephyr/zephyr.dts.pre */\n");
    let a = temp.path().join("a.tar.xz");
    let b = temp.path().join("b.tar.xz");
    archive::pack(&root, &a, "llext-edk").unwrap();
    archive::pack(&root, &b, "llext-edk").unwrap();
    assert_eq!(fs::read(&a).unwrap(), fs::read(&b).unwrap());
    let extraction = tempfile::tempdir().unwrap();
    let extracted = archive::extract_edk(&a, extraction.path()).unwrap();
    assert_eq!(
        edk::digest(&root).unwrap(),
        edk::digest(&extracted).unwrap()
    );
}
#[test]
fn filter_accepts_generated_headers_from_external_build() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path().join("llext-edk");
    fixture(&root);
    let external = "include/tmp/external/app";
    fs::create_dir_all(root.join(external).parent().unwrap()).unwrap();
    fs::rename(root.join("include/build/original"), root.join(external)).unwrap();
    let cmake = fs::read_to_string(root.join("cmake.cflags")).unwrap();
    fs::write(
        root.join("cmake.cflags"),
        cmake.replace("include/build/original", external),
    )
    .unwrap();
    let devicetree = root.join("include/zephyr/include/generated/zephyr/devicetree_generated.h");
    fs::write(
        &devicetree,
        fs::read_to_string(&devicetree)
            .unwrap()
            .replace("/workspace/build/original", "/tmp/external/app"),
    )
    .unwrap();

    edk::filter(
        &root,
        std::path::Path::new("/tmp/external/app"),
        std::path::Path::new("/workspace"),
    )
    .unwrap();
    assert!(
        root.join("include/build/host/generated/meshbus/test.pb.h")
            .is_file()
    );
    assert_eq!(
        fs::read_to_string(devicetree).unwrap(),
        "/* <host-build>/zephyr/zephyr.dts.pre */\n"
    );
    assert!(!root.join("include/tmp").exists());
}
#[test]
fn missing_generated_dependency_fails() {
    let temp = tempfile::tempdir().unwrap();
    fixture(temp.path());
    fs::remove_file(
        temp.path()
            .join("include/build/original/generated/meshbus/test.pb.h"),
    )
    .unwrap();
    assert!(
        edk::filter(
            temp.path(),
            std::path::Path::new("/workspace/build/original"),
            std::path::Path::new("/workspace"),
        )
        .is_err()
    );
}
#[test]
fn rejects_unsafe_paths() {
    for path in ["../../bad", "/bad", "C:/bad", r"a\bad", ""] {
        assert!(archive::relative(path).is_err());
    }
}
#[test]
fn archive_rejects_wrong_root_and_links() {
    for kind in [
        tar::EntryType::Regular,
        tar::EntryType::Link,
        tar::EntryType::Symlink,
    ] {
        let temp = tempfile::tempdir().unwrap();
        let archive_path = temp.path().join("bad.tar.xz");
        let writer = lzma_rust2::XzWriter::new(
            fs::File::create(&archive_path).unwrap(),
            lzma_rust2::XzOptions::with_preset(1),
        )
        .unwrap();
        let mut builder = tar::Builder::new(writer);
        let mut header = tar::Header::new_gnu();
        header.set_entry_type(kind);
        header.set_size(0);
        header.set_mode(0o644);
        if kind != tar::EntryType::Regular {
            header.set_link_name("../../outside").unwrap();
        }
        header.set_cksum();
        builder
            .append_data(
                &mut header,
                if kind == tar::EntryType::Regular {
                    "wrong-root/file"
                } else {
                    "llext-edk/file"
                },
                std::io::empty(),
            )
            .unwrap();
        builder.into_inner().unwrap().finish().unwrap();
        let destination = temp.path().join("extract");
        assert!(archive::extract_edk(&archive_path, &destination).is_err());
    }
}
#[test]
fn modified_edk_digest_fails() {
    let temp = tempfile::tempdir().unwrap();
    fixture(temp.path());
    edk::filter(
        temp.path(),
        std::path::Path::new("/workspace/build/original"),
        std::path::Path::new("/workspace"),
    )
    .unwrap();
    let digest = edk::digest(temp.path()).unwrap();
    host::json(&temp.path().join("edk-release.json"),&serde_json::json!({"schema":1,"publishable":false,"target":"board/cpu","metadata-version":1,"host":{"version":"1.0.0","application":"app"},"toolchain":{"compiler":"arm-zephyr-eabi-gcc"},"exported-symbols":["printk"],"edk":{"sdk-sha256":digest}})).unwrap();
    edk::manifest(temp.path()).unwrap();
    let source = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    fs::create_dir_all(temp.path().join("LICENSES")).unwrap();
    fs::copy(
        source.join("LICENSE"),
        temp.path().join("LICENSES/Apache-2.0.txt"),
    )
    .unwrap();
    fs::copy(source.join("LICENSE"), temp.path().join("LICENSE.txt")).unwrap();
    for name in ["NOTICE.txt", "ZUI-NOTICES.md", "U8G2-NOTICES.md"] {
        fs::write(temp.path().join(name), "Fixture notices\n").unwrap();
    }
    // The public verifier must work with no Python, west or compiler on PATH.
    let output = tempfile::tempdir().unwrap();
    let path = output.path().join("fixture-edk.tar.xz");
    archive::pack(temp.path(), &path, "llext-edk").unwrap();
    archive::sidecar(&path).unwrap();
    let verify = || {
        std::process::Command::new(env!("CARGO_BIN_EXE_meshbus"))
            .env("PATH", "")
            .current_dir(output.path())
            .args(["edk", "verify"])
            .arg(&path)
            .output()
            .unwrap()
    };
    let result = verify();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    let record: serde_json::Value = serde_json::from_slice(&result.stdout).unwrap();
    assert_eq!(record["verified"], true);
    assert_eq!(record["manifest"]["target"], "board/cpu");
    // Recompute the archive checksum so failures specifically prove that
    // valid integrity and compiler-input metadata cannot hide missing terms.
    for name in [
        "LICENSES/Apache-2.0.txt",
        "LICENSE.txt",
        "NOTICE.txt",
        "ZUI-NOTICES.md",
        "U8G2-NOTICES.md",
    ] {
        let file = temp.path().join(name);
        let original = fs::read(&file).unwrap();
        fs::remove_file(&file).unwrap();
        archive::pack(temp.path(), &path, "llext-edk").unwrap();
        archive::sidecar(&path).unwrap();
        assert_eq!(verify().status.code(), Some(1), "missing {name}");
        fs::write(&file, "").unwrap();
        archive::pack(temp.path(), &path, "llext-edk").unwrap();
        archive::sidecar(&path).unwrap();
        assert_eq!(verify().status.code(), Some(1), "empty {name}");
        if ["LICENSES/Apache-2.0.txt", "LICENSE.txt"].contains(&name) {
            fs::write(&file, "Wrong license text\n").unwrap();
            archive::pack(temp.path(), &path, "llext-edk").unwrap();
            archive::sidecar(&path).unwrap();
            assert_eq!(verify().status.code(), Some(1), "incorrect {name}");
        }
        fs::write(&file, original).unwrap();
    }
    fs::write(
        temp.path()
            .join("include/modules/lib/zui/include/zui/zui.h"),
        "changed",
    )
    .unwrap();
    assert!(edk::manifest(temp.path()).is_err());
    archive::pack(temp.path(), &path, "llext-edk").unwrap();
    archive::sidecar(&path).unwrap();
    assert_eq!(verify().status.code(), Some(1));
}

#[test]
fn manifest_rejects_private_key_paths() {
    let temp = tempfile::tempdir().unwrap();
    fixture(temp.path());
    edk::filter(
        temp.path(),
        std::path::Path::new("/workspace/build/original"),
        std::path::Path::new("/workspace"),
    )
    .unwrap();
    let autoconf = temp
        .path()
        .join("include/zephyr/include/generated/zephyr/autoconf.h");
    fs::write(
        &autoconf,
        fs::read_to_string(&autoconf).unwrap().replace(
            "CONFIG_MCUBOOT_SIGNATURE_KEY_FILE \"\"",
            "CONFIG_MCUBOOT_SIGNATURE_KEY_FILE \"/tmp/private.pem\"",
        ),
    )
    .unwrap();
    let digest = edk::digest(temp.path()).unwrap();
    host::json(&temp.path().join("edk-release.json"),&serde_json::json!({"schema":1,"publishable":false,"target":"board/cpu","metadata-version":1,"host":{"version":"1.0.0","application":"app"},"toolchain":{"compiler":"arm-zephyr-eabi-gcc"},"exported-symbols":["printk"],"edk":{"sdk-sha256":digest}})).unwrap();
    assert!(edk::manifest(temp.path()).is_err());
}

#[test]
fn archive_rejects_case_collisions_and_size_bombs() {
    for bomb in [false, true] {
        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join("bad.tar.xz");
        let writer = lzma_rust2::XzWriter::new(
            fs::File::create(&path).unwrap(),
            lzma_rust2::XzOptions::with_preset(1),
        )
        .unwrap();
        let mut builder = tar::Builder::new(writer);
        for name in ["llext-edk/Header.h", "llext-edk/header.h"] {
            let mut header = tar::Header::new_gnu();
            header.set_size(if bomb { 256 * 1024 * 1024 } else { 0 });
            header.set_mode(0o644);
            header.set_cksum();
            builder
                .append_data(&mut header, name, std::io::empty())
                .unwrap();
        }
        builder.into_inner().unwrap().finish().unwrap();
        assert!(archive::extract_edk(&path, &temp.path().join("output")).is_err());
    }
}
#[test]
fn sysbuild_image_directories_resolve_to_the_application() {
    let temp = tempfile::tempdir().unwrap();
    for name in ["meshbus", "app"] {
        let build = temp.path().join(name);
        let image = build.join(name);
        fs::create_dir_all(image.join("zephyr")).unwrap();
        fs::write(image.join("zephyr/.config"), "CONFIG_TEST=y\n").unwrap();
        assert_eq!(edk::app_build(&build), image);
        assert_eq!(edk::app_build(&image), image);
    }
}

#[test]
fn export_inventory_is_required_typed_and_slid_is_explicit() {
    use serde_json::{Value, json};
    let temp = tempfile::tempdir().unwrap();
    fixture(temp.path());
    edk::filter(
        temp.path(),
        std::path::Path::new("/workspace/build/original"),
        std::path::Path::new("/workspace"),
    )
    .unwrap();
    let path = temp.path().join("edk-release.json");
    let mut manifest = json!({"schema":1,"publishable":false,"target":"board/cpu","metadata-version":1,"host":{"version":"1.0.0","application":"app","build-revision":"fixture"},"toolchain":{"compiler":"arm-zephyr-eabi-gcc"},"exported-symbols":["printk"],"edk":{"sdk-sha256":edk::digest(temp.path()).unwrap()}});
    host::json(&path, &manifest).unwrap();
    edk::manifest(temp.path()).unwrap();
    for value in [
        None,
        Some(Value::Null),
        Some(json!(false)),
        Some(json!({})),
        Some(json!("printk")),
        Some(json!([7])),
        Some(json!([""])),
        Some(json!(["printk", "printk"])),
    ] {
        let mut invalid = manifest.clone();
        if let Some(value) = value {
            invalid["exported-symbols"] = value;
        } else {
            invalid.as_object_mut().unwrap().remove("exported-symbols");
        }
        host::json(&path, &invalid).unwrap();
        assert!(edk::manifest(temp.path()).is_err(), "accepted {invalid}");
        assert!(
            meshbus_cli::llext::preflight(
                &json!({"stack-size":4096}),
                &invalid,
                &Default::default(),
                0,
                &Default::default()
            )
            .is_err()
        );
    }
    let config = temp
        .path()
        .join("include/zephyr/include/generated/zephyr/autoconf.h");
    fs::write(&config, "#define CONFIG_LLEXT_EXPORT_BUILTINS_BY_SLID 1\n").unwrap();
    manifest["exported-symbols"] = Value::Null;
    manifest["edk"]["sdk-sha256"] = json!(edk::digest(temp.path()).unwrap());
    host::json(&path, &manifest).unwrap();
    edk::manifest(temp.path()).unwrap();
    let direct_error = meshbus_cli::llext::preflight(
        &json!({"stack-size":4096}),
        &manifest,
        &Default::default(),
        0,
        &Default::default(),
    )
    .unwrap_err()
    .to_string();
    let result = std::process::Command::new(env!("CARGO_BIN_EXE_meshbus"))
        .args(["app", "source", "--edk"])
        .arg(temp.path())
        .arg("--toolchain")
        .arg(temp.path())
        .arg("--output")
        .arg(temp.path().join("source.json"))
        .output()
        .unwrap();
    assert!(!result.status.success());
    assert!(String::from_utf8_lossy(&result.stderr).contains(&direct_error));
    assert!(direct_error.contains("SLID EDKs"));
    assert!(!temp.path().join("source.json").exists());
}
