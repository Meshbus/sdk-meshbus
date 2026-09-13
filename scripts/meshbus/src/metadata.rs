// SPDX-License-Identifier: Apache-2.0
use crate::host;
use anyhow::{Result, ensure};
use serde_json::Value;
use std::{io::Cursor, path::Path};

pub fn string<'a>(v: &'a Value, key: &str) -> Result<&'a str> {
    v[key]
        .as_str()
        .filter(|s| !s.is_empty())
        .ok_or_else(|| anyhow::anyhow!("missing or invalid string field: {key}"))
}
fn fixed(out: &mut Vec<u8>, s: &str, max: usize) -> Result<()> {
    ensure!(
        s.len() <= max && !s.contains('\0'),
        "metadata string too long or contains NUL"
    );
    out.extend(s.as_bytes());
    out.resize(out.len() + max + 1 - s.len(), 0);
    Ok(())
}
fn semver(s: &str) -> bool {
    regex::Regex::new(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z][0-9A-Za-z.-]*)?(?:\+[0-9A-Za-z][0-9A-Za-z.-]*)?$").unwrap().is_match(s)
}
pub fn icon(data: &[u8]) -> Result<Vec<u8>> {
    let mut decoder = png::Decoder::new(Cursor::new(data));
    decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16);
    let mut reader = decoder.read_info()?;
    ensure!(
        reader.info().width == 10 && reader.info().height == 10,
        "icon must be exactly 10x10 pixels"
    );
    let mut buffer = vec![
        0;
        reader
            .output_buffer_size()
            .ok_or_else(|| anyhow::anyhow!("PNG too large"))?
    ];
    let info = reader.next_frame(&mut buffer)?;
    let channels = info.color_type.samples();
    let mut pixels = vec![];
    for p in buffer[..info.buffer_size()].chunks_exact(channels) {
        pixels.push(match info.color_type {
            png::ColorType::Grayscale => [p[0], p[0], p[0], 255],
            png::ColorType::GrayscaleAlpha => [p[0], p[0], p[0], p[1]],
            png::ColorType::Rgb => [p[0], p[1], p[2], 255],
            png::ColorType::Rgba => [p[0], p[1], p[2], p[3]],
            _ => anyhow::bail!("unsupported PNG color type"),
        });
    }
    let transparent = pixels.iter().any(|p| p[3] < 128);
    let mut out = vec![0; 20];
    for (i, p) in pixels.iter().enumerate() {
        let foreground = if transparent {
            p[3] >= 128
        } else {
            (u32::from(p[0]) * 299 + u32::from(p[1]) * 587 + u32::from(p[2]) * 114) / 1000 < 128
        };
        if foreground {
            out[i / 10 * 2 + (i % 10) / 8] |= 1 << ((i % 10) % 8);
        }
    }
    Ok(out)
}
pub fn build(
    data: &Value,
    source: &Path,
    metadata: u32,
    edk_version: &str,
    target: &str,
    heap: u32,
    interface_abi: Option<u32>,
) -> Result<Vec<u8>> {
    ensure!(
        matches!((metadata, interface_abi), (1, None) | (2, Some(1..))),
        "metadata v1 requires no ABI; v2 requires a positive interface ABI"
    );
    let allowed = ["id", "name", "version", "entry-point", "stack-size"];
    for key in data
        .as_object()
        .ok_or_else(|| anyhow::anyhow!("llext.yaml must be a mapping"))?
        .keys()
    {
        ensure!(
            allowed.contains(&key.as_str()),
            "unsupported or build-injected metadata field: {key}"
        );
    }
    let id = string(data, "id")?;
    ensure!(
        regex::Regex::new(r"^[a-z][a-z0-9_.-]{0,30}$")?.is_match(id),
        "invalid extension id"
    );
    let entry = string(data, "entry-point")?;
    ensure!(
        regex::Regex::new(r"^[A-Za-z_][A-Za-z0-9_]{0,62}$")?.is_match(entry),
        "invalid entry point"
    );
    let name = string(data, "name")?;
    let version = string(data, "version")?;
    ensure!(
        semver(version) && semver(edk_version),
        "version must be SemVer"
    );
    ensure!(
        !target.is_empty()
            && target.len() <= 63
            && target
                .bytes()
                .all(|b| b.is_ascii_alphanumeric() || b"._:/+-".contains(&b)),
        "invalid generated target"
    );
    let stack = u32::try_from(
        data["stack-size"]
            .as_u64()
            .ok_or_else(|| anyhow::anyhow!("invalid stack-size"))?,
    )?;
    ensure!(
        stack > 0 && heap > 0 && metadata > 0,
        "stack, heap and metadata version must be positive"
    );
    let size = 368;
    let magic = 0x4d424c41;
    let mut out = vec![];
    for n in [magic, metadata, size, stack, heap] {
        out.extend(n.to_le_bytes());
    }
    out.extend(20u32.to_le_bytes());
    fixed(&mut out, id, 31)?;
    fixed(&mut out, name, 63)?;
    fixed(&mut out, version, 15)?;
    fixed(&mut out, entry, 63)?;
    fixed(&mut out, edk_version, 15)?;
    out.extend(interface_abi.unwrap_or(0).to_le_bytes());
    out.extend([0; 44]);
    fixed(&mut out, target, 63)?;
    let path = source.join("icon.png");
    let png = if path.exists() {
        host::read(&path, 1024 * 1024)?
    } else {
        include_bytes!("../assets/app_icon.png").to_vec()
    };
    out.extend(icon(&png)?);
    out.extend([0; 20]);
    ensure!(
        out.len() == size as usize,
        "metadata layout size mismatch: {} != {size}",
        out.len()
    );
    Ok(out)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn interface_abi_is_explicit_and_versioned() {
        let app = serde_json::json!({"id":"abi", "name":"ABI", "version":"1.0.0",
            "entry-point":"main", "stack-size":1024});
        let root = tempfile::tempdir().unwrap();
        let old = build(&app, root.path(), 1, "1.0.0", "board/cpu", 4096, None).unwrap();
        let new = build(
            &app,
            root.path(),
            2,
            "1.0.0",
            "board/cpu",
            4096,
            Some(0x12345678),
        )
        .unwrap();
        assert_eq!(&old[216..264], &[0; 48]);
        assert_eq!(&new[216..220], &[0x78, 0x56, 0x34, 0x12]);
        assert_eq!(&old[264..], &new[264..]);
        for (version, abi) in [(1, Some(1)), (2, None), (2, Some(0)), (3, Some(1))] {
            assert!(build(&app, root.path(), version, "1.0.0", "board/cpu", 4096, abi).is_err());
        }
    }
    #[test]
    fn rejects_injected_fields() {
        let v = serde_json::json!({"metadata-version":1});
        assert!(build(&v, Path::new("."), 1, "1.0.0", "a", 1, None).is_err());
    }

    #[test]
    fn application_metadata_has_no_type_or_service_fields() {
        let app = serde_json::json!({
            "id":"example", "name":"Example", "version":"1.0.0",
            "entry-point":"example_main", "stack-size":1024
        });
        let root = tempfile::tempdir().unwrap();
        assert_eq!(
            build(&app, root.path(), 1, "1.0.0", "board/cpu", 4096, None)
                .unwrap()
                .len(),
            368
        );
        for (key, value) in [
            ("type", "service"),
            ("type", "app"),
            ("description", "background"),
        ] {
            let mut invalid = app.clone();
            invalid[key] = value.into();
            let error =
                build(&invalid, root.path(), 1, "1.0.0", "board/cpu", 4096, None).unwrap_err();
            assert!(
                error
                    .to_string()
                    .contains("unsupported or build-injected metadata field")
            );
        }
    }
}
