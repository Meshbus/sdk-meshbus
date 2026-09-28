// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MemoryRegion {
    pub name: &'static str,
    pub start: u64,
    pub end: u64,
}

/// Probe connection and physical memory facts. Product partitions belong to
/// the build-generated flash map, never to this SoC description.
#[derive(Clone, Copy, Debug)]
pub struct SocProfile {
    pub target: &'static str,
    pub frequency_khz: u32,
    pub part_register: u64,
    pub part_value: u32,
    pub erase_region: MemoryRegion,
}

pub fn profile(soc: &str) -> Option<&'static SocProfile> {
    match soc {
        "nrf54l15" => Some(&SocProfile {
            target: "nRF54L15",
            frequency_khz: 4_000,
            part_register: 0x00ff_c31c,
            part_value: 0x0005_4b15,
            erase_region: MemoryRegion {
                name: "internal RRAM",
                start: 0,
                end: 0x17d000,
            },
        }),
        _ => None,
    }
}
