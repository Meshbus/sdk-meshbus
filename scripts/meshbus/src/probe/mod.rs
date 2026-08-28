// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

use std::time::Duration;
use std::{cmp, ops::Range};

use probe_rs::flashing::{DownloadOptions, FlashProgress};
use probe_rs::probe::{DebugProbeInfo, WireProtocol, list::Lister};
use probe_rs::{MemoryInterface, Permissions};

use crate::commands::firmware::{FirmwareError, FirmwarePlan};
use crate::device::MemoryRegion;

pub fn description(probe: &DebugProbeInfo) -> String {
    format!(
        "{} [{}]",
        probe.identifier,
        probe.serial_number.as_deref().unwrap_or("no-serial")
    )
}

pub fn print_probes() -> Result<(), FirmwareError> {
    let probes = Lister::new().list_all();
    if probes.is_empty() {
        println!("No debug probes found");
    } else {
        for probe in probes {
            println!("{}", description(&probe));
        }
    }
    Ok(())
}

pub fn select_probe(requested: Option<&str>) -> Result<DebugProbeInfo, FirmwareError> {
    let probes = Lister::new().list_all();
    if let Some(requested) = requested {
        return probes
            .into_iter()
            .find(|probe| probe.serial_number.as_deref() == Some(requested))
            .ok_or_else(|| FirmwareError::Usage(format!("debug probe not found: {requested}")));
    }
    match probes.len() {
        0 => Err(FirmwareError::Usage("no debug probe is connected".into())),
        1 => Ok(probes.into_iter().next().unwrap()),
        _ => {
            let choices = probes
                .iter()
                .map(|probe| format!("  {}", description(probe)))
                .collect::<Vec<_>>()
                .join("\n");
            Err(FirmwareError::Usage(format!(
                "more than one debug probe is connected; select one with --probe:\n{choices}"
            )))
        }
    }
}

pub fn program(plan: &FirmwarePlan, selected: DebugProbeInfo) -> Result<(), FirmwareError> {
    let mut probe = selected
        .open()
        .map_err(|error| operation("unable to open debug probe", error))?;
    probe
        .select_protocol(WireProtocol::Swd)
        .map_err(|error| operation("unable to select SWD", error))?;
    probe
        .set_speed(plan.profile.frequency_khz)
        .map_err(|error| operation("unable to set SWD speed", error))?;

    let permissions = if plan.erase_all {
        Permissions::new().allow_erase_all()
    } else {
        Permissions::new()
    };
    let mut session = probe
        .attach(plan.profile.target, permissions)
        .map_err(|error| operation("unable to attach to target", error))?;

    {
        let mut core = session
            .core(0)
            .map_err(|error| operation("unable to access target core", error))?;
        core.reset_and_halt(Duration::from_secs(1))
            .map_err(|error| operation("unable to reset and halt target", error))?;
        let actual_part = core
            .read_word_32(plan.profile.part_register)
            .map_err(|error| operation("unable to read target part register", error))?;
        if actual_part != plan.profile.part_value {
            return Err(FirmwareError::Operation(format!(
                "target MCU mismatch: expected {} part 0x{:08x}, read 0x{actual_part:08x}",
                plan.profile.part_name, plan.profile.part_value
            )));
        }
    }

    if plan.erase_all {
        let mut progress = FlashProgress::empty();
        probe_rs::flashing::erase_all(&mut session, &mut progress, false)
            .map_err(|error| operation("failed to chip-erase internal RRAM", error))?;
    }

    let mut loader = session.target().flash_loader();
    for image in &plan.images {
        for segment in &image.segments {
            loader
                .add_data(segment.start, &segment.data)
                .map_err(|error| operation("unable to stage firmware image", error))?;
        }
    }

    let mut options = DownloadOptions::default();
    options.keep_unwritten_bytes = !plan.erase_all;
    options.skip_erase = plan.erase_all;
    options.verify = true;
    loader
        .commit(&mut session, options)
        .map_err(|error| operation("probe-rs programming failed", error))?;

    if plan.erase_all {
        verify_unwritten_flash_erased(&mut session, plan)?;
    }

    session
        .core(0)
        .and_then(|mut core| core.reset())
        .map_err(|error| operation("firmware was programmed but target reset failed", error))?;
    Ok(())
}

