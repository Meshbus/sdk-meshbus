// SPDX-License-Identifier: Apache-2.0
use anyhow::{Context, Result, ensure};
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::{
    fs,
    path::{Path, PathBuf},
    process::Command,
};

pub fn hash(data: &[u8]) -> String {
    format!("{:x}", Sha256::digest(data))
}
pub fn hex(data: &[u8]) -> String {
    data.iter().map(|b| format!("{b:02x}")).collect()
}
pub fn unhex(s: &str) -> Result<Vec<u8>> {
    ensure!(
        s.is_ascii() && s.len().is_multiple_of(2),
        "invalid hexadecimal value"
    );
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).map_err(Into::into))
        .collect()
}
pub fn read(path: &Path, max: u64) -> Result<Vec<u8>> {
    ensure!(
        fs::symlink_metadata(path)?.file_type().is_file(),
        "not a regular file: {}",
        path.display()
    );
    ensure!(
        fs::metadata(path)?.len() <= max,
        "file exceeds size limit: {}",
        path.display()
    );
    fs::read(path).with_context(|| format!("read {}", path.display()))
}
pub fn json(path: &Path, value: &impl Serialize) -> Result<()> {
    let mut bytes = serde_json::to_vec_pretty(value)?;
    bytes.push(b'\n');
    fs::write(path, bytes)?;
    Ok(())
}
pub fn print(value: &impl Serialize) -> Result<()> {
    println!("{}", serde_json::to_string_pretty(value)?);
    Ok(())
}
pub fn files(root: &Path) -> Result<Vec<PathBuf>> {
    let mut out = Vec::new();
    for entry in fs::read_dir(root)? {
        let e = entry?;
        let ty = e.file_type()?;
        ensure!(
            !ty.is_symlink(),
            "symlink is not permitted: {}",
            e.path().display()
        );
        if ty.is_dir() {
            out.extend(files(&e.path())?);
        } else {
            ensure!(ty.is_file(), "special file in artifact");
            out.push(e.path());
        }
    }
    out.sort();
    Ok(out)
}
pub fn run(command: &mut Command) -> Result<()> {
    let result = command
        .status()
        .context("unable to start external build tool")?;
    ensure!(result.success(), "external tool exited with {result}");
    Ok(())
}
pub fn output(command: &mut Command) -> Result<String> {
    let out = command.output().context("unable to start external tool")?;
    ensure!(
        out.status.success(),
        "external tool failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    Ok(String::from_utf8(out.stdout)?.trim().to_owned())
}
pub fn checksums(root: &Path) -> Result<()> {
    let mut text = String::new();
    for file in files(root)? {
        if file == root.join("SHA256SUMS") {
            continue;
        }
        text.push_str(&format!(
            "{}  {}\n",
            hash(&fs::read(&file)?),
            file.strip_prefix(root)?
                .to_string_lossy()
                .replace('\\', "/")
        ));
    }
    fs::write(root.join("SHA256SUMS"), text)?;
    Ok(())
}

pub fn verify_checksums(root: &Path) -> Result<()> {
    // Reject links anywhere in the input tree, including directory components.
    files(root)?;
    let text = String::from_utf8(read(&root.join("SHA256SUMS"), 4 * 1024 * 1024)?)?;
    let mut seen = std::collections::BTreeSet::new();
    for line in text.lines() {
        let (digest, name) = line.split_once("  ").context("invalid SHA256SUMS line")?;
        ensure!(
            digest.len() == 64 && digest.bytes().all(|b| b.is_ascii_hexdigit()),
            "invalid SHA256 digest"
        );
        let path = crate::archive::relative(name)?;
        ensure!(
            seen.insert(path.clone()) && path != Path::new("SHA256SUMS"),
            "duplicate or self-referencing checksum"
        );
        ensure!(
            hash(&read(&root.join(path), 1024 * 1024 * 1024)?) == digest,
            "checksum mismatch: {name}"
        );
    }
    ensure!(!seen.is_empty(), "empty checksum file");
    Ok(())
}
