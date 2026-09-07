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
use crate::device::{DeviceProfile, profile};

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
    /// Exact Meshbus device profile name.
    #[arg(long, value_parser = ["idea_mesh_tracker_c2"])]
    device: String,
    /// MCUboot .bin or Intel HEX image.
    #[arg(long)]
    bootloader: Option<PathBuf>,
    /// Application .bin or Intel HEX image.
    #[arg(long)]
    app: Option<PathBuf>,
    /// Merged provisioning Intel HEX image.
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
    /// Erase all internal RRAM before programming a complete image set.
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
    pub profile: &'static DeviceProfile,
    pub images: Vec<FirmwareImage>,
    pub erase_all: bool,
}

impl FirmwarePlan {
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
            println!("{}", format_plan(&plan));
            let selected = crate::probe::select_probe(arguments.probe.as_deref())?;
            println!("Probe: {}", crate::probe::description(&selected));
            confirm(arguments.yes)?;
            crate::probe::program(&plan, selected)?;
            println!("Firmware programmed and verified successfully");
            Ok(())
        }
    }
}

fn build_plan(arguments: &ImageArguments, erase_all: bool) -> Result<FirmwarePlan, FirmwareError> {
    let selected_profile = profile(&arguments.device).ok_or_else(|| {
        FirmwareError::Usage(format!(
            "unknown Meshbus device profile: {}",
            arguments.device
        ))
    })?;

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
        images.push(read_image(role, path, selected_profile)?);
    }
    check_overlaps(&images)?;

    let plan = FirmwarePlan {
        profile: selected_profile,
        images,
        erase_all,
    };
    if plan.erase_all && !plan.has_complete_boot_set() {
        return Err(FirmwareError::Usage(
            "--erase-all requires --provision or both --bootloader and --app; an application alone would leave the device without MCUboot".into(),
        ));
    }
    Ok(plan)
}

fn read_image(
    role: &'static str,
    path: &Path,
    selected_profile: &DeviceProfile,
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

    let region = selected_profile.image_region(role).unwrap();
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

    for segment in &segments {
        if segment.start < region.start || segment.end() > region.end {
            return Err(FirmwareError::Usage(format!(
                "{role} image range 0x{:08x}-0x{:08x} is outside {} {} range 0x{:08x}-0x{:08x}",
                segment.start,
                segment.end() - 1,
                selected_profile.name,
                region.name,
                region.start,
                region.end - 1,
            )));
        }
    }

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
        "chip erase all internal RRAM; verify programmed data and all unwritten flash"
    } else {
        "image-covered sectors"
    };
    let mut lines = vec![
        format!("Device: {}", plan.profile.name),
        format!(
            "Target: {} ({}, {} MHz SWD)",
            plan.profile.part_name,
            plan.profile.target,
            plan.profile.frequency_khz / 1000
        ),
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
    use std::io::Write;
    use tempfile::NamedTempFile;

    fn image_arguments(path: &Path) -> ImageArguments {
        ImageArguments {
            device: "idea_mesh_tracker_c2".into(),
            bootloader: None,
            app: Some(path.into()),
            provision: None,
        }
    }

    #[test]
    fn binary_uses_profile_base_address() {
        let mut file = NamedTempFile::with_suffix(".bin").unwrap();
        file.write_all(&[1, 2, 3, 4]).unwrap();
        let plan = build_plan(&image_arguments(file.path()), false).unwrap();
        assert_eq!(plan.images[0].start(), 0x20000);
        assert_eq!(plan.images[0].end(), 0x20004);
    }

    #[test]
    fn hex_outside_app_partition_is_rejected() {
        let mut file = NamedTempFile::with_suffix(".hex").unwrap();
        writeln!(file, ":01FFFF0042BF").unwrap();
        writeln!(file, ":00000001FF").unwrap();
        let error = build_plan(&image_arguments(file.path()), false).unwrap_err();
        assert!(
            error
                .to_string()
                .contains("outside idea_mesh_tracker_c2 application range")
        );
    }

    #[test]
    fn erase_all_requires_complete_boot_set() {
        let mut file = NamedTempFile::with_suffix(".bin").unwrap();
        file.write_all(b"app").unwrap();
        let error = build_plan(&image_arguments(file.path()), true).unwrap_err();
        assert!(
            error
                .to_string()
                .contains("would leave the device without MCUboot")
        );
    }

    #[test]
    fn invalid_hex_checksum_is_rejected() {
        let mut file = NamedTempFile::with_suffix(".hex").unwrap();
        writeln!(file, ":010000004200").unwrap();
        writeln!(file, ":00000001FF").unwrap();
        let error = build_plan(&image_arguments(file.path()), false).unwrap_err();
        assert!(error.to_string().contains("checksum is invalid"));
    }

    #[test]
    fn provisioning_cannot_be_combined_with_app() {
        let mut app = NamedTempFile::with_suffix(".bin").unwrap();
        app.write_all(b"app").unwrap();
        let mut provision = NamedTempFile::with_suffix(".hex").unwrap();
        writeln!(provision, ":0100000042BD").unwrap();
        writeln!(provision, ":00000001FF").unwrap();
        let arguments = ImageArguments {
            device: "idea_mesh_tracker_c2".into(),
            bootloader: None,
            app: Some(app.path().into()),
            provision: Some(provision.path().into()),
        };
        let error = build_plan(&arguments, false).unwrap_err();
        assert!(error.to_string().contains("cannot be combined"));
    }

    #[test]
    fn provisioning_can_restore_persistent_rram_after_full_erase() {
        let mut provision = NamedTempFile::with_suffix(".hex").unwrap();
        writeln!(provision, ":020000040012E8").unwrap();
        writeln!(provision, ":0100000042BD").unwrap();
        writeln!(provision, ":00000001FF").unwrap();
        let arguments = ImageArguments {
            device: "idea_mesh_tracker_c2".into(),
            bootloader: None,
            app: None,
            provision: Some(provision.path().into()),
        };

        let plan = build_plan(&arguments, true).unwrap();
        assert_eq!(plan.images[0].start(), 0x120000);
        assert_eq!(plan.images[0].end(), 0x120001);
    }

    #[test]
    fn format_plan_describes_full_flash_erase_and_verification() {
        let mut file = NamedTempFile::with_suffix(".bin").unwrap();
        file.write_all(b"app").unwrap();
        let mut arguments = image_arguments(file.path());
        arguments.bootloader = Some(file.path().into());
        let plan = build_plan(&arguments, true).unwrap();
        assert!(format_plan(&plan).contains(
            "chip erase all internal RRAM; verify programmed data and all unwritten flash"
        ));
    }

    #[test]
    fn memory_region_is_end_exclusive() {
        let region = crate::device::MemoryRegion {
            name: "test",
            start: 1,
            end: 4,
        };
        assert_eq!(region.end - region.start, 3);
    }
}
