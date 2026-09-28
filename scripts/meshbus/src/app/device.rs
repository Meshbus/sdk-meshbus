// SPDX-License-Identifier: Apache-2.0
use super::inputs;
use crate::commands::connect::DeviceClient;
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HostInfo {
    pub protocol_version: u32,
    pub device_id: String,
    pub target: String,
    pub build_revision: String,
    pub metadata_version: u32,
    pub image_sha256: String,
}
#[derive(Debug, Serialize, Deserialize)]
pub struct Binding {
    pub schema: u32,
    pub port: String,
    pub usb_serial: Option<String>,
    pub host: HostInfo,
}
fn ports() -> Result<Vec<(String, Option<String>)>> {
    Ok(serialport::available_ports()?
        .into_iter()
        .map(|p| {
            let serial = match p.port_type {
                serialport::SerialPortType::UsbPort(usb) => usb.serial_number,
                _ => None,
            };
            (p.port_name, serial)
        })
        .collect())
}
pub fn resolve(
    selector: &str,
    available: &[(String, Option<String>)],
) -> Result<(String, Option<String>)> {
    let matches: Vec<_> = available
        .iter()
        .filter(|(port, serial)| port == selector || serial.as_deref() == Some(selector))
        .filter(|(port, serial)| {
            // macOS enumerates dial-in and call-out names for the same UART.
            // Collapse only that exact alias pair, never distinct interfaces.
            port == selector
                || !port.strip_prefix("/dev/tty.").is_some_and(|suffix| {
                    available.iter().any(|(other, other_serial)| {
                        other == &format!("/dev/cu.{suffix}") && other_serial == serial
                    })
                })
        })
        .collect();
    ensure!(
        matches.len() == 1,
        "device selector must match exactly one UART port or USB serial; found {}",
        matches.len()
    );
    Ok(matches[0].clone())
}
pub fn validate(info: &HostInfo) -> Result<()> {
    ensure!(
        info.protocol_version == 1,
        "unsupported host identity protocol; use an explicit app target"
    );
    ensure!(
        !info.device_id.is_empty()
            && info.device_id.len() <= 64
            && info.device_id.len().is_multiple_of(2)
            && info.device_id.bytes().all(|b| b.is_ascii_hexdigit()),
        "invalid stable device identity"
    );
    ensure!(
        info.image_sha256.len() == 64 && info.image_sha256.bytes().all(|b| b.is_ascii_hexdigit()),
        "firmware has no exact image identity; use an explicit app target"
    );
    ensure!(
        !info.target.is_empty() && !info.build_revision.is_empty() && info.metadata_version == 1,
        "incomplete device EDK identity"
    );
    Ok(())
}
/// Check runtime requirements, keeping build provenance advisory.
pub fn check_compatibility(
    host: &HostInfo,
    target: &str,
    metadata_version: u32,
    firmware: &str,
    image: Option<&str>,
) -> Result<bool> {
    ensure!(
        host.target == target,
        "application target differs from connected device"
    );
    ensure!(
        host.metadata_version == metadata_version,
        "unsupported application metadata version"
    );
    let mismatch = host.build_revision != firmware || image != Some(host.image_sha256.as_str());
    if mismatch {
        eprintln!(
            "Warning: this application was built for different or unverified firmware ({firmware}; current {}). It may malfunction; continuing.",
            host.build_revision
        );
    }
    Ok(mismatch)
}
pub fn open(
    root: &Path,
    selector: Option<&str>,
    log: Option<PathBuf>,
) -> Result<(DeviceClient, Binding)> {
    let previous = root.join(".meshbus-device.json");
    let previous: Option<Binding> = if previous.exists() {
        Some(inputs::load(&previous)?)
    } else {
        None
    };
    let available = ports()?;
    let selector = selector
        .or_else(|| {
            previous
                .as_ref()
                .map(|p| p.usb_serial.as_deref().unwrap_or(&p.port))
        })
        .context("select an explicit --device UART port or USB serial")?;
    let (port, usb_serial) = resolve(selector, &available)?;
    let mut client = DeviceClient::open(port.clone(), log)?;
    let host: HostInfo = serde_json::from_value(client.command("llext host")
        .context("device identity query failed; connected firmware must expose llext host: use explicit app target --target/--firmware for offline builds")?)?;
    validate(&host)?;
    if let Some(old) = &previous {
        ensure!(
            old.schema == 1 && old.host.device_id == host.device_id,
            "connected device differs from project binding; explicitly remove .meshbus-device.json to select another device"
        );
    }
    Ok((
        client,
        Binding {
            schema: 1,
            port,
            usb_serial,
            host,
        },
    ))
}
pub fn target(root: &Path, selector: &str, source: &Path, profile: &str) -> Result<()> {
    let (_client, binding) = open(root, Some(selector), None)?;
    let h = &binding.host;
    let selected = inputs::resolve_selection(
        root,
        source,
        &h.target,
        &h.build_revision,
        profile,
        Some(&h.image_sha256),
    )?;
    ensure!(
        selected.release.metadata_version == h.metadata_version,
        "device metadata format differs from release EDK"
    );
    inputs::write_json(&root.join(".meshbus-target.json"), &selected)?;
    inputs::write_json(&root.join(".meshbus-device.json"), &binding)?;
    crate::host::print(&serde_json::json!({"device":binding,"selection":selected}))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn build_differences_warn_but_target_and_format_still_reject() {
        let host = HostInfo {
            protocol_version: 1,
            device_id: "0011".into(),
            target: "board/cpu".into(),
            build_revision: "new".into(),
            metadata_version: 1,
            image_sha256: "a".repeat(64),
        };
        assert!(
            !check_compatibility(&host, "board/cpu", 1, "new", Some(&host.image_sha256)).unwrap()
        );
        assert!(
            check_compatibility(&host, "board/cpu", 1, "old", Some(&host.image_sha256)).unwrap()
        );
        assert!(check_compatibility(&host, "board/cpu", 1, "new", Some(&"b".repeat(64))).unwrap());
        assert!(check_compatibility(&host, "board/cpu", 1, "new", None).unwrap());
        assert!(check_compatibility(&host, "other/cpu", 1, "new", None).is_err());
        assert!(check_compatibility(&host, "board/cpu", 2, "new", None).is_err());
    }
    #[test]
    fn device_selection_never_guesses_an_endpoint() {
        let available = vec![
            ("/dev/one".into(), Some("C2-A".into())),
            ("/dev/two".into(), Some("C2-B".into())),
        ];
        assert_eq!(resolve("C2-B", &available).unwrap().0, "/dev/two");
        for selector in ["", "C2", "/dev/absent"] {
            assert!(resolve(selector, &available).is_err());
        }
        let duplicate = vec![available[0].clone(), available[0].clone()];
        assert!(resolve("C2-A", &duplicate).is_err());
        let aliases = vec![
            ("/dev/tty.usbmodem1".into(), Some("C2-A".into())),
            ("/dev/cu.usbmodem1".into(), Some("C2-A".into())),
        ];
        assert_eq!(resolve("C2-A", &aliases).unwrap().0, "/dev/cu.usbmodem1");
        assert_eq!(
            resolve("/dev/tty.usbmodem1", &aliases).unwrap().0,
            "/dev/tty.usbmodem1"
        );
        let mut distinct = aliases;
        distinct.push(("/dev/cu.usbmodem2".into(), Some("C2-A".into())));
        assert!(resolve("C2-A", &distinct).is_err());
        let mut host = HostInfo {
            protocol_version: 1,
            device_id: "0011".into(),
            target: "board/cpu".into(),
            build_revision: "abc".into(),
            metadata_version: 1,
            image_sha256: "a".repeat(64),
        };
        validate(&host).unwrap();
        host.protocol_version = 0;
        assert!(validate(&host).is_err());
        host.protocol_version = 1;
        host.image_sha256.clear();
        assert!(validate(&host).is_err());
    }
}
