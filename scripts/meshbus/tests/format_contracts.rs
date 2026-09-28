// SPDX-License-Identifier: Apache-2.0
//! DFOTA, ELF and resource contract fixtures.
use meshbus_cli::{delta, host, package};
use serde_json::Value;
use std::{fs, path::Path, process::Command};
fn dfota_fixture() -> (tempfile::TempDir, std::path::PathBuf) {
    let root = tempfile::tempdir().unwrap();
    let fixture: Value = serde_json::from_str(include_str!("fixtures/dfota-v1.json")).unwrap();
    for (name, bytes) in fixture.as_object().unwrap() {
        fs::write(
            root.path().join(name),
            host::unhex(bytes.as_str().unwrap()).unwrap(),
        )
        .unwrap();
    }
    let key = root.path().join("public-key");
    (root, key)
}
#[test]
fn dfota_package_and_corruption() {
    let (root, key) = dfota_fixture();
    assert_eq!(
        package::verify(root.path(), &key, &key).unwrap()["verified"],
        true
    );
    let patch = root.path().join("patch.newp");
    let mut bytes = fs::read(&patch).unwrap();
    bytes[12] ^= 1;
    fs::write(&patch, &bytes).unwrap();
    assert!(package::verify(root.path(), &key, &key).is_err());
}
#[test]
fn dfota_delta_decodes_in_rust() {
    let (root, _) = dfota_fixture();
    let old = fs::read(root.path().join("source.signed.bin")).unwrap();
    let target = fs::read(root.path().join("target.signed.bin")).unwrap();
    let patch = fs::read(root.path().join("patch.newp")).unwrap();
    assert_eq!(delta::apply(&old, &patch[88..]).unwrap(), target);
}
#[test]
fn native_package_roundtrip_and_key_rejection() {
    use ed25519_dalek::{SigningKey, pkcs8::EncodePrivateKey};
    let (root, key) = dfota_fixture();
    let signing = SigningKey::from_bytes(&[66; 32]);
    let pem = root.path().join("synthetic-test-key.pem");
    fs::write(
        &pem,
        signing.to_pkcs8_pem(Default::default()).unwrap().as_bytes(),
    )
    .unwrap();
    let output = root.path().join("native");
    let args = package::CreateArgs {
        source: root.path().join("source.signed.bin"),
        target: root.path().join("target.signed.bin"),
        output: output.clone(),
        role: "repeater".into(),
        board_id: "devkit_nrf54l15".into(),
        soc_id: "nrf54l15".into(),
        hardware_min: 1,
        hardware_max: 1,
        partition_abi: 1,
        campaign_id: Some("00112233445566778899aabbccddeeff".into()),
        image_key_id: 1,
        image_public_key: key.clone(),
        manifest_key_id: 1,
        security_counter: 1,
        manifest_private_key: Some(pem),
        manifest_signature: None,
        manifest_public_key: None,
        signer_provenance: "test-fixture".into(),
    };
    package::create(&args).unwrap();
    package::verify(&output, &key, &key).unwrap();
    let wrong = root.path().join("wrong-key");
    fs::write(
        &wrong,
        SigningKey::from_bytes(&[67; 32]).verifying_key().as_bytes(),
    )
    .unwrap();
    assert!(package::verify(&output, &wrong, &key).is_err());
    assert!(package::verify(&output, &key, &wrong).is_err());
    assert!(package::create(&args).is_err());
}
#[test]
#[ignore = "requires MESHBUS_DETOOLS_C pointing to the pinned C decoder executable"]
fn native_delta_with_pinned_c_decoder() {
    let decoder = std::env::var_os("MESHBUS_DETOOLS_C").expect("MESHBUS_DETOOLS_C required");
    assert!(Path::new(&decoder).is_file());
    let (root, _) = dfota_fixture();
    let old = fs::read(root.path().join("source.signed.bin")).unwrap();
    let target = fs::read(root.path().join("target.signed.bin")).unwrap();
    let mut shuffled = (0..=255).cycle().take(8192).collect::<Vec<u8>>();
    shuffled.rotate_left(3000);
    for (old, target) in [
        (old, target),
        (vec![], vec![17; 4096]),
        ((0..=255).cycle().take(8192).collect(), shuffled),
        (vec![1; 7000], vec![2; 1000]),
    ] {
        let patch = delta::create(&old, &target).unwrap();
        fs::write(root.path().join("raw.patch"), patch).unwrap();
        fs::write(root.path().join("source.bin"), old).unwrap();
        let result = Command::new(&decoder)
            .arg("apply_patch")
            .arg(root.path().join("source.bin"))
            .arg(root.path().join("raw.patch"))
            .arg(root.path().join("reconstructed.bin"))
            .output()
            .unwrap();
        assert!(
            result.status.success(),
            "{}",
            String::from_utf8_lossy(&result.stderr)
        );
        assert_eq!(
            fs::read(root.path().join("reconstructed.bin")).unwrap(),
            target
        );
    }
}

