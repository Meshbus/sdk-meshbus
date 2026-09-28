// SPDX-License-Identifier: Apache-2.0
use meshbus_cli::{app::bundle, host};
use serde_json::json;
use std::fs;

#[test]
fn package_integrity_identity_and_paths_share_one_boundary() {
    let root = tempfile::tempdir().unwrap();
    // Built from tests/subsys/llext/src/app_ext.c for qemu_x86.
    let mba = host::unhex(include_str!("fixtures/native-mba-v1.hex").trim()).unwrap();
    let package = root.path().join("local-native.mba");
    fs::write(&package, &mba).unwrap();
    host::json(
        &package.with_extension("build.json"),
        &json!({
            "schema":1,"target":"qemu_x86/atom",
            "host":{"build-revision":"fixture", "image-sha256":"a".repeat(64)},
            "metadata_version":1,"requires":["symbol:extra_required"],"resource_collection":false
        }),
    )
    .unwrap();
    let collection = root.path().join("mbs_app.install");
    fs::create_dir_all(&collection).unwrap();
    fs::write(collection.join("install.json"), b"stale SDK output").unwrap();
    let path = bundle::collect(&package).unwrap();
    let original: serde_json::Value = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    assert!(original["resources"].as_array().unwrap().is_empty());
    assert!(
        bundle::load(&path)
            .unwrap()
            .manifest
            .host
            .requires
            .contains("extra_required")
    );
    for case in [
        "schema",
        "identity",
        "traversal",
        "absolute",
        "missing",
        "digest",
        "duplicate",
        "save-path",
        "metadata-version",
    ] {
        let mut changed = original.clone();
        match case {
            "schema" => changed["schema"] = json!(99),
            "identity" => changed["id"] = json!("another-app"),
            "traversal" => changed["mba"]["path"] = json!("../local-native.mba"),
            "absolute" => changed["mba"]["path"] = json!("/local-native.mba"),
            "missing" => changed["mba"]["path"] = json!("missing.mba"),
            "digest" => changed["mba"]["sha256"] = json!("0".repeat(64)),
            "duplicate" => changed["resources"] = json!([changed["mba"].clone()]),
            "save-path" => changed["mba"]["destination"] = json!("/extra/saves/local-native.dat"),
            "metadata-version" => changed["host"]["metadata_version"] = json!(2),
            _ => unreachable!(),
        }
        host::json(&path, &changed).unwrap();
        assert!(bundle::load(&path).is_err(), "accepted {case}");
    }
}

#[test]
fn package_rejects_unsupported_metadata_and_reserved_bytes_before_collection() {
    let root = tempfile::tempdir().unwrap();
    let fixture = host::unhex(include_str!("fixtures/native-mba-v1.hex").trim()).unwrap();
    let elf = meshbus_cli::elf::Elf::parse(&fixture).unwrap();
    let metadata = elf
        .sections
        .iter()
        .find(|s| s.name == ".meshbus.llext.meta")
        .unwrap();
    let offset = metadata.offset as usize;
    for (field, value, message) in [
        (4, 0, "unsupported MBA metadata"),
        (4, 2, "unsupported MBA metadata"),
        (216, 1, "nonzero MBA reserved bytes"),
        (220, 1, "nonzero MBA reserved bytes"),
        (348, 1, "nonzero MBA reserved bytes"),
    ] {
        let mut bytes = fixture.clone();
        bytes[offset + field..offset + field + 4].copy_from_slice(&u32::to_le_bytes(value));
        let path = root.path().join("invalid.mba");
        fs::write(&path, bytes).unwrap();
        // No build sidecar exists: metadata must fail before it is consulted.
        let error = bundle::collect(&path).unwrap_err();
        assert!(error.to_string().contains(message), "{error}");
    }
}

#[test]
fn collection_requires_current_report_and_preserves_sdk_resource_bridge() {
    let root = tempfile::tempdir().unwrap();
    let mba = host::unhex(include_str!("fixtures/native-mba-v1.hex").trim()).unwrap();
    let package = root.path().join("mbs_app.mba");
    fs::write(&package, &mba).unwrap();
    let report_path = package.with_extension("build.json");
    let report = json!({"schema":1,"target":"qemu_x86/atom","host":{"build-revision":"fixture"},"metadata_version":1,"requires":[],"resource_collection":true});
    fs::write(root.path().join("mbs_app.abr"), b"resources").unwrap();
    let resource = json!({"schema":1,"identity":"mbs_app","file":"mbs_app.abr","directory":"/extra/apps/mbs_app","destination":"/extra/apps/mbs_app/mbs_app.abr","length":9,"sha256":host::hash(b"resources")});
    host::json(&root.path().join("arduboy-resources.json"), &resource).unwrap();
    meshbus_cli::llext::resource_collection(root.path(), &package, "mbs_app").unwrap();
    for (field, value) in [
        ("schema", None),
        ("schema", Some(json!(2))),
        ("resource_collection", None),
        ("resource_collection", Some(json!("true"))),
    ] {
        let mut invalid = report.clone();
        if let Some(value) = value {
            invalid[field] = value;
        } else {
            invalid.as_object_mut().unwrap().remove(field);
        }
        host::json(&report_path, &invalid).unwrap();
        assert!(bundle::collect(&package).is_err(), "accepted {invalid}");
    }
    host::json(&report_path, &report).unwrap();
    let path = bundle::collect(&package).unwrap();
    let result = bundle::load(&path).unwrap();
    assert_eq!(result.manifest.resources.len(), 1);
    assert_eq!(
        result.manifest.resources[0].destination,
        "/extra/apps/mbs_app/mbs_app.abr"
    );
    assert_eq!(
        result.manifest.resources[0].sha256,
        host::hash(b"resources")
    );
    fs::write(
        root.path().join("mbs_app.install/mbs_app.abr"),
        b"tampered!",
    )
    .unwrap();
    assert!(bundle::collect(&package).is_err());
}
