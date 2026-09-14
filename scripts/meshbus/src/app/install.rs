// SPDX-License-Identifier: Apache-2.0
//! Verified file-set transfer and recoverable device commit.
use super::{bundle, device::HostInfo, session};
use crate::{commands::connect::DeviceClient, host};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

const STORE: &str = "/extra/apps/.meshbus";
#[derive(Debug, Serialize, Deserialize)]
pub struct Status {
    pub protocol_version: u32,
    pub app_id: String,
    pub state: String,
    pub current_bundle: String,
    pub pending_bundle: String,
    pub previous_bundle: String,
    pub mba_path: String,
    pub free_bytes: u64,
    pub required_bytes: u64,
    pub detail: i32,
}
pub fn command(client: &mut DeviceClient, action: &str, id: &str, tail: &str) -> Result<Status> {
    let value = client.command(&format!(
        "desktop package {action} {} {tail}",
        shell_words::quote(id)
    ))?;
    let status: Status = serde_json::from_value(value)?;
    ensure!(
        status.protocol_version == 1 && status.app_id == id,
        "package response identity/protocol mismatch"
    );
    ensure!(
        status.detail == 0,
        "package {action} failed: detail={}, state={}, current={}, pending={}, free={} required={} shortfall={}; use app installed {id}, then app recover {id} [--abort]",
        status.detail,
        status.state,
        status.current_bundle,
        status.pending_bundle,
        status.free_bytes,
        status.required_bytes,
        status.required_bytes.saturating_sub(status.free_bytes)
    );
    Ok(status)
}
pub fn idle(client: &mut DeviceClient, id: &str) -> Result<()> {
    let current = session::status(client, 0)?;
    if !current.resources_reclaimed {
        ensure!(
            current.app_id == id,
            "another app owns the runtime; stop it explicitly before changing {id}"
        );
        let stopped = session::stop(client, id, Some(current.session_id), 5000)?;
        ensure!(
            stopped.resources_reclaimed,
            "Session resources are not reclaimed; files were not changed"
        );
    }
    Ok(())
}
fn parents(client: &mut DeviceClient, path: &str) -> Result<()> {
    let mut directory = String::new();
    let components: Vec<_> = path.split('/').filter(|s| !s.is_empty()).collect();
    for part in &components[..components.len() - 1] {
        directory.push('/');
        directory.push_str(part);
        // mkdir service accepts an existing directory, but not a file.
        let result = client.command(&format!("fs mkdir {}", shell_words::quote(&directory)))?;
        ensure!(
            result["accepted"] == true,
            "directory creation refused: {directory}"
        );
    }
    Ok(())
}
#[derive(Serialize)]
struct File {
    path: String,
    length: u64,
    sha256: String,
}
#[derive(Serialize)]
struct Record<'a> {
    schema: u32,
    id: &'a str,
    version: &'a str,
    bundle: &'a str,
    image: &'a str,
    target: &'a str,
    firmware: &'a str,
    metadata_version: u32,
    interface_abi: u32,
    mba_path: String,
    atomic: bool,
    operation: u32,
    purge_saves: bool,
    files: Vec<File>,
    requires: Vec<&'a str>,
}
fn record<'a>(bundle: &'a bundle::Bundle, host: &'a HostInfo, atomic: bool) -> Result<Record<'a>> {
    let m = &bundle.manifest;
    ensure!(
        m.host.image_sha256.as_deref() == Some(&host.image_sha256)
            && m.host.target == host.target
            && m.host.firmware == host.build_revision
            && m.host.metadata_version == host.metadata_version
            && m.host.interface_abi == Some(host.interface_abi),
        "package does not match the connected firmware/EDK"
    );
    ensure!(
        !atomic
            || m.resources.is_empty()
            || m.host.requires.contains("mbs_desktop_app_resource_path"),
        "atomic resources require a build using the host resource location resolver"
    );
    let files: Vec<_> = m
        .files()
        .map(|f| {
            let path = if atomic {
                format!(
                    "/extra/apps/{}/versions/{}/{}",
                    m.id,
                    &bundle.sha256[..32],
                    f.path
                )
            } else {
                f.destination.clone()
            };
            bundle::destination(&path)?;
            Ok(File {
                path,
                length: f.length,
                sha256: f.sha256.clone(),
            })
        })
        .collect::<Result<_>>()?;
    ensure!(
        m.host.requires.len() <= 128,
        "package exceeds device symbol limit"
    );
    Ok(Record {
        schema: 1,
        id: &m.id,
        version: &m.version,
        bundle: &bundle.sha256,
        image: &host.image_sha256,
        target: &host.target,
        firmware: &host.build_revision,
        metadata_version: host.metadata_version,
        interface_abi: host.interface_abi,
        mba_path: files[0].path.clone(),
        atomic,
        operation: 0,
        purge_saves: false,
        files,
        requires: m.host.requires.iter().map(String::as_str).collect(),
    })
}
pub fn default_manifest(root: &Path) -> Result<PathBuf> {
    let project = super::project::read(root)?;
    let metadata = super::project::metadata(&project)?;
    let id = crate::metadata::string(&metadata, "id")?;
    Ok(root
        .join("build")
        .join(format!("{id}.install/package.json")))
}
pub fn install(
    client: &mut DeviceClient,
    host_info: &HostInfo,
    path: &Path,
    replace: bool,
    atomic: bool,
) -> Result<Status> {
    // Revalidate and retain all payloads before any device mutation.
    let bundle = bundle::load(path)?;
    let record = record(&bundle, host_info, atomic)?;
    let mut payloads = Vec::new();
    for file in bundle.manifest.files() {
        let bytes = host::read(&bundle.root.join(&file.path), 64 * 1024 * 1024)?;
        ensure!(
            bytes.len() as u64 == file.length && host::hash(&bytes) == file.sha256,
            "local payload changed: {}",
            file.path
        );
        payloads.push(bytes);
    }
    let encoded = serde_json::to_vec(&record)?;
    ensure!(
        encoded.len() <= 32768,
        "device install record exceeds limit"
    );
    let existing = command(client, "status", record.id, "")?;
    ensure!(
        existing.pending_bundle.is_empty(),
        "incomplete transaction; run app recover {} before another install",
        record.id
    );
    ensure!(
        existing.current_bundle.is_empty() || existing.current_bundle == record.bundle || replace,
        "app already installed; use --replace to authorize replacement"
    );
    ensure!(
        existing.current_bundle != record.bundle || existing.mba_path == record.mba_path,
        "installed layout differs; uninstall this app (preserving saves), then install with the requested layout"
    );
    idle(client, record.id)?;
    if existing.current_bundle == record.bundle {
        return command(client, "commit", record.id, "");
    }
    let incoming = format!("{STORE}/incoming.json");
    parents(client, &incoming)?;
    client.upload(&incoming, &encoded)?;
    ensure!(
        client.file_hash(&incoming)? == (encoded.len() as u64, host::hash(&encoded)),
        "device install record verification failed"
    );
    let prepared = command(
        client,
        "prepare",
        record.id,
        &format!("{incoming} {replace}"),
    )?;
    ensure!(
        prepared.pending_bundle == record.bundle && prepared.state == "installing",
        "device did not reserve this transaction; no payload was uploaded"
    );
    for (file, bytes) in record.files.iter().zip(payloads) {
        parents(client, &file.path)?;
        client.upload(&file.path, &bytes).with_context(|| {
            format!(
                "upload interrupted; app recover {} --abort can discard this uncommitted install",
                record.id
            )
        })?;
        ensure!(
            client.file_hash(&file.path)? == (file.length, file.sha256.clone()),
            "device file verification failed: {}",
            file.path
        );
    }
    let committed = command(client, "commit", record.id, "")?;
    ensure!(
        committed.current_bundle == bundle.sha256 && committed.pending_bundle.is_empty(),
        "commit not confirmed; query app installed {}",
        record.id
    );
    Ok(committed)
}

