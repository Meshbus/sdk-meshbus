// SPDX-License-Identifier: Apache-2.0
//! Local release sources and immutable project input identities. No network IO.
use crate::{archive, edk, host};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::BTreeMap,
    fs,
    io::Read,
    path::{Path, PathBuf},
};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Artifact {
    pub path: PathBuf,
    pub sha256: String,
}
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Release {
    pub target: String,
    pub firmware: String,
    pub profile: String,
    pub host_platform: String,
    pub edk: Artifact,
    pub toolchain: Artifact,
    pub sdk: Option<Artifact>,
    pub tools: BTreeMap<String, Artifact>,
    pub metadata_version: u32,
    pub interface_abi: Option<u32>,
}
#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Source {
    pub schema: u32,
    pub minimum_cli: String,
    pub releases: Vec<Release>,
}
#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Selection {
    pub schema: u32,
    pub source: PathBuf,
    pub source_sha256: String,
    pub release: Release,
}
#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Lock {
    pub schema: u32,
    pub project_sha256: String,
    pub selection_sha256: String,
    pub selection: Selection,
    pub release: Release,
}

pub fn platform() -> String {
    format!("{}-{}", std::env::consts::OS, std::env::consts::ARCH)
}
fn version(s: &str) -> Result<[u32; 3]> {
    let parts = s
        .split('.')
        .map(str::parse)
        .collect::<std::result::Result<Vec<u32>, _>>()?;
    ensure!(parts.len() == 3, "invalid CLI version");
    Ok([parts[0], parts[1], parts[2]])
}
fn ignored(name: &str, at_root: bool) -> bool {
    matches!(name, ".git" | "__pycache__" | ".DS_Store")
        || (at_root && (matches!(name, ".scratch" | "build" | "out") || name.starts_with("build-")))
}
fn walk(root: &Path, current: &Path, out: &mut Vec<PathBuf>) -> Result<()> {
    for e in fs::read_dir(current)? {
        let e = e?;
        if ignored(&e.file_name().to_string_lossy(), current == root) {
            continue;
        }
        let p = e.path();
        let ty = e.file_type()?;
        if ty.is_symlink() {
            let real = p.canonicalize()?;
            ensure!(
                real.starts_with(root),
                "input symlink escapes root: {}",
                p.display()
            );
            ensure!(
                real.is_file(),
                "directory symlinks are unsupported: {}",
                p.display()
            );
            out.push(p);
        } else if ty.is_dir() {
            walk(root, &p, out)?;
        } else {
            ensure!(ty.is_file(), "special input file");
            out.push(p);
        }
    }
    Ok(())
}
pub fn digest(path: &Path) -> Result<String> {
    let root = path
        .canonicalize()
        .with_context(|| format!("missing local input: {}", path.display()))?;
    let mut hash = Sha256::new();
    let files = if root.is_file() {
        vec![root.clone()]
    } else {
        let mut f = vec![];
        walk(&root, &root, &mut f)?;
        f.sort();
        f
    };
    for file in files {
        if root.is_dir() {
            let name = file.strip_prefix(&root)?.to_string_lossy();
            hash.update((name.len() as u64).to_le_bytes());
            hash.update(name.as_bytes());
        }
        let mut input = fs::File::open(&file)?;
        if root.is_dir() {
            hash.update(input.metadata()?.len().to_le_bytes());
        }
        let mut buffer = [0; 65536];
        loop {
            let n = input.read(&mut buffer)?;
            if n == 0 {
                break;
            }
            hash.update(&buffer[..n]);
        }
    }
    Ok(format!("{:x}", hash.finalize()))
}
impl Artifact {
    pub fn local(path: &Path) -> Result<Self> {
        let path = path.canonicalize()?;
        Ok(Self {
            sha256: digest(&path)?,
            path,
        })
    }
    pub fn verify(&self) -> Result<()> {
        ensure!(
            digest(&self.path)? == self.sha256,
            "local input changed: {}; restore input or explicitly update",
            self.path.display()
        );
        Ok(())
    }
}
pub fn write_json(path: &Path, value: &impl Serialize) -> Result<()> {
    let parent = path.parent().unwrap_or(Path::new("."));
    fs::create_dir_all(parent)?;
    let mut temp = tempfile::NamedTempFile::new_in(parent)?;
    use std::io::Write;
    serde_json::to_writer_pretty(&mut temp, value)?;
    temp.write_all(b"\n")?;
    temp.as_file().sync_all()?;
    temp.persist(path).map_err(|e| e.error)?;
    Ok(())
}
pub fn load<T: for<'a> Deserialize<'a>>(path: &Path) -> Result<T> {
    Ok(serde_json::from_slice(&host::read(path, 4 * 1024 * 1024)?)?)
}
pub fn edk_manifest(path: &Path) -> Result<serde_json::Value> {
    if path.is_dir() {
        return edk::manifest(path);
    }
    let tmp = tempfile::tempdir()?;
    let root = archive::extract_edk(path, tmp.path())?;
    edk::manifest(&root)
}
pub fn source(path: &Path) -> Result<Source> {
    let value: Source = load(path)?;
    ensure!(value.schema == 1, "unsupported release source schema");
    ensure!(
        version(env!("CARGO_PKG_VERSION"))? >= version(&value.minimum_cli)?,
        "release source requires CLI {}",
        value.minimum_cli
    );
    let mut seen = std::collections::BTreeSet::new();
    for r in &value.releases {
        ensure!(
            seen.insert((&r.target, &r.profile, &r.firmware, &r.host_platform)),
            "duplicate release identity"
        );
    }
    Ok(value)
}
pub fn select(
    project: &Path,
    path: &Path,
    target: &str,
    firmware: &str,
    profile: &str,
) -> Result<Selection> {
    let path = path.canonicalize()?;
    let src = source(&path)?;
    let mut matches = src.releases.into_iter().filter(|r| {
        r.target == target
            && r.firmware == firmware
            && r.profile == profile
            && r.host_platform == platform()
    });
    let mut release = matches
        .next()
        .context("no exact release/EDK for target, firmware, profile and host platform")?;
    ensure!(matches.next().is_none(), "ambiguous release");
    let parent = path.parent().unwrap();
    for artifact in std::iter::once(&mut release.edk)
        .chain(std::iter::once(&mut release.toolchain))
        .chain(release.sdk.iter_mut())
        .chain(release.tools.values_mut())
    {
        if artifact.path.is_relative() {
            artifact.path = parent.join(&artifact.path);
        }
    }
    release.edk.verify()?;
    check_edk(&release)?;
    let selection = Selection {
        schema: 1,
        source_sha256: host::hash(&fs::read(&path)?),
        source: path,
        release,
    };
    write_json(&project.join(".meshbus-target.json"), &selection)?;
    Ok(selection)
}
pub fn check_edk(release: &Release) -> Result<()> {
    let m = edk_manifest(&release.edk.path)?;
    ensure!(
        m["target"] == release.target
            && m["host"]["build-revision"] == release.firmware
            && m["metadata-version"] == release.metadata_version
            && m["interface-abi"] == serde_json::to_value(release.interface_abi)?,
        "EDK identity differs from selected release"
    );
    ensure!(
        m["exported-symbols"].is_array(),
        "project EDK requires actual exported-symbols inventory"
    );
    Ok(())
}
fn copy_tree(source: &Path, dest: &Path) -> Result<()> {
    let root = source.canonicalize()?;
    let mut files = vec![];
    walk(&root, &root, &mut files)?;
    fn directories(source: &Path, dest: &Path, at_root: bool) -> Result<()> {
        fs::create_dir_all(dest)?;
        for entry in fs::read_dir(source)? {
            let entry = entry?;
            if !ignored(&entry.file_name().to_string_lossy(), at_root)
                && entry.file_type()?.is_dir()
            {
                directories(&entry.path(), &dest.join(entry.file_name()), false)?;
            }
        }
        Ok(())
    }
    directories(&root, dest, true)?;
    for f in files {
        let target = dest.join(f.strip_prefix(&root)?);
        fs::create_dir_all(target.parent().unwrap())?;
        fs::copy(f, target)?;
    }
    Ok(())
}
fn cache(artifact: &Artifact, directory: &Path) -> Result<Artifact> {
    fs::create_dir_all(directory)?;
    let directory = directory.canonicalize()?;
    let dest = directory.join(&artifact.sha256);
    if dest.exists() {
        let result = Artifact {
            path: dest,
            sha256: artifact.sha256.clone(),
        };
        result.verify()?;
        return Ok(result);
    }
    artifact.verify()?;
    let temp = tempfile::tempdir_in(&directory)?;
    let input = temp.path().join("input");
    if artifact.path.is_dir() {
        fs::create_dir(&input)?;
        copy_tree(&artifact.path, &input)?;
    } else {
        fs::copy(&artifact.path, &input)?;
    }
    let copy = Artifact {
        path: input.clone(),
        sha256: artifact.sha256.clone(),
    };
    copy.verify()?;
    match fs::rename(&input, &dest) {
        Ok(()) => {}
        Err(e) => {
            if !dest.exists() {
                return Err(e.into());
            }
        }
    }
    let result = Artifact {
        path: dest,
        sha256: artifact.sha256.clone(),
    };
    result.verify()?;
    Ok(result)
}
pub fn project_hash(root: &Path) -> Result<String> {
    Ok(host::hash(&fs::read(root.join("llext.yaml"))?))
}
pub fn sync(root: &Path, cache_dir: &Path, locked: bool, update: bool) -> Result<Lock> {
    super::project::read(root)?;
    let path = root.join("meshbus.lock");
    if path.exists() && !update {
        let mut lock = read_lock(root)?;
        let before = serde_json::to_vec(&lock)?;
        // Restore exactly the locked identities; no resolver or index refresh.
        if !lock.release.edk.path.exists() {
            lock.release.edk = cache(&lock.selection.release.edk, cache_dir)?;
        }
        if let Some(sdk) = &lock.release.sdk {
            if !sdk.path.exists() {
                lock.release.sdk = Some(cache(
                    lock.selection
                        .release
                        .sdk
                        .as_ref()
                        .context("lock has no SDK origin")?,
                    cache_dir,
                )?);
            }
        }
        verify(&lock)?;
        if serde_json::to_vec(&lock)? != before {
            ensure!(
                !locked,
                "cache location changed; run sync without --locked to record its path"
            );
            write_json(&path, &lock)?;
        }
        return Ok(lock);
    }
    ensure!(
        !locked,
        "--locked requires an existing current lock; run sync first"
    );
    let selection: Selection = load(&root.join(".meshbus-target.json"))?;
    ensure!(selection.schema == 1, "unsupported target selection schema");
    ensure!(
        host::hash(&fs::read(&selection.source)?) == selection.source_sha256,
        "release source changed; select target again before updating"
    );
    let mut release = selection.release.clone();
    release.edk = cache(&release.edk, cache_dir)?;
    if let Some(sdk) = &release.sdk {
        release.sdk = Some(cache(sdk, cache_dir)?);
    }
    // Tools stay explicitly local; no hidden multi-gigabyte copy or download.
    let lock = Lock {
        schema: 1,
        project_sha256: project_hash(root)?,
        selection_sha256: host::hash(&serde_json::to_vec(&selection)?),
        selection,
        release,
    };
    verify(&lock)?;
    write_json(&path, &lock)?;
    Ok(lock)
}
pub fn read_lock(root: &Path) -> Result<Lock> {
    let lock: Lock = load(&root.join("meshbus.lock"))
        .context("project is not synchronized; select target and run app sync")?;
    ensure!(lock.schema == 1, "unsupported lock schema");
    ensure!(
        lock.project_sha256 == project_hash(root)?,
        "project declaration changed; explicitly run app update"
    );
    ensure!(
        host::hash(&serde_json::to_vec(&lock.selection)?) == lock.selection_sha256,
        "lock selection digest mismatch"
    );
    let path = root.join(".meshbus-target.json");
    if path.exists() {
        let selection: Selection = load(&path)?;
        ensure!(
            host::hash(&serde_json::to_vec(&selection)?) == lock.selection_sha256,
            "selected target changed; explicitly run app update"
        );
    }
    Ok(lock)
}
pub fn verify(lock: &Lock) -> Result<()> {
    let r = &lock.release;
    ensure!(
        r.host_platform == platform(),
        "lock belongs to another host platform; import inputs for this host"
    );
    r.edk.verify()?;
    r.toolchain.verify()?;
    if let Some(sdk) = &r.sdk {
        sdk.verify()?;
    }
    for name in ["cmake", "ninja", "python"] {
        r.tools
            .get(name)
            .with_context(|| format!("missing tool: {name}"))?
            .verify()?;
    }
    check_edk(r)?;
    check_compiler(&r.edk.path, &r.toolchain.path)
}
pub fn check_compiler(edk: &Path, toolchain: &Path) -> Result<()> {
    let m = edk_manifest(edk)?;
    if let Some(identity) = m["toolchain"]["identity"].as_str() {
        let compiler = crate::llext::tool(toolchain, "gcc")?;
        let version = host::output(std::process::Command::new(compiler).arg("-dumpfullversion"))?;
        ensure!(
            identity.ends_with(&format!("/arm-zephyr-eabi-gcc-{version}")),
            "compiler version differs from EDK: EDK {identity}, local GCC {version}"
        );
    }
    Ok(())
}