fn verify_unwritten_flash_erased(
    session: &mut probe_rs::Session,
    plan: &FirmwarePlan,
) -> Result<(), FirmwareError> {
    let mut core = session
        .core(0)
        .map_err(|error| operation("unable to verify full-flash erase", error))?;
    let mut buffer = vec![0_u8; 4096];
    let programmed = plan.images.iter().flat_map(|image| {
        image
            .segments
            .iter()
            .map(|segment| segment.start..segment.end())
    });

    for range in unwritten_ranges(plan.profile.erase_region, programmed) {
        let mut address = range.start;
        while address < range.end {
            let count = usize::try_from((range.end - address).min(buffer.len() as u64)).unwrap();
            core.read_8(address, &mut buffer[..count])
                .map_err(|error| operation("unable to read unwritten flash", error))?;
            if let Some(offset) = first_non_erased_offset(&buffer[..count]) {
                return Err(FirmwareError::Operation(format!(
                    "full-flash erase verification failed: unwritten flash at 0x{:08x} contains 0x{:02x}, expected 0xff",
                    address + offset as u64,
                    buffer[offset]
                )));
            }
            address += count as u64;
        }
    }
    Ok(())
}

fn unwritten_ranges(
    erase_region: MemoryRegion,
    programmed: impl IntoIterator<Item = Range<u64>>,
) -> Vec<Range<u64>> {
    let mut programmed = programmed
        .into_iter()
        .filter_map(|range| {
            let start = cmp::max(range.start, erase_region.start);
            let end = cmp::min(range.end, erase_region.end);
            (start < end).then_some(start..end)
        })
        .collect::<Vec<_>>();
    programmed.sort_unstable_by_key(|range| range.start);

    let mut unwritten = Vec::new();
    let mut cursor = erase_region.start;
    for range in programmed {
        if cursor < range.start {
            unwritten.push(cursor..range.start);
        }
        cursor = cmp::max(cursor, range.end);
        if cursor >= erase_region.end {
            break;
        }
    }
    if cursor < erase_region.end {
        unwritten.push(cursor..erase_region.end);
    }
    unwritten
}

fn first_non_erased_offset(bytes: &[u8]) -> Option<usize> {
    bytes.iter().position(|byte| *byte != 0xff)
}

fn operation(context: &str, error: impl std::fmt::Display) -> FirmwareError {
    FirmwareError::Operation(format!("{context}: {error}"))
}

#[cfg(test)]
mod tests {
    use super::{first_non_erased_offset, unwritten_ranges};
    use crate::device::MemoryRegion;

    #[test]
    fn erased_data_verification_finds_first_programmed_byte() {
        assert_eq!(first_non_erased_offset(&[0xff, 0xff, 0x00, 0xff]), Some(2));
        assert_eq!(first_non_erased_offset(&[0xff; 4]), None);
    }

    #[test]
    fn full_erase_verification_covers_every_unwritten_gap() {
        let erase_region = MemoryRegion {
            name: "test flash",
            start: 0,
            end: 100,
        };
        let ranges = unwritten_ranges(erase_region, [10..20, 30..40, 18..32]);
        assert_eq!(ranges, vec![0..10, 40..100]);
    }

    #[test]
    fn full_erase_verification_clips_programmed_ranges_to_flash() {
        let erase_region = MemoryRegion {
            name: "test flash",
            start: 10,
            end: 20,
        };
        let ranges = unwritten_ranges(erase_region, [0..12, 18..30]);
        assert_eq!(ranges, vec![12..18]);
    }
}
