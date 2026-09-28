// SPDX-License-Identifier: Apache-2.0
use crate::host;
use anyhow::{Result, ensure};
use std::{
    collections::BTreeSet,
    fs,
    io::Write,
    path::{Path, PathBuf},
};

pub fn relative(value: &str) -> Result<PathBuf> {
    ensure!(
        !value.starts_with('/')
            && !value.contains('\\')
            && !value.contains(':')
            && !value.chars().any(char::is_control),
        "unsafe archive path"
    );
    let mut path = PathBuf::new();
    for part in value.split('/') {
        match part {
            "" | "." => {}
            ".." => {
                ensure!(path.pop(), "path escapes archive root");
            }
            p => {
                let stem = p.split('.').next().unwrap_or("").to_ascii_uppercase();
                ensure!(
                    !["CON", "PRN", "AUX", "NUL"].contains(&stem.as_str())
                        && !(stem.len() == 4
                            && (stem.starts_with("COM") || stem.starts_with("LPT"))
                            && stem.as_bytes()[3].is_ascii_digit()),
                    "reserved Windows archive name"
                );
                ensure!(
                    !p.ends_with([' ', '.']) && !p.contains(['<', '>', '"', '|', '?', '*']),
                    "nonportable archive path"
                );
                path.push(p);
            }
        }
    }
    ensure!(!path.as_os_str().is_empty(), "empty archive path");
    Ok(path)
}
pub fn extract_edk(input: &Path, destination: &Path) -> Result<PathBuf> {
    ensure!(
        !destination.exists() || fs::read_dir(destination)?.next().is_none(),
        "extraction destination must be empty"
    );
    fs::create_dir_all(destination)?;
    let reader = lzma_rust2::XzReader::new(std::io::BufReader::new(fs::File::open(input)?), false);
    let mut tar = tar::Archive::new(reader);
    let mut seen = BTreeSet::new();
    let mut links = vec![];
    let mut total = 0u64;
    for e in tar.entries()? {
        let mut e = e?;
        let original = String::from_utf8(e.path_bytes().to_vec())?;
        ensure!(!original.split('/').any(|p| p == ".."), "archive traversal");
        let path = relative(&original)?;
        ensure!(
            path.starts_with("llext-edk") && seen.insert(path.to_string_lossy().to_lowercase()),
            "invalid root or duplicate archive member"
        );
        ensure!(seen.len() <= 100000, "too many EDK archive entries");
        let kind = e.header().entry_type();
        let dest = destination.join(&path);
        if kind.is_dir() {
            fs::create_dir_all(&dest)?;
            continue;
        }
        ensure!(
            path != Path::new("llext-edk"),
            "archive root must be a directory"
        );
        if kind.is_symlink() {
            let target = e
                .link_name()?
                .ok_or_else(|| anyhow::anyhow!("missing link target"))?
                .to_string_lossy()
                .into_owned();
            ensure!(
                !target.starts_with('/') && !target.contains(':') && !target.contains('\\'),
                "unsafe symlink"
            );
            let parent = path.parent().unwrap().to_string_lossy();
            let resolved = relative(&format!("{parent}/{target}"))?;
            ensure!(resolved.starts_with("llext-edk"), "symlink escapes EDK");
            links.push((dest, destination.join(resolved)));
            continue;
        }
        ensure!(
            kind.is_file(),
            "hard links and special archive entries are forbidden"
        );
        let size = e.size();
        total = total
            .checked_add(size)
            .ok_or_else(|| anyhow::anyhow!("archive size overflow"))?;
        ensure!(
            size <= 64 * 1024 * 1024 && total <= 256 * 1024 * 1024 && seen.len() <= 100000,
            "EDK archive exceeds bounds"
        );
        fs::create_dir_all(dest.parent().unwrap())?;
        let mut f = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&dest)?;
        ensure!(
            std::io::copy(&mut e, &mut f)? == size,
            "truncated archive member"
        );
    }
    for (link, target) in links {
        ensure!(
            target.is_file() && !target.is_symlink(),
            "dangling or chained EDK symlink"
        );
        let size = fs::metadata(&target)?.len();
        total += size;
        ensure!(total <= 256 * 1024 * 1024, "EDK archive exceeds bounds");
        fs::create_dir_all(link.parent().unwrap())?;
        ensure!(!link.exists(), "duplicate link path");
        fs::copy(target, link)?;
    }
    let root = destination.join("llext-edk");
    ensure!(root.is_dir(), "EDK root missing");
    Ok(root)
}
fn append_tree<W: Write>(
    builder: &mut tar::Builder<W>,
    root: &Path,
    archive_root: &str,
) -> Result<()> {
    fn walk<W: Write>(b: &mut tar::Builder<W>, p: &Path, name: &Path) -> Result<()> {
        let meta = fs::symlink_metadata(p)?;
        ensure!(
            !meta.file_type().is_symlink(),
            "archive may not contain symlinks"
        );
        let mut header = tar::Header::new_gnu();
        header.set_uid(0);
        header.set_gid(0);
        header.set_mtime(0);
        header.set_mode(if meta.is_dir() { 0o755 } else { 0o644 });
        header.set_size(if meta.is_dir() { 0 } else { meta.len() });
        header.set_entry_type(if meta.is_dir() {
            tar::EntryType::Directory
        } else {
            tar::EntryType::Regular
        });
        if meta.is_dir() {
            header.set_cksum();
            b.append_data(&mut header, name, std::io::empty())?;
            let mut children = fs::read_dir(p)?.collect::<std::io::Result<Vec<_>>>()?;
            children.sort_by_key(|e| e.file_name());
            for e in children {
                walk(b, &e.path(), &name.join(e.file_name()))?;
            }
        } else {
            ensure!(meta.is_file(), "special archive file");
            if p.file_name().is_some_and(|n| n == "meshbus") {
                header.set_mode(0o755);
            }
            header.set_cksum();
            b.append_data(&mut header, name, fs::File::open(p)?)?;
        }
        Ok(())
    }
    walk(builder, root, Path::new(archive_root))
}
pub fn pack(root: &Path, output: &Path, archive_root: &str) -> Result<()> {
    let parent = output.parent().unwrap_or(Path::new("."));
    fs::create_dir_all(parent)?;
    let temporary = tempfile::NamedTempFile::new_in(parent)?;
    let file = temporary.reopen()?;
    let name = output.to_string_lossy();
    if name.ends_with(".tar.xz") {
        let writer = lzma_rust2::XzWriter::new(file, lzma_rust2::XzOptions::with_preset(6))?;
        let mut tar = tar::Builder::new(writer);
        append_tree(&mut tar, root, archive_root)?;
        tar.into_inner()?.finish()?;
    } else if name.ends_with(".tar.gz") {
        let writer = flate2::GzBuilder::new()
            .mtime(0)
            .write(file, flate2::Compression::default());
        let mut tar = tar::Builder::new(writer);
        append_tree(&mut tar, root, archive_root)?;
        tar.into_inner()?.finish()?;
    } else if name.ends_with(".zip") {
        let mut zip = zip::ZipWriter::new(file);
        for path in host::files(root)? {
            let name = format!(
                "{archive_root}/{}",
                path.strip_prefix(root)?
                    .to_string_lossy()
                    .replace('\\', "/")
            );
            zip.start_file(
                name,
                zip::write::SimpleFileOptions::default()
                    .compression_method(zip::CompressionMethod::Deflated)
                    .unix_permissions(0o644),
            )?;
            zip.write_all(&fs::read(path)?)?;
        }
        zip.finish()?;
    } else {
        anyhow::bail!("archive must end in .tar.xz, .tar.gz or .zip");
    }
    temporary.persist(output)?;
    Ok(())
}
pub fn sidecar(path: &Path) -> Result<()> {
    fs::write(
        format!("{}.sha256", path.display()),
        format!(
            "{}  {}\n",
            host::hash(&fs::read(path)?),
            path.file_name().unwrap().to_string_lossy()
        ),
    )?;
    Ok(())
}
pub fn verify_sidecar(path: &Path) -> Result<()> {
    ensure!(
        fs::read_to_string(format!("{}.sha256", path.display()))?.trim()
            == format!(
                "{}  {}",
                host::hash(&fs::read(path)?),
                path.file_name().unwrap().to_string_lossy()
            ),
        "archive checksum mismatch"
    );
    Ok(())
}
