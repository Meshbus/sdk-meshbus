// SPDX-License-Identifier: Apache-2.0
use super::{device, inputs};
use crate::commands::connect::DeviceClient;
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use std::{
    path::Path,
    time::{Duration, Instant},
};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Status {
    pub protocol_version: u32,
    pub session_id: u64,
    pub app_id: String,
    pub path: String,
    pub state: String,
    pub detail: i32,
    pub resources_reclaimed: bool,
}
fn decode(value: serde_json::Value) -> Result<Status> {
    let status: Status = serde_json::from_value(value)?;
    ensure!(status.protocol_version == 1, "unsupported Session protocol");
    Ok(status)
}
pub fn status(client: &mut DeviceClient, id: u64) -> Result<Status> {
    decode(client.command(&format!("desktop mba status {id}"))?)
}
fn wait(
    client: &mut DeviceClient,
    mut current: Status,
    stopping: bool,
    timeout_ms: u64,
) -> Result<Status> {
    let deadline = Instant::now() + Duration::from_millis(timeout_ms);
    loop {
        let done = if stopping {
            current.resources_reclaimed
        } else {
            current.state == "desktop_mba_state_running"
                || current.state == "desktop_mba_state_ended"
        };
        if done || current.state == "desktop_mba_state_failed" {
            return Ok(current);
        }
        ensure!(
            Instant::now() < deadline,
            "Session {} is still {}; resources_reclaimed={}; query status before retrying",
            current.session_id,
            current.state,
            current.resources_reclaimed
        );
        std::thread::sleep(Duration::from_millis(100));
        current = status(client, current.session_id)?;
    }
}
pub fn start(client: &mut DeviceClient, id: &str, path: &str) -> Result<Status> {
    let accepted = decode(client.command(&format!(
        "desktop mba start {} {}",
        shell_words::quote(id),
        shell_words::quote(path)
    ))?)?;
    ensure!(
        accepted.app_id == id && accepted.session_id != 0,
        "start response identity mismatch"
    );
    wait(client, accepted, false, 15_000)
}
pub fn stop(
    client: &mut DeviceClient,
    id: &str,
    session: Option<u64>,
    timeout_ms: u32,
) -> Result<Status> {
    let current = status(client, session.unwrap_or(0))?;
    ensure!(
        current.app_id == id && current.session_id != 0,
        "requested app ID does not match the Session"
    );
    let accepted = decode(client.command(&format!(
        "desktop mba stop {} {timeout_ms}",
        current.session_id
    ))?)?;
    ensure!(
        accepted.session_id == current.session_id && accepted.app_id == id,
        "stop response identity mismatch"
    );
    wait(client, accepted, true, u64::from(timeout_ms) + 2000)
}
pub fn print_result(status: &Status) -> Result<()> {
    crate::host::print(status)?;
    ensure!(
        status.state != "desktop_mba_state_failed",
        "Session failed with detail {}; reclaimed={}",
        status.detail,
        status.resources_reclaimed
    );
    Ok(())
}
pub fn check_host(root: &Path, host: &device::HostInfo) -> Result<()> {
    if root.join("meshbus.lock").exists() {
        let lock = inputs::read_lock(root)?;
        let r = lock.release;
        let hash = r.image_sha256.context("EDK has no exact image identity; select an EDK exported from identity-capable firmware")?;
        ensure!(
            host.image_sha256 == hash
                && host.target == r.target
                && host.build_revision == r.firmware
                && host.metadata_version == r.metadata_version
                && Some(host.interface_abi) == r.interface_abi,
            "connected firmware differs from the project EDK; explicitly retarget before running"
        );
    }
    Ok(())
}
