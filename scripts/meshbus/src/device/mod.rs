// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

mod idea_mesh_tracker_c2;

pub use idea_mesh_tracker_c2::IDEA_MESH_TRACKER_C2;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MemoryRegion {
    pub name: &'static str,
    pub start: u64,
    pub end: u64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DeviceProfile {
    pub name: &'static str,
    pub target: &'static str,
    pub frequency_khz: u32,
    pub part_register: u64,
    pub part_value: u32,
    pub part_name: &'static str,
    pub erase_region: MemoryRegion,
    pub bootloader_region: MemoryRegion,
    pub app_region: MemoryRegion,
    pub provision_region: MemoryRegion,
}

impl DeviceProfile {
    pub fn image_region(&self, role: &str) -> Option<MemoryRegion> {
        match role {
            "bootloader" => Some(self.bootloader_region),
            "app" => Some(self.app_region),
            "provision" => Some(self.provision_region),
            _ => None,
        }
    }
}

pub fn profile(name: &str) -> Option<&'static DeviceProfile> {
    match name {
        "idea_mesh_tracker_c2" => Some(&IDEA_MESH_TRACKER_C2),
        _ => None,
    }
}
