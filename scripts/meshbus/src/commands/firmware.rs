// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

use std::collections::BTreeMap;
use std::fs;
use std::io::{self, IsTerminal, Write};
use std::path::{Path, PathBuf};

use clap::{Args, Subcommand};
use sha2::{Digest, Sha256};
use thiserror::Error;

use crate::commands::connect::{ConnectError, FirmwareDeltaArgs, run_firmware_delta};
mod layout;
use crate::soc::SocProfile;
use layout::FlashMap;

#[derive(Debug, Args)]
pub struct FirmwareArgs {
    #[command(subcommand)]
    command: FirmwareCommand,
}

#[derive(Debug, Subcommand)]
enum FirmwareCommand {
    /// Create, inspect, and authenticate offline delta packages.
    Package(crate::package::PackageArgs),
    /// Transfer and activate a signed delta package over Meshbus Management.
    Delta(FirmwareDeltaArgs),
    /// List connected debug probes.
    Probes,
    /// Validate images and print the programming plan without connecting.
    Inspect(ImageArguments),
    /// Validate and program firmware.
    Flash(FlashArguments),
}

#[derive(Debug, Args)]
struct ImageArguments {
    /// Build-generated flash-map.json from the matching firmware archive.
    #[arg(long)]
    manifest: PathBuf,
    /// MCUboot .bin or Intel HEX image.
    #[arg(long)]
    bootloader: Option<PathBuf>,
    /// Application .bin or Intel HEX image.
    #[arg(long)]
    app: Option<PathBuf>,
    /// Complete merged provisioning .bin or Intel HEX image.
    #[arg(long)]
    provision: Option<PathBuf>,
}

#[derive(Debug, Args)]
struct FlashArguments {
    #[command(flatten)]
    images: ImageArguments,
    /// Exact debug-probe serial number; required when multiple probes are connected.
    #[arg(long)]
    probe: Option<String>,
    /// Erase all internal nonvolatile memory before programming a complete boot set.
    #[arg(long)]
    erase_all: bool,
    /// Skip interactive programming confirmation.
    #[arg(long)]
    yes: bool,
}

