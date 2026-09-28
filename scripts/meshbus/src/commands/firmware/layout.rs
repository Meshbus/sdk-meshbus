// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

use super::{FirmwareError, ImageSegment};
use crate::soc::MemoryRegion;
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::{fs, path::Path};

pub(super) const MAX_IMAGE: u64 = 16 * 1024 * 1024;

#[derive(Clone, Debug, Deserialize)]
pub struct FlashMap {
    schema: u32,
    kind: String,
    pub target: String,
    pub soc: String,
    pub format: String,
    pub images: Vec<Image>,
}

#[derive(Clone, Debug, Deserialize)]
pub struct Image {
    pub domain: String,
    pub address: u64,
    pub size: u64,
    pub sha256: String,
    pub partition_address: u64,
    pub partition_size: u64,
}

fn invalid(message: impl Into<String>) -> FirmwareError {
    FirmwareError::Usage(message.into())
}

impl FlashMap {
    pub fn load(path: &Path) -> Result<Self, FirmwareError> {
        let size = fs::metadata(path)
            .map_err(|source| FirmwareError::Read {
                path: path.into(),
                source,
            })?
            .len();
        if size > 4 * 1024 * 1024 {
            return Err(invalid("flash map exceeds 4 MiB"));
        }
        let data = fs::read(path).map_err(|source| FirmwareError::Read {
            path: path.into(),
            source,
        })?;
        let map: Self = serde_json::from_slice(&data)
            .map_err(|error| invalid(format!("invalid flash map: {error}")))?;
        map.validate()?;
        Ok(map)
    }

    fn validate(&self) -> Result<(), FirmwareError> {
        if self.schema != 1
            || self.kind != "firmware"
            || self.target.is_empty()
            || self.soc.is_empty()
            || !self.target.split('/').skip(1).any(|s| s == self.soc)
        {
            return Err(invalid("invalid firmware flash-map schema, target or SoC"));
        }
        let expected: &[&str] = match self.format.as_str() {
            "mcuboot" => &["mcuboot", "app"],
            "uf2" => &["app"],
            _ => return Err(invalid("unsupported flash-map image format")),
        };
        if self.images.len() != expected.len()
            || expected
                .iter()
                .any(|d| self.images.iter().filter(|i| &i.domain == d).count() != 1)
        {
            return Err(invalid(
                "flash map has missing, duplicate or unsupported image domains",
            ));
        }
        for image in &self.images {
            let end = image.address.checked_add(image.size);
            let partition_end = image.partition_address.checked_add(image.partition_size);
            if image.size == 0
                || image.size > MAX_IMAGE
                || image.partition_size == 0
                || image.partition_size > MAX_IMAGE
                || image.address < image.partition_address
                || end.is_none()
                || partition_end.is_none()
                || end > partition_end
                || partition_end.unwrap() > 0x1_0000_0000
                || image.sha256.len() != 64
                || !image.sha256.bytes().all(|b| b.is_ascii_hexdigit())
            {
                return Err(invalid(format!(
                    "invalid {} image/partition bounds or digest",
                    image.domain
                )));
            }
        }
        let mut sorted = self.images.iter().collect::<Vec<_>>();
        sorted.sort_by_key(|i| i.partition_address);
        if sorted
            .windows(2)
            .any(|p| p[0].partition_address + p[0].partition_size > p[1].partition_address)
        {
            return Err(invalid("overlapping flash-map partitions"));
        }
        let start = sorted.iter().map(|i| i.address).min().unwrap();
        let end = sorted.iter().map(|i| i.address + i.size).max().unwrap();
        if end - start > MAX_IMAGE {
            return Err(invalid("flash-map image span exceeds 16 MiB"));
        }
        Ok(())
    }

    fn selected(&self, role: &str) -> Result<Vec<&Image>, FirmwareError> {
        if role == "provision" {
            if self.format != "mcuboot" {
                return Err(invalid(
                    "application-only firmware has no complete provisioning image",
                ));
            }
            return Ok(self.images.iter().collect());
        }
        let domain = if role == "bootloader" {
            "mcuboot"
        } else {
            role
        };
        self.images
            .iter()
            .find(|i| i.domain == domain)
            .map(|i| vec![i])
            .ok_or_else(|| invalid(format!("flash map has no {role} image")))
    }

    pub fn region(&self, role: &str) -> Result<MemoryRegion, FirmwareError> {
        let images = self.selected(role)?;
        Ok(MemoryRegion {
            name: "manifest image",
            start: images.iter().map(|i| i.address).min().unwrap(),
            end: images.iter().map(|i| i.address + i.size).max().unwrap(),
        })
    }

    /// Require the selected bytes to reproduce the build's native BIN digests.
    /// Sparse HEX holes and merged-BIN gaps have erased (0xff) semantics.
    pub fn resolve(
        &self,
        role: &str,
        segments: &[ImageSegment],
    ) -> Result<Vec<ImageSegment>, FirmwareError> {
        let region = self.region(role)?;
        let mut bytes = vec![0xff; (region.end - region.start) as usize];
        for segment in segments {
            if segment.start < region.start || segment.end() > region.end {
                return Err(invalid(format!(
                    "{role} image range is outside flash-map payload bounds"
                )));
            }
            let offset = (segment.start - region.start) as usize;
            bytes[offset..offset + segment.data.len()].copy_from_slice(&segment.data);
        }
        let mut resolved = Vec::new();
        for image in self.selected(role)? {
            let start = (image.address - region.start) as usize;
            let data = &mut bytes[start..start + image.size as usize];
            if format!("{:x}", Sha256::digest(&*data)) != image.sha256.to_ascii_lowercase() {
                return Err(invalid(format!(
                    "{} image SHA-256 differs from flash map",
                    image.domain
                )));
            }
            resolved.push(ImageSegment {
                start: image.address,
                data: data.to_vec(),
            });
            data.fill(0xff);
        }
        if bytes.iter().any(|&b| b != 0xff) {
            return Err(invalid("provisioning image writes outside declared images"));
        }
        Ok(resolved)
    }
}
