// SPDX-License-Identifier: Apache-2.0
//! Behavioral regression gates for migrated host tools, without devices or Python.
use meshbus_cli::{host, package};
use serde_json::Value;
use std::{fs, path::Path, process::Command};

fn fixture() -> tempfile::TempDir {
    let root = tempfile::tempdir().unwrap();
    let files: Value = serde_json::from_str(include_str!("fixtures/legacy-dfota-v1.json")).unwrap();
    for (name, hex) in files.as_object().unwrap() {
        fs::write(
            root.path().join(name),
            host::unhex(hex.as_str().unwrap()).unwrap(),
        )
        .unwrap();
    }
    root
}
fn refresh_inventory(root: &Path) {
    let mut inventory: Value =
        serde_json::from_slice(&fs::read(root.join("inventory.json")).unwrap()).unwrap();
    for (name, entry) in inventory["files"].as_object_mut().unwrap() {
        let data = fs::read(root.join(name)).unwrap();
        *entry = serde_json::json!({"size":data.len(),"sha256":host::hash(&data)});
    }
    host::json(&root.join("inventory.json"), &inventory).unwrap();
    host::checksums(root).unwrap();
}
#[test]
fn tampering_is_rejected_even_with_updated_checksums() {
    for name in [
        "manifest.sig",
        "manifest.cbor",
        "source.signed.bin",
        "target.signed.bin",
        "patch.newp",
    ] {
        let root = fixture();
        let path = root.path().join(name);
        let mut bytes = fs::read(&path).unwrap();
        let position = if name.ends_with(".bin") { 2048 } else { 0 };
        bytes[position] ^= 1;
        fs::write(path, bytes).unwrap();
        refresh_inventory(root.path());
        let key = root.path().join("public-key");
        assert!(
            package::verify(root.path(), &key, &key).is_err(),
            "accepted {name} corruption"
        );
    }
}
#[test]
fn substituted_valid_image_and_wrong_target_are_rejected() {
    let root = fixture();
    fs::copy(
        root.path().join("target.signed.bin"),
        root.path().join("source.signed.bin"),
    )
    .unwrap();
    refresh_inventory(root.path());
    let key = root.path().join("public-key");
    assert!(package::verify(root.path(), &key, &key).is_err());
    let root = fixture();
    let path = root.path().join("inventory.json");
    let mut inventory: Value = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    inventory["board_id"] = "other-board".into();
    host::json(&path, &inventory).unwrap();
    host::checksums(root.path()).unwrap();
    let key = root.path().join("public-key");
    assert!(package::verify(root.path(), &key, &key).is_err());
}
#[test]
fn image_truncation_and_protected_counter_rules() {
    let root = fixture();
    let bytes = fs::read(root.path().join("source.signed.bin")).unwrap();
    let header = u16::from_le_bytes(bytes[8..10].try_into().unwrap()) as usize;
    let payload = u32::from_le_bytes(bytes[12..16].try_into().unwrap()) as usize;
    let protected = header + payload;
    for length in [0, 31, header - 1, protected, bytes.len() - 1] {
        assert!(package::parse_image(&bytes[..length]).is_err());
    }
    for (offset, value) in [(10, 0), (protected + 4, 0x51), (protected + 6, 2)] {
        let mut damaged = bytes.clone();
        damaged[offset] = value;
        assert!(package::parse_image(&damaged).is_err());
    }
    // Duplicate a well-formed protected counter, adjusting all enclosing lengths.
    let mut duplicate = bytes.clone();
    let entry = duplicate[protected + 4..protected + 12].to_vec();
    duplicate.splice(protected + 12..protected + 12, entry);
    duplicate[10..12].copy_from_slice(&20u16.to_le_bytes());
    duplicate[protected + 2..protected + 4].copy_from_slice(&20u16.to_le_bytes());
    assert!(package::parse_image(&duplicate).is_err());
}
#[test]
fn command_exit_codes_and_standalone_package_use() {
    let root = fixture();
    let key = root.path().join("public-key");
    let executable = env!("CARGO_BIN_EXE_meshbus");
    let result = Command::new(executable)
        .current_dir(root.path())
        .env("PATH", "")
        .args(["firmware", "package", "verify"])
        .arg(root.path())
        .arg("--manifest-public-key")
        .arg(&key)
        .arg("--image-public-key")
        .arg(&key)
        .output()
        .unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    for (args, expected) in [
        (vec!["firmware", "package", "inspect", "missing"], 1),
        (vec!["edk", "-d", "missing", "-o", "output"], 1),
        (vec!["llext", "missing"], 1),
        (
            vec![
                "firmware",
                "inspect",
                "--device",
                "idea_mesh_tracker_c2",
                "--app",
                "missing",
            ],
            2,
        ),
        (vec!["unknown-command"], 2),
        (vec!["release", "matrix"], 2),
    ] {
        let output = Command::new(executable)
            .current_dir(root.path())
            .args(args)
            .output()
            .unwrap();
        assert_eq!(output.status.code(), Some(expected));
    }
}