#[derive(Debug, Error)]
pub enum FirmwareError {
    #[error("{0}")]
    Usage(String),
    #[error("{0}")]
    Operation(String),
    #[error("{0}")]
    Package(String),
    #[error("unable to read firmware image {path}: {source}")]
    Read { path: PathBuf, source: io::Error },
    #[error(transparent)]
    Delta(#[from] ConnectError),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ImageSegment {
    pub start: u64,
    pub data: Vec<u8>,
}

impl ImageSegment {
    pub fn end(&self) -> u64 {
        self.start + self.data.len() as u64
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ImageFormat {
    Bin,
    Hex,
}

impl ImageFormat {
    fn as_str(self) -> &'static str {
        match self {
            Self::Bin => "bin",
            Self::Hex => "hex",
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FirmwareImage {
    pub role: &'static str,
    pub path: PathBuf,
    pub format: ImageFormat,
    pub digest: String,
    pub segments: Vec<ImageSegment>,
}

impl FirmwareImage {
    pub fn start(&self) -> u64 {
        self.segments
            .iter()
            .map(|segment| segment.start)
            .min()
            .unwrap()
    }

    pub fn end(&self) -> u64 {
        self.segments.iter().map(ImageSegment::end).max().unwrap()
    }

    pub fn programmed_size(&self) -> usize {
        self.segments.iter().map(|segment| segment.data.len()).sum()
    }
}

#[derive(Clone, Debug)]
pub struct FirmwarePlan {
    pub layout: FlashMap,
    pub images: Vec<FirmwareImage>,
    pub erase_all: bool,
}

impl FirmwarePlan {
    pub fn programming_soc(&self) -> Result<&'static SocProfile, FirmwareError> {
        let soc = crate::soc::profile(&self.layout.soc).ok_or_else(|| {
            FirmwareError::Usage(format!(
                "SWD programming is unsupported for SoC {}",
                self.layout.soc
            ))
        })?;
        for image in &self.layout.images {
            if image.partition_address < soc.erase_region.start
                || image.partition_address + image.partition_size > soc.erase_region.end
            {
                return Err(FirmwareError::Usage(
                    "flash-map partition is outside physical SoC memory".into(),
                ));
            }
        }
        Ok(soc)
    }

    fn has_complete_boot_set(&self) -> bool {
        self.images.iter().any(|image| image.role == "provision")
            || (["bootloader", "app"]
                .iter()
                .all(|role| self.images.iter().any(|image| image.role == *role)))
    }
}

pub fn run_firmware(args: FirmwareArgs) -> Result<(), FirmwareError> {
    match args.command {
        FirmwareCommand::Package(args) => {
            crate::package::run(args).map_err(|e| FirmwareError::Package(e.to_string()))
        }
        FirmwareCommand::Delta(arguments) => {
            run_firmware_delta(arguments)?;
            Ok(())
        }
        FirmwareCommand::Probes => crate::probe::print_probes(),
        FirmwareCommand::Inspect(images) => {
            let plan = build_plan(&images, false)?;
            println!("{}", format_plan(&plan));
            Ok(())
        }
        FirmwareCommand::Flash(arguments) => {
            let plan = build_plan(&arguments.images, arguments.erase_all)?;
            let soc = plan.programming_soc()?;
            println!("{}", format_plan(&plan));
            let selected = crate::probe::select_probe(arguments.probe.as_deref())?;
            println!("Probe: {}", crate::probe::description(&selected));
            confirm(arguments.yes)?;
            crate::probe::program(&plan, soc, selected)?;
            println!("Firmware programmed and verified successfully");
            Ok(())
        }
    }
}

fn build_plan(arguments: &ImageArguments, erase_all: bool) -> Result<FirmwarePlan, FirmwareError> {
    let layout = FlashMap::load(&arguments.manifest)?;

    let provided = [
        ("bootloader", arguments.bootloader.as_ref()),
        ("app", arguments.app.as_ref()),
        ("provision", arguments.provision.as_ref()),
    ]
    .into_iter()
    .filter_map(|(role, path)| path.map(|path| (role, path)))
    .collect::<Vec<_>>();

    if provided.is_empty() {
        return Err(FirmwareError::Usage(
            "provide at least one of --bootloader, --app, or --provision".into(),
        ));
    }
    if arguments.provision.is_some() && provided.len() != 1 {
        return Err(FirmwareError::Usage(
            "--provision cannot be combined with --bootloader or --app".into(),
        ));
    }

    let mut images = Vec::with_capacity(provided.len());
    for (role, path) in provided {
        images.push(read_image(role, path, &layout)?);
    }
    check_overlaps(&images)?;

    let plan = FirmwarePlan {
        layout,
        images,
        erase_all,
    };
    if plan.erase_all && (plan.layout.format != "mcuboot" || !plan.has_complete_boot_set()) {
        return Err(FirmwareError::Usage(
            "--erase-all requires --provision or both --bootloader and --app; an application alone would leave the device without MCUboot".into(),
        ));
    }
    Ok(plan)
}

fn read_image(
    role: &'static str,
    path: &Path,
    layout: &FlashMap,
) -> Result<FirmwareImage, FirmwareError> {
    let resolved = fs::canonicalize(path).map_err(|source| FirmwareError::Read {
        path: path.to_path_buf(),
        source,
    })?;
    if !resolved.is_file() {
        return Err(FirmwareError::Usage(format!(
            "firmware image does not exist: {}",
            resolved.display()
        )));
    }
    if fs::metadata(&resolved)
        .map_err(|source| FirmwareError::Read {
            path: resolved.clone(),
            source,
        })?
        .len()
        > layout::MAX_IMAGE * 4
    {
        return Err(FirmwareError::Usage(
            "firmware file exceeds size bound".into(),
        ));
    }
    let raw = fs::read(&resolved).map_err(|source| FirmwareError::Read {
        path: resolved.clone(),
        source,
    })?;
    if raw.is_empty() {
        return Err(FirmwareError::Usage(format!(
            "firmware image is empty: {}",
            resolved.display()
        )));
    }

    let region = layout.region(role)?;
    let suffix = resolved
        .extension()
        .and_then(|value| value.to_str())
        .unwrap_or_default()
        .to_ascii_lowercase();
    let (format, segments) = match suffix.as_str() {
        "bin" => (
            ImageFormat::Bin,
            vec![ImageSegment {
                start: region.start,
                data: raw.clone(),
            }],
        ),
        "hex" | "ihex" => (ImageFormat::Hex, parse_intel_hex(&raw, &resolved)?),
        _ => {
            return Err(FirmwareError::Usage(format!(
                "unsupported firmware format {:?}; expected .bin, .hex, .ihex",
                resolved.extension().unwrap_or_default()
            )));
        }
    };

    if format == ImageFormat::Bin && raw.len() as u64 != region.end - region.start {
        return Err(FirmwareError::Usage(
            "binary size differs from flash map".into(),
        ));
    }
    let segments = layout.resolve(role, &segments)?;

    Ok(FirmwareImage {
        role,
        path: resolved,
        format,
        digest: format!("{:x}", Sha256::digest(&raw)),
        segments,
    })
}

fn parse_intel_hex(raw: &[u8], path: &Path) -> Result<Vec<ImageSegment>, FirmwareError> {
    let text = std::str::from_utf8(raw).map_err(|error| {
        FirmwareError::Usage(format!(
            "invalid Intel HEX image {}: {error}",
            path.display()
        ))
    })?;
    let mut bytes = BTreeMap::<u64, u8>::new();
    let mut base = 0_u64;
    let mut eof = false;

    for (index, source_line) in text.lines().enumerate() {
        let line_number = index + 1;
        let line = source_line.trim();
        if line.is_empty() {
            continue;
        }
        let encoded = line
            .strip_prefix(':')
            .ok_or_else(|| hex_error(path, line_number, "record does not begin with ':'"))?;
        if encoded.len() % 2 != 0 {
            return Err(hex_error(path, line_number, "record has odd hex length"));
        }
        let record = (0..encoded.len())
            .step_by(2)
            .map(|offset| {
                u8::from_str_radix(&encoded[offset..offset + 2], 16)
                    .map_err(|_| hex_error(path, line_number, "record contains non-hex data"))
            })
            .collect::<Result<Vec<_>, _>>()?;
        if record.len() < 5 {
            return Err(hex_error(path, line_number, "record is too short"));
        }
        let length = record[0] as usize;
        if record.len() != length + 5 {
            return Err(hex_error(
                path,
                line_number,
                "record length does not match payload",
            ));
        }
        if record
            .iter()
            .fold(0_u8, |sum, value| sum.wrapping_add(*value))
            != 0
        {
            return Err(hex_error(path, line_number, "record checksum is invalid"));
        }
        let address = u16::from_be_bytes([record[1], record[2]]) as u64;
        let kind = record[3];
        let data = &record[4..4 + length];

        match kind {
            0x00 => {
                for (offset, value) in data.iter().enumerate() {
                    let absolute = base + address + offset as u64;
                    if let Some(previous) = bytes.insert(absolute, *value)
                        && previous != *value
                    {
                        return Err(hex_error(
                            path,
                            line_number,
                            "overlapping records contain conflicting data",
                        ));
                    }
                }
            }
            0x01 => {
                if length != 0 {
                    return Err(hex_error(path, line_number, "EOF record contains data"));
                }
                eof = true;
                break;
            }
            0x02 => {
                if length != 2 {
                    return Err(hex_error(
                        path,
                        line_number,
                        "invalid segment address record",
                    ));
                }
                base = u16::from_be_bytes([data[0], data[1]]) as u64 * 16;
            }
            0x03 | 0x05 => {}
            0x04 => {
                if length != 2 {
                    return Err(hex_error(
                        path,
                        line_number,
                        "invalid linear address record",
                    ));
                }
                base = (u16::from_be_bytes([data[0], data[1]]) as u64) << 16;
            }
            _ => {
                return Err(hex_error(path, line_number, "unsupported record type"));
            }
        }
    }

    if !eof {
        return Err(FirmwareError::Usage(format!(
            "invalid Intel HEX image {}: missing EOF record",
            path.display()
        )));
    }
    if bytes.is_empty() {
        return Err(FirmwareError::Usage(format!(
            "firmware image is empty: {}",
            path.display()
        )));
    }

    let mut segments = Vec::<ImageSegment>::new();
    for (address, value) in bytes {
        if let Some(segment) = segments.last_mut()
            && segment.end() == address
        {
            segment.data.push(value);
        } else {
            segments.push(ImageSegment {
                start: address,
                data: vec![value],
            });
        }
    }
    Ok(segments)
}

fn hex_error(path: &Path, line: usize, message: &str) -> FirmwareError {
    FirmwareError::Usage(format!(
        "invalid Intel HEX image {} at line {line}: {message}",
        path.display()
    ))
}

fn check_overlaps(images: &[FirmwareImage]) -> Result<(), FirmwareError> {
    let mut segments = images
        .iter()
        .flat_map(|image| {
            image
                .segments
                .iter()
                .map(move |segment| (segment.start, segment.end(), image))
        })
        .collect::<Vec<_>>();
    segments.sort_by_key(|segment| segment.0);
    for pair in segments.windows(2) {
        let previous = pair[0];
        let current = pair[1];
        if current.0 < previous.1 {
            return Err(FirmwareError::Usage(format!(
                "firmware images overlap at 0x{:08x}: {} ({}) and {} ({})",
                current.0,
                previous.2.role,
                previous.2.path.display(),
                current.2.role,
                current.2.path.display(),
            )));
        }
    }
    Ok(())
}

fn format_plan(plan: &FirmwarePlan) -> String {
    let erase = if plan.erase_all {
        "chip erase all internal nonvolatile memory; verify programmed data and all unwritten flash"
    } else {
        "image-covered sectors"
    };
    let mut lines = vec![
        format!("Target: {}", plan.layout.target),
        format!("SoC: {}", plan.layout.soc),
        format!("Format: {}", plan.layout.format),
        format!("Erase: {erase}"),
        "Images:".into(),
    ];
    for image in &plan.images {
        lines.push(format!("  {}: {}", image.role, image.path.display()));
        lines.push(format!(
            "    format={} range=0x{:08x}-0x{:08x} bytes={}",
            image.format.as_str(),
            image.start(),
            image.end() - 1,
            image.programmed_size()
        ));
        lines.push(format!("    sha256={}", image.digest));
    }
    lines.join("\n")
}

fn confirm(assume_yes: bool) -> Result<(), FirmwareError> {
    if assume_yes {
        return Ok(());
    }
    if !io::stdin().is_terminal() {
        return Err(FirmwareError::Usage(
            "interactive confirmation is unavailable; pass --yes".into(),
        ));
    }
    print!("Program this device? [y/N] ");
    io::stdout()
        .flush()
        .map_err(|error| FirmwareError::Operation(error.to_string()))?;
    let mut answer = String::new();
    io::stdin()
        .read_line(&mut answer)
        .map_err(|error| FirmwareError::Operation(error.to_string()))?;
    if matches!(answer.trim().to_ascii_lowercase().as_str(), "y" | "yes") {
        Ok(())
    } else {
        Err(FirmwareError::Usage(
            "firmware programming cancelled".into(),
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{Value, json};
    use tempfile::TempDir;

    struct Fixture {
        root: TempDir,
        manifest: Value,
    }
    impl Fixture {
        fn new() -> Self {
            let root = TempDir::new().unwrap();
            fs::write(root.path().join("app.bin"), b"app!").unwrap();
            fs::write(root.path().join("boot.bin"), b"boot").unwrap();
            let image = |domain: &str, address, data: &[u8]| {
                json!({
                    "domain": domain, "address": address, "size": data.len(),
                    "sha256": format!("{:x}", Sha256::digest(data)),
                    "partition_address": address, "partition_size": 0x1000,
                })
            };
            Self {
                root,
                manifest: json!({
                    "schema": 1, "kind": "firmware", "format": "mcuboot",
                    "target": "fixture/nrf54l15/cpuapp", "soc": "nrf54l15",
                    "images": [image("mcuboot", 0, b"boot"), image("app", 0x20000, b"app!")],
                }),
            }
        }
        fn args(&self) -> ImageArguments {
            let path = self.root.path().join("flash-map.json");
            fs::write(&path, serde_json::to_vec(&self.manifest).unwrap()).unwrap();
            ImageArguments {
                manifest: path,
                bootloader: None,
                app: Some(self.root.path().join("app.bin")),
                provision: None,
            }
        }
        fn full(&self) -> ImageArguments {
            let mut args = self.args();
            args.bootloader = Some(self.root.path().join("boot.bin"));
            args
        }
    }

    #[test]
    fn product_layout_changes_without_cli_board_registration() {
        let mut f = Fixture::new();
        for (target, address) in [
            ("first/nrf54l15/cpuapp", 0x20000),
            ("second/nrf54l15/cpuapp", 0x50000),
        ] {
            f.manifest["target"] = json!(target);
            f.manifest["images"][1]["address"] = json!(address);
            f.manifest["images"][1]["partition_address"] = json!(address);
            let plan = build_plan(&f.args(), false).unwrap();
            assert_eq!(plan.images[0].start(), address);
            assert!(format_plan(&plan).contains(target));
            assert_eq!(plan.programming_soc().unwrap().target, "nRF54L15");
        }
    }

    #[test]
    fn changed_or_truncated_image_is_rejected() {
        let f = Fixture::new();
        for data in [b"bad!".as_slice(), b"app".as_slice()] {
            fs::write(f.root.path().join("app.bin"), data).unwrap();
            let error = build_plan(&f.args(), false).unwrap_err().to_string();
            assert!(error.contains("SHA-256") || error.contains("size differs"));
        }
    }

    #[test]
    fn compact_flash_map_keeps_image_validation_and_programming_plan() {
        let fixture = Fixture::new();
        let mut manifest = fixture.manifest.clone();
        manifest["schema"] = json!(2);
        manifest["license_materials"] = json!({"notice":"NOTICE.txt", "sha256":"f".repeat(64)});
        let args = fixture.args();
        fs::write(&args.manifest, serde_json::to_vec(&manifest).unwrap()).unwrap();
        let plan = build_plan(&args, false).unwrap();
        assert_eq!(plan.images.len(), 1);
    }

    #[test]
    fn invalid_manifest_fails_before_programming() {
        let original = Fixture::new();
        let mut variants = vec![];
        for (key, value) in [
            ("schema", json!(3)),
            ("kind", json!("cli")),
            ("soc", json!("other")),
            ("format", json!("unknown")),
        ] {
            let mut value_map = original.manifest.clone();
            value_map[key] = value;
            variants.push(value_map);
        }
        let mut missing = original.manifest.clone();
        missing.as_object_mut().unwrap().remove("soc");
        variants.push(missing);
        for (field, value) in [
            ("address", json!(u64::MAX)),
            ("size", json!(0)),
            ("size", json!(0x2000)),
            ("partition_size", json!(u64::MAX)),
            ("sha256", json!("invalid")),
        ] {
            let mut value_map = original.manifest.clone();
            value_map["images"][1][field] = value;
            variants.push(value_map);
        }
        let mut duplicate = original.manifest.clone();
        duplicate["images"][1]["domain"] = json!("mcuboot");
        variants.push(duplicate);
        let mut overlap = original.manifest.clone();
        overlap["images"][1]["partition_address"] = json!(0);
        overlap["images"][1]["partition_size"] = json!(0x21000);
        variants.push(overlap);
        for manifest in variants {
            let f = Fixture {
                root: TempDir::new().unwrap(),
                manifest,
            };
            let error = build_plan(&f.args(), false).unwrap_err();
            assert!(!matches!(error, FirmwareError::Read { .. }), "{error}");
        }
    }

    #[test]
    fn physical_soc_bounds_are_independent_of_product_metadata() {
        let mut f = Fixture::new();
        f.manifest["images"][1]["partition_size"] = json!(0x180000);
        let plan = build_plan(&f.args(), false).unwrap();
        assert!(
            plan.programming_soc()
                .unwrap_err()
                .to_string()
                .contains("physical SoC memory")
        );
    }

    #[test]
    fn unsupported_soc_can_be_inspected_but_not_programmed() {
        let mut f = Fixture::new();
        f.manifest["soc"] = json!("unsupported");
        f.manifest["target"] = json!("fixture/unsupported");
        let plan = build_plan(&f.args(), false).unwrap();
        assert!(
            plan.programming_soc()
                .unwrap_err()
                .to_string()
                .contains("unsupported")
        );
    }

    #[test]
    fn erase_all_requires_complete_boot_set() {
        let f = Fixture::new();
        assert!(
            build_plan(&f.args(), true)
                .unwrap_err()
                .to_string()
                .contains("complete")
                || build_plan(&f.args(), true)
                    .unwrap_err()
                    .to_string()
                    .contains("MCUboot")
        );
        let plan = build_plan(&f.full(), true).unwrap();
        assert!(format_plan(&plan).contains("all internal nonvolatile memory"));
    }

    #[test]
    fn uf2_layout_does_not_authorize_bootloader_or_chip_erase() {
        let mut f = Fixture::new();
        f.manifest["format"] = json!("uf2");
        f.manifest["images"].as_array_mut().unwrap().remove(0);
        assert!(build_plan(&f.args(), false).is_ok());
        assert!(build_plan(&f.args(), true).is_err());
        assert!(build_plan(&f.full(), false).is_err());
    }

    #[test]
    fn hex_payload_is_checked_by_address_and_native_digest() {
        let f = Fixture::new();
        let mut args = f.args();
        let path = f.root.path().join("app.hex");
        fs::write(&path, ":020000040002F8\n:04000000617070219A\n:00000001FF\n").unwrap();
        args.app = Some(path.clone());
        assert_eq!(build_plan(&args, false).unwrap().images[0].end(), 0x20004);
        fs::write(&path, ":01FFFF0042BF\n:00000001FF\n").unwrap();
        assert!(
            build_plan(&args, false)
                .unwrap_err()
                .to_string()
                .contains("outside")
        );
        fs::write(&path, ":010000004200\n:00000001FF\n").unwrap();
        assert!(
            build_plan(&args, false)
                .unwrap_err()
                .to_string()
                .contains("checksum")
        );
    }

    #[test]
    fn sparse_hex_holes_are_programmed_as_verified_erased_bytes() {
        let mut f = Fixture::new();
        let expected = [0x61, 0xff, 0xff, 0x21];
        f.manifest["images"][1]["sha256"] = json!(format!("{:x}", Sha256::digest(expected)));
        let mut args = f.args();
        let path = f.root.path().join("sparse.hex");
        fs::write(
            &path,
            ":020000040002F8\n:01000000619E\n:0100030021DB\n:00000001FF\n",
        )
        .unwrap();
        args.app = Some(path);
        let plan = build_plan(&args, false).unwrap();
        assert_eq!(plan.images[0].segments.len(), 1);
        assert_eq!(plan.images[0].segments[0].data, expected);
    }

    #[test]
    fn provision_requires_exact_images_and_erased_gaps() {
        let f = Fixture::new();
        let mut args = f.args();
        let path = f.root.path().join("full.bin");
        let mut data = vec![0xff; 0x20004];
        data[..4].copy_from_slice(b"boot");
        data[0x20000..].copy_from_slice(b"app!");
        fs::write(&path, &data).unwrap();
        args.provision = Some(path.clone());
        assert!(
            build_plan(&args, true)
                .unwrap_err()
                .to_string()
                .contains("cannot be combined")
        );
        args.app = None;
        let plan = build_plan(&args, true).unwrap();
        assert_eq!(plan.images[0].segments.len(), 2);
        assert_eq!(plan.images[0].programmed_size(), 8);
        data[0x1000] = 42;
        fs::write(&path, &data).unwrap();
        assert!(
            build_plan(&args, true)
                .unwrap_err()
                .to_string()
                .contains("outside declared")
        );
        data[0x1000] = 0xff;
        data[0] = 42;
        fs::write(&path, &data).unwrap();
        assert!(
            build_plan(&args, true)
                .unwrap_err()
                .to_string()
                .contains("SHA-256")
        );
    }
}
