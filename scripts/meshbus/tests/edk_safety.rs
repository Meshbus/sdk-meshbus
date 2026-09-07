// SPDX-License-Identifier: Apache-2.0
use meshbus_cli::{archive, edk, host};
use std::fs;
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
