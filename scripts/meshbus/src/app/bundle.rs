// SPDX-License-Identifier: Apache-2.0
//! Versioned file-set boundary. SDKs own conversion; this module owns integrity.
use crate::{elf::Elf, host, metadata};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::{
    collections::BTreeSet,
    fs,
    path::{Path, PathBuf},
};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct File {
    pub path: String,
    pub destination: String,
    pub length: u64,
    pub sha256: String,
}
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Host {
    pub target: String,
    pub firmware: String,
    pub image_sha256: Option<String>,
    pub metadata_version: u32,
    pub interface_abi: Option<u32>,
    pub requires: BTreeSet<String>,
}
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    pub schema: u32,
    pub kind: String,
    pub id: String,
    pub version: String,
    pub mba: File,
    pub resources: Vec<File>,
    pub host: Host,
}
impl Manifest {
    pub fn files(&self) -> impl Iterator<Item = &File> {
        std::iter::once(&self.mba).chain(self.resources.iter())
    }
}
#[derive(Debug)]
pub struct Bundle {
    pub root: PathBuf,
    pub manifest: Manifest,
    pub sha256: String,
}
pub fn relative(path: &str) -> Result<()> {
    ensure!(
        !path.is_empty()
            && path.len() <= 191
            && path.split('/').all(|p| {
                !p.is_empty()
                    && p != "."
                    && p != ".."
                    && !p.starts_with('.')
                    && p.bytes()
                        .all(|b| b.is_ascii_alphanumeric() || b"_.-".contains(&b))
            }),
        "invalid package relative path: {path}"
    );
    Ok(())
}
pub fn destination(path: &str) -> Result<()> {
    ensure!(path.len() <= 191, "installation path too long");
    relative(
        path.strip_prefix("/extra/apps/")
            .context("package files must be under /extra/apps; saves are separate")?,
    )
}
fn payload(root: &Path, file: &File) -> Result<Vec<u8>> {
    relative(&file.path)?;
    destination(&file.destination)?;
    let path = root.join(&file.path);
    ensure!(
        path.canonicalize()?.starts_with(root.canonicalize()?),
        "package file escapes collection"
    );
    let data = host::read(&path, 64 * 1024 * 1024)?;
    ensure!(
        data.len() as u64 == file.length && host::hash(&data) == file.sha256,
        "package file integrity mismatch: {}",
        file.path
    );
    Ok(data)
}
struct Identity {
    id: String,
    version: String,
    target: String,
    metadata_version: u32,
    interface_abi: Option<u32>,
    imports: BTreeSet<String>,
}
fn identity(bytes: &[u8]) -> Result<Identity> {
    let elf = Elf::parse(bytes)?;
    let sections: Vec<_> = elf
        .sections
        .iter()
        .filter(|s| s.name == ".meshbus.llext.meta")
        .collect();
    ensure!(
        sections.len() == 1 && sections[0].size == 368 && bytes[5] == 1,
        "MBA requires one supported little-endian metadata record"
    );
    let section = sections[0];
    let data = bytes
        .get(section.offset as usize..(section.offset + section.size) as usize)
        .context("MBA metadata outside file")?;
    let word = |off| u32::from_le_bytes(data[off..off + 4].try_into().unwrap());
    let text = |off: usize, size: usize| -> Result<String> {
        let data = &data[off..off + size];
        let end = data
            .iter()
            .position(|b| *b == 0)
            .context("unterminated MBA metadata")?;
        ensure!(end > 0, "empty MBA metadata");
        Ok(std::str::from_utf8(&data[..end])?.into())
    };
    let version = word(4);
    let abi = word(216);
    ensure!(
        word(0) == 0x4d424c41 && word(8) == 368 && matches!((version, abi), (1, 0) | (2, 1..)),
        "unsupported MBA metadata/ABI"
    );
    let id = text(24, 32)?;
    ensure!(
        regex::Regex::new(r"^[a-z][a-z0-9_.-]{0,30}$")?.is_match(&id),
        "invalid MBA identity"
    );
    Ok(Identity {
        id,
        version: text(120, 16)?,
        target: text(264, 64)?,
        metadata_version: version,
        interface_abi: (version == 2).then_some(abi),
        imports: elf.required_imports(bytes)?,
    })
}
pub fn load(path: &Path) -> Result<Bundle> {
    let encoded = host::read(path, 65536)?;
    let manifest: Manifest = serde_json::from_slice(&encoded)?;
    ensure!(
        manifest.schema == 1 && manifest.kind == "meshbus-app",
        "unsupported package schema/kind"
    );
    ensure!(
        manifest.resources.len() <= 31,
        "package has too many resources"
    );
    let root = path
        .parent()
        .context("package manifest has no directory")?
        .canonicalize()?;
    let mut names = BTreeSet::new();
    let mut destinations = BTreeSet::new();
    for file in manifest.files() {
        ensure!(
            names.insert(&file.path) && destinations.insert(&file.destination),
            "duplicate package file or destination"
        );
        payload(&root, file)?;
    }
    let info = identity(&payload(&root, &manifest.mba)?)?;
    ensure!(
        manifest.id == info.id && manifest.version == info.version,
        "manifest identity differs from MBA"
    );
    ensure!(
        manifest.mba.path.ends_with(".mba") && manifest.mba.destination.ends_with(".mba"),
        "invalid MBA filename"
    );
    ensure!(
        manifest
            .resources
            .iter()
            .all(|f| !f.destination.ends_with(".mba")),
        "resource cannot be another MBA"
    );
    ensure!(
        manifest.host.target == info.target
            && manifest.host.metadata_version == info.metadata_version
            && manifest.host.interface_abi == info.interface_abi,
        "manifest host differs from MBA"
    );
    ensure!(
        info.imports.is_subset(&manifest.host.requires),
        "manifest omits required host imports"
    );
    ensure!(
        !manifest.host.firmware.is_empty(),
        "missing firmware build identity"
    );
    if let Some(hash) = &manifest.host.image_sha256 {
        ensure!(
            hash.len() == 64 && hash.bytes().all(|b| b.is_ascii_hexdigit()),
            "invalid firmware image identity"
        );
    }
    Ok(Bundle {
        root,
        manifest,
        sha256: host::hash(&encoded),
    })
}
/// Bridge legacy SDK install.json (schema 1), preserving its destinations.
/// Reuse the real MBA and sidecar; never interpret resource payloads here.
pub fn collect(package: &Path) -> Result<PathBuf> {
    let mba = host::read(package, 64 * 1024 * 1024)?;
    let mut info = identity(&mba)?;
    let report: Value = serde_json::from_slice(&host::read(
        &package.with_extension("build.json"),
        1024 * 1024,
    )?)?;
    ensure!(
        report["target"] == info.target
            && report["metadata_version"] == info.metadata_version
            && report["interface_abi"] == serde_json::to_value(info.interface_abi)?,
        "build report differs from MBA"
    );
    let root = package
        .parent()
        .context("MBA has no directory")?
        .join(format!("{}.install", info.id));
    fs::create_dir_all(&root)?;
    let legacy = root.join("install.json");
    let mut resources = vec![];
    let mut mba_file = File {
        path: format!("{}.mba", info.id),
        destination: format!("/extra/apps/{0}/{0}.mba", info.id),
        length: mba.len() as u64,
        sha256: host::hash(&mba),
    };
    let has_resources = report
        .get("resource_collection")
        .map(|v| {
            v.as_bool()
                .context("invalid current resource collection state")
        })
        .transpose()?
        .unwrap_or_else(|| legacy.exists());
    if has_resources {
        let legacy: Value = serde_json::from_slice(&host::read(&legacy, 65536)?)?;
        ensure!(
            legacy["schema"] == 1
                && legacy["id"] == info.id
                && legacy["resource"]["identity"] == info.id,
            "unsupported legacy resource manifest or conflicting identity"
        );
        let files = legacy["files"].as_array().context("missing legacy files")?;
        ensure!(
            files.len() == 2,
            "legacy resource schema 1 requires MBA and sidecar"
        );
        let mut found_mba = false;
        for item in files {
            let file = File {
                path: metadata::string(item, "name")?.into(),
                destination: metadata::string(item, "destination")?.into(),
                length: item["length"]
                    .as_u64()
                    .context("missing legacy file size")?,
                sha256: metadata::string(item, "sha256")?.into(),
            };
            let raw = payload(&root, &file)?;
            if file.path == mba_file.path {
                ensure!(
                    !found_mba && raw == mba,
                    "legacy MBA differs from current build"
                );
                found_mba = true;
                mba_file = file;
            } else {
                ensure!(
                    file.path == format!("{}.abr", info.id)
                        && legacy["resource"]["file"] == file.path
                        && legacy["resource"]["destination"] == file.destination
                        && legacy["resource"]["length"] == file.length
                        && legacy["resource"]["sha256"] == file.sha256,
                    "legacy sidecar descriptor differs from files"
                );
                resources.push(file);
            }
        }
        ensure!(
            found_mba && resources.len() == 1,
            "incomplete legacy resource collection"
        );
    } else {
        fs::write(root.join(&mba_file.path), &mba)?;
    }
    if let Some(requires) = report.get("requires") {
        for value in requires
            .as_array()
            .context("invalid declared host requirements")?
        {
            let symbol = value
                .as_str()
                .and_then(|s| s.strip_prefix("symbol:"))
                .filter(|s| !s.is_empty())
                .context("unsupported declared host requirement")?;
            info.imports.insert(symbol.to_owned());
        }
    }
    let manifest = Manifest {
        schema: 1,
        kind: "meshbus-app".into(),
        id: info.id,
        version: info.version,
        mba: mba_file,
        resources,
        host: Host {
            target: info.target,
            firmware: metadata::string(&report["host"], "build-revision")?.into(),
            image_sha256: report["host"]["image-sha256"].as_str().map(str::to_owned),
            metadata_version: info.metadata_version,
            interface_abi: info.interface_abi,
            requires: info.imports,
        },
    };
    let path = root.join("package.json");
    host::json(&path, &manifest)?;
    load(&path)?;
    Ok(path)
}
