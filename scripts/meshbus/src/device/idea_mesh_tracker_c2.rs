// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

use super::{DeviceProfile, MemoryRegion};

pub const IDEA_MESH_TRACKER_C2: DeviceProfile = DeviceProfile {
    name: "idea_mesh_tracker_c2",
    target: "nRF54L15",
    frequency_khz: 4_000,
    part_register: 0x00ff_c31c,
    part_value: 0x0005_4b15,
    part_name: "nRF54L15",
    erase_region: MemoryRegion {
        name: "internal RRAM",
        start: 0x000000,
        end: 0x17d000,
    },
    bootloader_region: MemoryRegion {
        name: "MCUboot",
        start: 0x000000,
        end: 0x020000,
    },
    app_region: MemoryRegion {
        name: "application",
        start: 0x020000,
        end: 0x120000,
    },
    provision_region: MemoryRegion {
        name: "provisioning image",
        start: 0x000000,
        end: 0x165000,
    },
};
