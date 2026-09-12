// SPDX-License-Identifier: Apache-2.0
use meshbus_cli::{archive, edk, host};
use std::fs;

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
fn current_and_legacy_service_host_builds_are_readable() {
    for prefix in ["MBS", "MESHBUS"] {
        let temp = tempfile::tempdir().unwrap();
        let mut context = namespace_context(temp.path());
        context
            .config
            .insert(format!("CONFIG_{prefix}_LLEXT"), "y".into());
        let generated = temp.path().join("zephyr/include/generated");
        fs::create_dir_all(&generated).unwrap();
        fs::write(
            generated.join(format!(
                "{}_llext_metadata_version.h",
                prefix.to_lowercase()
            )),
            format!("#define {prefix}_LLEXT_METADATA_VERSION 1U\n"),
        )
        .unwrap();
        context.require_llext().unwrap();
        assert_eq!(context.metadata_version().unwrap(), 1);
    }
}

#[test]
fn legacy_outputs_do_not_override_disabled_or_invalid_current_host_inputs() {
    let temp = tempfile::tempdir().unwrap();
    let mut context = namespace_context(temp.path());
    context.config.insert("CONFIG_MBS_LLEXT".into(), "n".into());
    context
        .config
        .insert("CONFIG_MESHBUS_LLEXT".into(), "y".into());
    assert!(context.require_llext().is_err());

    let generated = temp.path().join("zephyr/include/generated");
    fs::create_dir_all(&generated).unwrap();
    fs::write(
        generated.join("meshbus_llext_metadata_version.h"),
        "#define MESHBUS_LLEXT_METADATA_VERSION 1U\n",
    )
    .unwrap();
    fs::write(
        generated.join("mbs_llext_metadata_version.h"),
        "#define MBS_LLEXT_METADATA_VERSION 0U\n",
    )
    .unwrap();
    assert!(context.metadata_version().is_err());
}

fn fixture(root: &std::path::Path) {
    let paths = [
        (
            "cmake.cflags",
            "set(LLEXT_CFLAGS \"-I${CMAKE_CURRENT_LIST_DIR}/include/meshbus/include;-I${CMAKE_CURRENT_LIST_DIR}/include/build/original/modules/meshbus/subsys/meshbus;-I${CMAKE_CURRENT_LIST_DIR}/include/meshbus/private\")\n",
        ),
        (
            "Makefile.cflags",
            "LLEXT_CFLAGS = \"-I$(LLEXT_EDK_INSTALL_DIR)/include/meshbus/include\"\n",
        ),
        (
            "include/meshbus/include/zephyr/display/api.h",
            "/* display */\n",
        ),
        ("include/meshbus/include/zephyr/zui/api.h", "/* zui */\n"),
        (
            "include/meshbus/include/zephyr/meshbus/api.h",
            "#include \"meshbus/test.pb.h\"\n",
        ),
        ("include/meshbus/private/secret.h", "private\n"),
        (
            "include/build/original/modules/meshbus/subsys/meshbus/meshbus/test.pb.h",
            "/* protobuf */\n",
        ),
        (
            "include/build/original/modules/meshbus/private.h",
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
    ];
    for (name, data) in paths {
        let path = root.join(name);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, data).unwrap();
    }
    fs::create_dir_all(root.join(
        "include/build/original/modules/meshbus/subsys/meshbus/CMakeFiles/nanopb.dir/Users/test",
    ))
    .unwrap();
}
fn public_generated_fixture(root: &std::path::Path) {
    fixture(root);
    let generated = root.join("include/build/original/generated");
    fs::create_dir_all(&generated).unwrap();
    fs::rename(
        root.join("include/build/original/modules/meshbus/subsys/meshbus/meshbus"),
        generated.join("meshbus"),
    )
    .unwrap();
    fs::write(
        generated.join("meshbus/test.pb.c"),
        "private implementation",
    )
    .unwrap();
    fs::create_dir_all(generated.join("CMakeFiles/nanopb.dir")).unwrap();
    fs::write(
        generated.join("CMakeFiles/nanopb.dir/temporary.h"),
        "temporary",
    )
    .unwrap();
    let flags = root.join("cmake.cflags");
    fs::write(
        &flags,
        fs::read_to_string(&flags).unwrap().replace(
            "include/build/original/modules/meshbus/subsys/meshbus",
            "include/build/original/generated",
        ),
    )
    .unwrap();
}

fn flat_public_fixture(root: &std::path::Path) {
    public_generated_fixture(root);
    fs::remove_dir_all(root.join("include/meshbus/include/zephyr/meshbus")).unwrap();
    for module in [
        "bluetooth",
        "channel",
        "clock",
        "contact",
        "desktop",
        "display",
        "firmware",
        "fs",
        "gnss",
        "indicator",
        "input",
        "llext",
        "management",
        "meshcore",
        "message",
        "notify",
        "power",
        "radio",
        "telemetry",
    ] {
        let directory = root.join("include/meshbus/include").join(module);
        fs::create_dir_all(&directory).unwrap();
        fs::write(directory.join(format!("{module}.h")), "/* public */\n").unwrap();
    }
    fs::write(
        root.join("include/meshbus/include/clock/clock.h"),
        "#include \"meshbus/test.pb.h\"\n",
    )
    .unwrap();
    for (module, header) in [
        ("clock", "timestamp"),
        ("gnss", "heading"),
        ("llext", "metadata"),
        ("llext", "zbus"),
    ] {
        fs::write(
            root.join(format!("include/meshbus/include/{module}/{header}.h")),
            "/* public capability */\n",
        )
        .unwrap();
    }
    let private = root.join("include/meshbus/include/settings");
    fs::create_dir_all(&private).unwrap();
    fs::write(private.join("settings.h"), "private\n").unwrap();
}