#[test]
fn elf_heap_estimates_and_malformed_tables() {
    let fixture: Value = serde_json::from_str(include_str!("fixtures/elf-heap-v1.json")).unwrap();
    let bytes = host::unhex(fixture["elf"].as_str().unwrap()).unwrap();
    let elf = meshbus_cli::elf::Elf::parse(&bytes).unwrap();
    for row in fixture["cases"].as_array().unwrap() {
        let config = serde_json::from_value(row["config"].clone()).unwrap();
        assert_eq!(
            u64::from(elf.heap(&config).unwrap()),
            row["heap"].as_u64().unwrap()
        );
    }
    for length in [0, 51, bytes.len() - 1] {
        assert!(meshbus_cli::elf::Elf::parse(&bytes[..length]).is_err());
    }
    let mut damaged = bytes.clone();
    damaged[32..36].copy_from_slice(&u32::MAX.to_le_bytes());
    assert!(meshbus_cli::elf::Elf::parse(&damaged).is_err());
    let mut damaged = bytes;
    let section_table = u32::from_le_bytes(damaged[32..36].try_into().unwrap()) as usize;
    damaged[section_table..section_table + 4].copy_from_slice(&u32::MAX.to_le_bytes());
    assert!(meshbus_cli::elf::Elf::parse(&damaged).is_err());
}

#[test]
fn extension_required_imports_are_actual_undefined_symbols() {
    let fixture: Value = serde_json::from_str(include_str!("fixtures/elf-heap-v1.json")).unwrap();
    let bytes = host::unhex(fixture["elf"].as_str().unwrap()).unwrap();
    let elf = meshbus_cli::elf::Elf::parse(&bytes).unwrap();
    let imports = elf.required_imports(&bytes).unwrap();
    assert!(imports.contains("printk"));
    assert!(!imports.contains("main"));
}

#[test]
fn extension_preflight_rejects_missing_imports_and_capacity() {
    use meshbus_cli::llext::preflight;
    let data = serde_json::json!({"stack-size":4096});
    let edk = serde_json::json!({"exported-symbols":["printk"]});
    let config = [
        ("CONFIG_MBS_LLEXT_APP_MAX_HEAP_SIZE".into(), "90000".into()),
        (
            "CONFIG_MBS_DESKTOP_APP_SHARED_STACK_SIZE".into(),
            "4096".into(),
        ),
    ]
    .into();
    let imports = ["printk".into()].into();
    let ok = preflight(&data, &edk, &config, 50000, &imports).unwrap();
    assert_eq!(ok["missing_imports"], serde_json::json!([]));
    assert_eq!(ok["capacity_errors"], serde_json::json!([]));
    let bad = preflight(&data, &edk, &config, 90001, &["absent_api".into()].into()).unwrap();
    assert_eq!(bad["missing_imports"], serde_json::json!(["absent_api"]));
    assert_eq!(bad["capacity_errors"].as_array().unwrap().len(), 1);
    let bad = preflight(
        &serde_json::json!({"stack-size":4097}),
        &edk,
        &config,
        100,
        &imports,
    )
    .unwrap();
    assert_eq!(bad["capacity_errors"].as_array().unwrap().len(), 1);
}

#[test]
fn resource_collection_binds_exact_files_and_rejects_substitution() {
    let root = tempfile::tempdir().unwrap();
    let package = root.path().join("fixture.mba");
    fs::write(&package, b"mba").unwrap();
    fs::write(root.path().join("fixture.abr"), b"resource").unwrap();
    let spec = serde_json::json!({"schema":1,"identity":"fixture","file":"fixture.abr",
        "directory":"/extra/apps/test","destination":"/extra/apps/test/fixture.abr",
        "length":8,"sha256":host::hash(b"resource")});
    fs::write(
        root.path().join("arduboy-resources.json"),
        serde_json::to_vec(&spec).unwrap(),
    )
    .unwrap();
    let path = meshbus_cli::llext::resource_collection(root.path(), &package, "fixture").unwrap();
    let manifest: Value = serde_json::from_slice(&fs::read(path).unwrap()).unwrap();
    assert_eq!(manifest["files"][0]["sha256"], host::hash(b"resource"));
    assert_eq!(manifest["files"][1]["sha256"], host::hash(b"mba"));
    fs::write(root.path().join("fixture.abr"), b"changed!").unwrap();
    assert!(meshbus_cli::llext::resource_collection(root.path(), &package, "fixture").is_err());
}
