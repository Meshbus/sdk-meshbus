// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    println!("cargo:rerun-if-env-changed=MESHBUS_PROTO_ROOT");
    let proto_root = env::var_os("MESHBUS_PROTO_ROOT")
        .map(PathBuf::from)
        .unwrap_or_else(find_proto_root);
    let meshbus_dir = proto_root.join("meshbus");
    let mut proto_files = fs::read_dir(&meshbus_dir)
        .unwrap_or_else(|error| {
            panic!(
                "cannot read Meshbus protobuf directory {}: {error}",
                meshbus_dir.display()
            )
        })
        .filter_map(Result::ok)
        .map(|entry| entry.path())
        .filter(|path| {
            path.extension()
                .is_some_and(|extension| extension == "proto")
        })
        .collect::<Vec<_>>();
    proto_files.sort();
    assert!(
        !proto_files.is_empty(),
        "no Meshbus protobuf files found below {}",
        meshbus_dir.display()
    );
    for path in &proto_files {
        println!("cargo:rerun-if-changed={}", path.display());
    }

    let descriptor = PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("meshbus.pb");
    let protoc = protoc_bin_vendored::protoc_bin_path().expect("vendored protoc is unavailable");
    let status = Command::new(protoc)
        .arg("-I")
        .arg(&proto_root)
        .arg("--include_imports")
        .arg(format!("--descriptor_set_out={}", descriptor.display()))
        .args(
            proto_files
                .iter()
                .map(|path| relative_proto(path, &proto_root)),
        )
        .current_dir(&proto_root)
        .status()
        .expect("failed to execute vendored protoc");
    assert!(status.success(), "protoc failed to compile Meshbus schemas");
}

fn find_proto_root() -> PathBuf {
    let output = Command::new("west")
        .args(["list", "meshbus-protobufs", "-f", "{abspath}"])
        .output()
        .unwrap_or_else(|error| {
            panic!(
                "cannot locate meshbus-protobufs: run Cargo from the Zephyr environment or set MESHBUS_PROTO_ROOT: {error}"
            )
        });
    assert!(
        output.status.success(),
        "west could not locate meshbus-protobufs: {}",
        String::from_utf8_lossy(&output.stderr).trim()
    );
    PathBuf::from(String::from_utf8(output.stdout).unwrap().trim())
}

fn relative_proto(path: &Path, root: &Path) -> PathBuf {
    path.strip_prefix(root)
        .expect("protobuf file escaped its root")
        .to_path_buf()
}