#[test]
fn filter_accepts_flat_public_modules_and_preserves_leaf_headers() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path();
    flat_public_fixture(root);
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

fn namespaced_public_fixture(root: &std::path::Path) {
    flat_public_fixture(root);
    let include = root.join("include/meshbus/include");
    let namespace = include.join("meshbus");
    fs::create_dir(&namespace).unwrap();
    for entry in fs::read_dir(&include).unwrap() {
        let entry = entry.unwrap();
        if entry.file_name() != "zephyr" && entry.file_name() != "meshbus" {
            fs::rename(entry.path(), namespace.join(entry.file_name())).unwrap();
        }
    }
    fs::write(namespace.join("private.h"), "private\n").unwrap();
}

#[test]
fn filter_accepts_namespaced_modules_without_exporting_private_helpers() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path();
    namespaced_public_fixture(root);
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
            root.join("include/meshbus/include/meshbus")
                .join(header)
                .is_file(),
            "{header}"
        );
    }
    assert!(
        root.join("include/build/host/generated/meshbus/test.pb.h")
            .is_file()
    );
    assert!(
        !root
            .join("include/meshbus/include/meshbus/settings/settings.h")
            .exists()
    );
    assert!(
        !root
            .join("include/meshbus/include/meshbus/private.h")
            .exists()
    );
}

#[test]
fn namespaced_layout_rejects_missing_modules_and_either_legacy_layout() {
    for extra in [None, Some("clock"), Some("zephyr/meshbus")] {
        let temp = tempfile::tempdir().unwrap();
        let root = temp.path();
        namespaced_public_fixture(root);
        if let Some(extra) = extra {
            let directory = root.join("include/meshbus/include").join(extra);
            fs::create_dir_all(&directory).unwrap();
            fs::write(directory.join("clock.h"), "/* old public */\n").unwrap();
        } else {
            fs::remove_dir_all(root.join("include/meshbus/include/meshbus/firmware")).unwrap();
        }
        let error = edk::filter(
            root,
            std::path::Path::new("/workspace/build/original"),
            std::path::Path::new("/workspace"),
        )
        .unwrap_err()
        .to_string();
        assert!(
            error.contains(if extra.is_some() {
                "mixed public SDK layouts"
            } else {
                "firmware"
            }),
            "{error}"
        );
    }
}

#[test]
fn public_layout_rejects_missing_modules_and_mixed_roots() {
    for mixed in [false, true] {
        let temp = tempfile::tempdir().unwrap();
        let root = temp.path();
        flat_public_fixture(root);
        if mixed {
            let legacy = root.join("include/meshbus/include/zephyr/meshbus");
            fs::create_dir_all(&legacy).unwrap();
            fs::write(legacy.join("clock.h"), "/* legacy */\n").unwrap();
        } else {
            fs::remove_dir_all(root.join("include/meshbus/include/firmware")).unwrap();
        }
        let error = edk::filter(
            root,
            std::path::Path::new("/workspace/build/original"),
            std::path::Path::new("/workspace"),
        )
        .unwrap_err()
        .to_string();
        assert!(
            error.contains(if mixed {
                "mixed public SDK layouts"
            } else {
                "firmware"
            }),
            "{error}"
        );
    }
}
#[test]
fn filter_accepts_public_generated_directory() {
    let temp = tempfile::tempdir().unwrap();
    let root = temp.path();
    public_generated_fixture(root);
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
    for case in ["missing", "duplicate", "wrong-image", "mixed-layout"] {
        let temp = tempfile::tempdir().unwrap();
        let root = temp.path();
        public_generated_fixture(root);
        let header = root.join("include/build/original/generated/meshbus/test.pb.h");
        let alternative = if case == "mixed-layout" {
            root.join("include/build/original/modules/meshbus/subsys/meshbus/meshbus/test.pb.h")
        } else {
            root.join("include/build/other/generated/meshbus/test.pb.h")
        };
        if case != "missing" {
            fs::create_dir_all(alternative.parent().unwrap()).unwrap();
            fs::copy(&header, alternative).unwrap();
        }
        if ["missing", "wrong-image"].contains(&case) {
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
    public_generated_fixture(root);
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
        root.join("include/build/host/modules/meshbus/subsys/meshbus/meshbus/test.pb.h")
            .is_file()
    );
    assert!(
        !root
            .join("include/build/host/modules/meshbus/subsys/meshbus/CMakeFiles")
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
        root.join("include/build/host/modules/meshbus/subsys/meshbus/meshbus/test.pb.h")
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
            .join("include/build/original/modules/meshbus/subsys/meshbus/meshbus/test.pb.h"),
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
    host::json(&temp.path().join("edk-release.json"),&serde_json::json!({"schema":1,"publishable":false,"target":"board/cpu","metadata-version":1,"host":{"version":"1.0.0","application":"app"},"toolchain":{"compiler":"arm-zephyr-eabi-gcc"},"edk":{"sdk-sha256":digest}})).unwrap();
    edk::manifest(temp.path()).unwrap();
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
    fs::write(
        temp.path().join("include/meshbus/include/zephyr/zui/api.h"),
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
    host::json(&temp.path().join("edk-release.json"),&serde_json::json!({"schema":1,"publishable":false,"target":"board/cpu","metadata-version":1,"host":{"version":"1.0.0","application":"app"},"toolchain":{"compiler":"arm-zephyr-eabi-gcc"},"edk":{"sdk-sha256":digest}})).unwrap();
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
fn relocated_and_legacy_sysbuild_domains_resolve_to_the_application() {
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