pub fn list(client: &mut DeviceClient) -> Result<Vec<Status>> {
    fn entries(client: &mut DeviceClient, path: &str) -> Result<Vec<serde_json::Value>> {
        let mut result = Vec::new();
        let mut offset = 0;
        loop {
            let page =
                client.command(&format!("fs list {} {offset} 16", shell_words::quote(path)))?;
            result.extend(
                page["entries"]
                    .as_array()
                    .context("missing directory entries")?
                    .iter()
                    .cloned(),
            );
            let next = page["next_offset"]
                .as_u64()
                .context("missing directory cursor")?;
            if next == 0 {
                break;
            }
            ensure!(
                next > offset && result.len() <= 4096,
                "directory cursor made no progress"
            );
            offset = next;
        }
        Ok(result)
    }
    if !entries(client, "/extra/apps")?
        .iter()
        .any(|e| e["name"] == ".meshbus")
    {
        return Ok(Vec::new());
    }
    let mut installed = Vec::new();
    for entry in entries(client, STORE)? {
        if entry["type"] != "fs_entry_type_dir" {
            continue;
        }
        let id = entry["name"].as_str().context("missing registered ID")?;
        let status = command(client, "status", id, "")?;
        if !status.current_bundle.is_empty() || !status.pending_bundle.is_empty() {
            installed.push(status);
        }
    }
    Ok(installed)
}
