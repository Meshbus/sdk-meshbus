// SPDX-License-Identifier: Apache-2.0
use meshbus_cli::{app::bundle, host};
use serde_json::json;
use std::fs;

#[test]
fn package_integrity_identity_and_paths_share_one_boundary() {
    let root = tempfile::tempdir().unwrap();
    // Real native template output from the C2 EDK, retained as a package fixture.
    let mba = host::unhex(include_str!("fixtures/native-mba-v2.hex").trim()).unwrap();
    let package = root.path().join("local-native.mba");
    fs::write(&package, &mba).unwrap();
    host::json(&package.with_extension("build.json"), &json!({
        "target":"idea_mesh_tracker_c2/nrf54l15/cpuapp",
        "host":{"build-revision":"fixture", "image-sha256":"a".repeat(64)},
        "metadata_version":2,"interface_abi":1,"requires":["symbol:extra_required"],"resource_collection":false
    })).unwrap();
    let collection = root.path().join("local-native.install");
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
        "abi",
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
            "abi" => changed["host"]["interface_abi"] = json!(999),
            _ => unreachable!(),
        }
        host::json(&path, &changed).unwrap();
        assert!(bundle::load(&path).is_err(), "accepted {case}");
    }
}
