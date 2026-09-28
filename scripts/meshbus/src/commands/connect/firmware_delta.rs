// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use clap::{Args, Subcommand};
use prost::Message;
use prost_reflect::{DynamicMessage, Value};
use serde::Deserialize;
use serde_json::{Value as JsonValue, json};
use sha2::{Digest, Sha256};

use super::*;

const GROUP_ENUM: &str = "meshbus.FirmwareMgmtGroupId";
const GROUP_VALUE: &str = "FIRMWARE_MGMT_GROUP_ID_MESHBUS_FIRMWARE";
const COMMAND_ENUM: &str = "meshbus.FirmwareMgmtCommandId";
const STATE_ENUM: &str = "meshbus.FirmwareState";
const RESULT_ENUM: &str = "meshbus.FirmwareResult";
const UPDATE_KIND_ENUM: &str = "meshbus.FirmwareUpdateKind";
const CHUNK_SIZE: usize = 448;
const PATCH_MAX: usize = 24 * 1024;
const NEWP_HEADER_SIZE: usize = 88;
const DETOOLS_SEQUENTIAL_CRLE: u8 = 0x02;
const ZEPHYR_ESTALE: i128 = -133;

#[derive(Debug, Args)]
pub struct FirmwareDeltaArgs {
    #[command(subcommand)]
    command: FirmwareDeltaCommand,
}

#[derive(Debug, Subcommand)]
enum FirmwareDeltaCommand {
    /// Verify package hashes and print its transfer identity.
    Inspect {
        package: PathBuf,
        #[arg(long)]
        json: bool,
    },
    /// Resume and stream one package to targets sequentially.
    Send {
        package: PathBuf,
        #[arg(short = 'p', long)]
        port: String,
        /// Target Contact public-key prefix; repeat for a serialized campaign.
        #[arg(long, required = true)]
        target: Vec<String>,
        #[arg(long, default_value_t = 115_200)]
        baudrate: u32,
        #[arg(long, default_value_t = 2.5)]
        timeout: f64,
        #[arg(long, default_value_t = 130.0)]
        remote_timeout: f64,
        /// Stop after the patch is verified on target.
        #[arg(long)]
        no_apply: bool,
        /// Apply but do not request activation/reboot.
        #[arg(long)]
        no_activate: bool,
        /// Maximum post-activation confirmation wait.
        #[arg(long, default_value_t = 240)]
        confirm_timeout: u64,
        /// Persist non-secret per-target progress as JSON.
        #[arg(long)]
        state: Option<PathBuf>,
        #[arg(short = 'y', long)]
        yes: bool,
        #[arg(long)]
        json: bool,
    },
    /// Query target-owned durable transfer status.
    Status {
        #[arg(short = 'p', long)]
        port: String,
        #[arg(long)]
        target: String,
        #[arg(long, default_value_t = 115_200)]
        baudrate: u32,
        #[arg(long, default_value_t = 2.5)]
        timeout: f64,
        #[arg(long, default_value_t = 130.0)]
        remote_timeout: f64,
        #[arg(long)]
        transfer_id: Option<String>,
        #[arg(long)]
        json: bool,
    },
    /// Abort exactly one target transfer without erasing settings or slots.
    Abort {
        #[arg(short = 'p', long)]
        port: String,
        #[arg(long)]
        target: String,
        #[arg(long)]
        transfer_id: String,
        #[arg(long, default_value_t = 115_200)]
        baudrate: u32,
        #[arg(long, default_value_t = 2.5)]
        timeout: f64,
        #[arg(long, default_value_t = 130.0)]
        remote_timeout: f64,
        #[arg(short = 'y', long)]
        yes: bool,
        #[arg(long)]
        json: bool,
    },
}

#[derive(Debug, Deserialize)]
struct InventoryFile {
    size: u64,
    sha256: String,
}

#[derive(Debug, Deserialize)]
struct Inventory {
    schema_version: u32,
    transfer_id: String,
    board_id: String,
    role: String,
    chunk_size: usize,
    compression: String,
    files: BTreeMap<String, InventoryFile>,
}

struct Package {
    root: PathBuf,
    inventory: Inventory,
    transfer_id: Vec<u8>,
    manifest: Vec<u8>,
    signature: Vec<u8>,
    patch: Vec<u8>,
}

#[derive(Clone, Debug)]
struct StatusValue {
    result: i128,
    state: i128,
    update_kind: i128,
    transfer_id: Vec<u8>,
    durable_received: usize,
    next_offset: usize,
    patch_size: usize,
    chunk_size: usize,
    retryable: bool,
    safe_to_receive: bool,
    safe_to_apply: bool,
    shutdown_pending: bool,
    detail: i128,
}

impl StatusValue {
    fn json(&self) -> JsonValue {
        json!({
            "result": self.result,
            "state": self.state,
            "update_kind": self.update_kind,
            "transfer_id": hex_bytes(&self.transfer_id),
            "durable_received": self.durable_received,
            "next_offset": self.next_offset,
            "patch_size": self.patch_size,
            "chunk_size": self.chunk_size,
            "retryable": self.retryable,
            "safe_to_receive": self.safe_to_receive,
            "safe_to_apply": self.safe_to_apply,
            "shutdown_pending": self.shutdown_pending,
            "detail": self.detail,
        })
    }

    fn require_ok(&self, operation: &str, result_ok: i128) -> Result<(), ConnectError> {
        if self.result == result_ok {
            Ok(())
        } else {
            Err(ConnectError::Protocol(format!(
                "FIRMWARE {operation} rejected: result={}, state={}, detail={}",
                self.result, self.state, self.detail
            )))
        }
    }
}

struct Transport {
    runtime: Runtime,
    args: ConnectArgs,
    serial: SerialSession,
    prefix: prost::bytes::Bytes,
    group: u16,
}

impl Transport {
    fn command(&self, value: &str) -> Result<u8, ConnectError> {
        firmware_command_id(&self.runtime, value)
    }

    fn state(&self, value: &str) -> Result<i128, ConnectError> {
        firmware_enum_value(&self.runtime, STATE_ENUM, value)
    }

    fn result(&self, value: &str) -> Result<i128, ConnectError> {
        firmware_enum_value(&self.runtime, RESULT_ENUM, value)
    }

    fn update_kind(&self, value: &str) -> Result<i128, ConnectError> {
        firmware_enum_value(&self.runtime, UPDATE_KIND_ENUM, value)
    }

    fn open(
        port: String,
        prefix: &str,
        baudrate: u32,
        timeout: f64,
        remote_timeout: f64,
    ) -> Result<Self, ConnectError> {
        let args = ConnectArgs {
            port,
            baudrate,
            timeout,
            remote_timeout,
            command: None,
            json: true,
            quiet_logs: true,
            color: ConnectColor::Never,
            log_file: None,
            no_history: true,
            yes: true,
        };
        let prefix = parse_hex_usage(prefix)?;
        if prefix.len() != 4 {
            return Err(ConnectError::Usage(
                "FIRMWARE target must be an exact 4-byte Contact public-key prefix".into(),
            ));
        }
        let runtime = Runtime::load()?;
        let group = firmware_group_id(&runtime)?;
        let serial = SerialSession::open(&args)?;
        Ok(Self {
            runtime,
            args,
            serial,
            prefix,
            group,
        })
    }

    fn exchange(
        &mut self,
        command: u8,
        operation: &str,
        payload: Vec<u8>,
    ) -> Result<StatusValue, ConnectError> {
        let outer = self.runtime.by_path(&["management", "remote", "smp"])?;
        let payload = encode_smp_data_payload(&payload)?;
        let frame = remote_exchange_wait(
            &self.runtime,
            &self.args,
            &mut self.serial,
            outer,
            RemoteExchange {
                prefix: self.prefix.clone(),
                group: self.group,
                command,
                operation,
                payload,
                secret: None,
            },
        )?;
        if frame.len() < 8
            || u16::from_be_bytes([frame[4], frame[5]]) != self.group
            || frame[7] != command
        {
            return Err(ConnectError::Protocol(
                "FIRMWARE SMP response identity mismatch".into(),
            ));
        }
        let data = firmware_smp_payload(&frame, self.group, command)?;
        let descriptor = self
            .runtime
            .pool
            .get_message_by_name("meshbus.FirmwareResponse")
            .ok_or_else(|| ConnectError::Schema("FirmwareResponse descriptor is missing".into()))?;
        let message = DynamicMessage::decode(descriptor, data.as_slice()).map_err(|error| {
            ConnectError::Protocol(format!(
                "invalid FIRMWARE response protobuf: {error}; payload={}",
                hex_bytes(&data)
            ))
        })?;
        status_from_message(&message)
    }

    fn status(&mut self, transfer_id: Option<&[u8]>) -> Result<StatusValue, ConnectError> {
        let mut request = self.runtime.new_message("FirmwareStatusRequest")?;
        if let Some(id) = transfer_id {
            set_named_field(
                &mut request,
                "transfer_id",
                Value::Bytes(id.to_vec().into()),
            )?;
        }
        let payload = request.encode_to_vec();
        let command = self.command("FIRMWARE_MGMT_COMMAND_ID_STATUS")?;
        retry_stale_status(|| self.exchange(command, "read", payload.clone()))
    }

    fn begin(&mut self, package: &Package) -> Result<StatusValue, ConnectError> {
        let mut request = self.runtime.new_message("FirmwareDeltaBeginRequest")?;
        set_named_field(
            &mut request,
            "manifest",
            Value::Bytes(package.manifest.clone().into()),
        )?;
        set_named_field(
            &mut request,
            "signature",
            Value::Bytes(package.signature.clone().into()),
        )?;
        let command = self.command("FIRMWARE_MGMT_COMMAND_ID_DELTA_BEGIN")?;
        self.exchange(command, "write", request.encode_to_vec())
    }

    fn write(
        &mut self,
        id: &[u8],
        offset: usize,
        data: &[u8],
    ) -> Result<StatusValue, ConnectError> {
        let mut request = self.runtime.new_message("FirmwareDeltaWriteRequest")?;
        set_named_field(
            &mut request,
            "transfer_id",
            Value::Bytes(id.to_vec().into()),
        )?;
        set_scalar_text(&mut request, "offset", &offset.to_string())?;
        set_named_field(&mut request, "data", Value::Bytes(data.to_vec().into()))?;
        let command = self.command("FIRMWARE_MGMT_COMMAND_ID_DELTA_WRITE")?;
        self.exchange(command, "write", request.encode_to_vec())
    }

    fn lifecycle(&mut self, command: u8, id: &[u8]) -> Result<StatusValue, ConnectError> {
        let mut request = self.runtime.new_message("FirmwareTransferRequest")?;
        set_named_field(
            &mut request,
            "transfer_id",
            Value::Bytes(id.to_vec().into()),
        )?;
        self.exchange(command, "write", request.encode_to_vec())
    }

    fn lifecycle_reconciled(
        &mut self,
        command: u8,
        id: &[u8],
        result_ok: i128,
        update_kind: i128,
        accepted_states: &[i128],
    ) -> Result<StatusValue, ConnectError> {
        let first_error = match self.lifecycle(command, id) {
            Ok(status) => return Ok(status),
            Err(error) if remote_result_is_ambiguous(&error) => error,
            Err(error) => return Err(error),
        };

        if let Ok(observed) = self.status(Some(id))
            && reconciled_status_matches(&observed, id, result_ok, update_kind, accepted_states)
        {
            return Ok(observed);
        }

        self.lifecycle(command, id).map_err(|_| first_error)
    }
}

fn retry_stale_status<T, F>(mut attempt: F) -> Result<T, ConnectError>
where
    F: FnMut() -> Result<T, ConnectError>,
{
    match attempt() {
        Err(ConnectError::RemoteFailure {
            status: ZEPHYR_ESTALE,
        }) => attempt(),
        result => result,
    }
}

fn firmware_group_id(runtime: &Runtime) -> Result<u16, ConnectError> {
    let value = firmware_enum_value(runtime, GROUP_ENUM, GROUP_VALUE)?;
    u16::try_from(value).map_err(|_| {
        ConnectError::Schema(format!(
            "protobuf enum value is not a valid MCUmgr group: {GROUP_ENUM}.{GROUP_VALUE}={value}"
        ))
    })
}

fn firmware_command_id(runtime: &Runtime, value_name: &str) -> Result<u8, ConnectError> {
    let value = firmware_enum_value(runtime, COMMAND_ENUM, value_name)?;
    u8::try_from(value).map_err(|_| {
        ConnectError::Schema(format!(
            "protobuf enum value is not a valid command: {COMMAND_ENUM}.{value_name}={value}"
        ))
    })
}

fn firmware_enum_value(
    runtime: &Runtime,
    enum_name: &str,
    value_name: &str,
) -> Result<i128, ConnectError> {
    let enumeration = runtime
        .pool
        .get_enum_by_name(enum_name)
        .ok_or_else(|| ConnectError::Schema(format!("protobuf enum not found: {enum_name}")))?;
    let value = enumeration.get_value_by_name(value_name).ok_or_else(|| {
        ConnectError::Schema(format!(
            "protobuf enum value not found: {enum_name}.{value_name}"
        ))
    })?;
    Ok(i128::from(value.number()))
}

fn firmware_smp_payload(frame: &[u8], group: u16, command: u8) -> Result<Vec<u8>, ConnectError> {
    if frame.len() < 8 || u16::from_be_bytes([frame[4], frame[5]]) != group || frame[7] != command {
        return Err(ConnectError::Protocol(
            "FIRMWARE SMP response identity mismatch".into(),
        ));
    }
    let payload_length = u16::from_be_bytes([frame[2], frame[3]]) as usize;
    if frame.len() != 8 + payload_length {
        return Err(ConnectError::Protocol(
            "FIRMWARE SMP response length is invalid".into(),
        ));
    }
    let value: CborValue = ciborium::de::from_reader(&frame[8..]).map_err(|error| {
        ConnectError::Protocol(format!("invalid FIRMWARE SMP CBOR response: {error}"))
    })?;
    let map = value
        .as_map()
        .ok_or_else(|| ConnectError::Protocol("FIRMWARE SMP response is not a CBOR map".into()))?;
    if let Some(data) = cbor_lookup(map, "data").and_then(CborValue::as_bytes) {
        return Ok(data.to_vec());
    }
    if let Some(rc) = cbor_lookup(map, "rc").and_then(cbor_integer) {
        return Err(ConnectError::Protocol(format!(
            "FIRMWARE MCUmgr error: group={group}, rc={rc}"
        )));
    }
    if let Some(error) = cbor_lookup(map, "err").and_then(CborValue::as_map) {
        let group = cbor_lookup(error, "group")
            .and_then(cbor_integer)
            .unwrap_or(group as i128);
        let rc = cbor_lookup(error, "rc")
            .and_then(cbor_integer)
            .unwrap_or(-1);
        return Err(ConnectError::Protocol(format!(
            "FIRMWARE MCUmgr error: group={group}, rc={rc}"
        )));
    }
    Err(ConnectError::Protocol(
        "FIRMWARE SMP response has no data or error".into(),
    ))
}

fn status_from_message(message: &DynamicMessage) -> Result<StatusValue, ConnectError> {
    Ok(StatusValue {
        result: integer_field(message, "result")?,
        state: integer_field(message, "state")?,
        update_kind: integer_field(message, "update_kind")?,
        transfer_id: bytes_field(message, "transfer_id")?,
        durable_received: usize::try_from(integer_field(message, "durable_received")?)
            .map_err(|_| ConnectError::Protocol("invalid durable offset".into()))?,
        next_offset: usize::try_from(integer_field(message, "next_offset")?)
            .map_err(|_| ConnectError::Protocol("invalid next offset".into()))?,
        patch_size: usize::try_from(integer_field(message, "patch_size")?)
            .map_err(|_| ConnectError::Protocol("invalid patch size".into()))?,
        chunk_size: usize::try_from(integer_field(message, "chunk_size")?)
            .map_err(|_| ConnectError::Protocol("invalid chunk size".into()))?,
        retryable: bool_field(message, "retryable")?,
        safe_to_receive: bool_field(message, "safe_to_receive")?,
        safe_to_apply: bool_field(message, "safe_to_apply")?,
        shutdown_pending: bool_field(message, "shutdown_pending")?,
        detail: integer_field(message, "detail")?,
    })
}

fn reconciled_status_matches(
    status: &StatusValue,
    transfer_id: &[u8],
    result_ok: i128,
    update_kind: i128,
    accepted_states: &[i128],
) -> bool {
    status.transfer_id == transfer_id
        && status.result == result_ok
        && status.update_kind == update_kind
        && accepted_states.contains(&status.state)
}

impl Package {
    fn load(root: &Path) -> Result<Self, ConnectError> {
        let inventory_path = root.join("inventory.json");
        let inventory: Inventory =
            serde_json::from_slice(&fs::read(&inventory_path).map_err(|error| {
                ConnectError::Usage(format!("cannot read {}: {error}", inventory_path.display()))
            })?)
            .map_err(|error| ConnectError::Usage(format!("invalid package inventory: {error}")))?;
        let expected = [
            "manifest.cbor",
            "manifest.sig",
            "patch.newp",
            "source.signed.bin",
            "target.signed.bin",
        ];
        if inventory.schema_version != 1
            || inventory.chunk_size != CHUNK_SIZE
            || inventory.files.len() != expected.len()
            || expected
                .iter()
                .any(|name| !inventory.files.contains_key(*name))
        {
            return Err(ConnectError::Usage(
                "unsupported FIRMWARE package inventory".into(),
            ));
        }
        for (name, declared) in &inventory.files {
            if name.contains('/') || name.contains('\\') || name.starts_with('.') {
                return Err(ConnectError::Usage("unsafe FIRMWARE inventory path".into()));
            }
            let data = fs::read(root.join(name)).map_err(|error| {
                ConnectError::Usage(format!("cannot read package file {name}: {error}"))
            })?;
            let digest = Sha256::digest(&data);
            if data.len() as u64 != declared.size || hex_bytes(&digest) != declared.sha256 {
                return Err(ConnectError::Usage(format!(
                    "package inventory mismatch: {name}"
                )));
            }
        }
        let manifest = fs::read(root.join("manifest.cbor"))
            .map_err(|error| ConnectError::Usage(error.to_string()))?;
        let signature = fs::read(root.join("manifest.sig"))
            .map_err(|error| ConnectError::Usage(error.to_string()))?;
        let patch = fs::read(root.join("patch.newp"))
            .map_err(|error| ConnectError::Usage(error.to_string()))?;
        let transfer_id = Sha256::digest(&manifest).to_vec();
        if manifest.len() > 384
            || signature.len() != 64
            || !supported_patch_contract(&patch)
            || inventory.compression != "crle"
            || hex_bytes(&transfer_id) != inventory.transfer_id
        {
            return Err(ConnectError::Usage(
                "FIRMWARE package contract validation failed".into(),
            ));
        }
        Ok(Self {
            root: root.to_owned(),
            inventory,
            transfer_id,
            manifest,
            signature,
            patch,
        })
    }

    fn json(&self) -> JsonValue {
        json!({
            "package": self.root,
            "transfer_id": hex_bytes(&self.transfer_id),
            "role": self.inventory.role,
            "board_id": self.inventory.board_id,
            "patch_size": self.patch.len(),
            "chunks": self.patch.len().div_ceil(CHUNK_SIZE),
            "verified_hashes": true,
            "signature_verification": "use meshbus firmware package verify with the trusted public keys",
        })
    }
}

fn supported_patch_contract(patch: &[u8]) -> bool {
    (NEWP_HEADER_SIZE + 1..=PATCH_MAX).contains(&patch.len())
        && patch.get(..4) == Some(b"NEWP")
        && patch[NEWP_HEADER_SIZE] == DETOOLS_SEQUENTIAL_CRLE
}

fn common_args(
    port: String,
    baudrate: u32,
    timeout: f64,
    remote_timeout: f64,
) -> (String, u32, f64, f64) {
    (port, baudrate, timeout, remote_timeout)
}

fn save_state(path: Option<&PathBuf>, value: &JsonValue) -> Result<(), ConnectError> {
    if let Some(path) = path {
        let encoded = serde_json::to_vec_pretty(value)
            .map_err(|error| ConnectError::Protocol(error.to_string()))?;
        fs::write(path, encoded).map_err(|error| {
            ConnectError::Transport(format!(
                "cannot write campaign state {}: {error}",
                path.display()
            ))
        })?;
    }
    Ok(())
}

fn send_one(
    package: &Package,
    transport: &mut Transport,
    target: &str,
    no_apply: bool,
    no_activate: bool,
    confirm_timeout: u64,
    state_path: Option<&PathBuf>,
) -> Result<JsonValue, ConnectError> {
    let result_ok = transport.result("FIRMWARE_RESULT_OK")?;
    let update_kind_delta = transport.update_kind("FIRMWARE_UPDATE_KIND_DELTA")?;
    let state_idle = transport.state("FIRMWARE_STATE_IDLE")?;
    let state_receiving = transport.state("FIRMWARE_STATE_RECEIVING")?;
    let state_staged = transport.state("FIRMWARE_STATE_STAGED")?;
    let state_verified = transport.state("FIRMWARE_STATE_VERIFIED")?;
    let state_applying = transport.state("FIRMWARE_STATE_APPLYING")?;
    let state_pending_reboot = transport.state("FIRMWARE_STATE_PENDING_REBOOT")?;
    let state_testing = transport.state("FIRMWARE_STATE_TESTING")?;
    let state_confirmed = transport.state("FIRMWARE_STATE_CONFIRMED")?;
    let state_rolled_back = transport.state("FIRMWARE_STATE_ROLLED_BACK")?;
    let state_failed = transport.state("FIRMWARE_STATE_FAILED")?;
    let state_aborted = transport.state("FIRMWARE_STATE_ABORTED")?;
    let finish = transport.command("FIRMWARE_MGMT_COMMAND_ID_DELTA_FINISH")?;
    let apply = transport.command("FIRMWARE_MGMT_COMMAND_ID_DELTA_APPLY")?;
    let activate = transport.command("FIRMWARE_MGMT_COMMAND_ID_DELTA_ACTIVATE")?;
    let mut status = transport.status(None)?;
    if status.transfer_id == package.transfer_id && status.state == state_confirmed {
        status.require_ok("confirmed status", result_ok)?;
        if status.update_kind != update_kind_delta {
            return Err(ConnectError::Protocol(
                "target confirmed a different firmware update kind".into(),
            ));
        }
        return Ok(json!({"target": target, "terminal": "confirmed", "status": status.json()}));
    }
    if status.transfer_id == package.transfer_id && status.state == state_rolled_back {
        return Err(ConnectError::Protocol(format!(
            "target {target} rolled back candidate"
        )));
    }
    if matches!(
        status.state,
        value if value == state_receiving
            || value == state_staged
            || value == state_verified
            || value == state_applying
            || value == state_pending_reboot
            || value == state_testing
    ) && status.transfer_id != package.transfer_id
    {
        return Err(ConnectError::Protocol(format!(
            "target {target} has a conflicting durable transfer {}",
            hex_bytes(&status.transfer_id)
        )));
    }
    if status.state == state_idle
        || status.transfer_id != package.transfer_id
        || status.state == state_failed
        || status.state == state_aborted
    {
        status = transport.begin(package)?;
        status.require_ok("begin", result_ok)?;
    }
    if status.update_kind != update_kind_delta
        || status.transfer_id != package.transfer_id
        || status.chunk_size != CHUNK_SIZE
    {
        return Err(ConnectError::Protocol(
            "target accepted inconsistent FIRMWARE geometry".into(),
        ));
    }
    if status.state == state_receiving {
        if status.shutdown_pending || !status.safe_to_receive {
            return Err(ConnectError::Protocol(
                "target power policy paused FIRMWARE reception".into(),
            ));
        }
        let mut offset = status.next_offset;
        if offset > package.patch.len() {
            return Err(ConnectError::Protocol(
                "target FIRMWARE offset exceeds package".into(),
            ));
        }
        while offset < package.patch.len() {
            let end = (offset + CHUNK_SIZE).min(package.patch.len());
            status =
                match transport.write(&package.transfer_id, offset, &package.patch[offset..end]) {
                    Ok(value) => value,
                    Err(first_error) => {
                        let observed = transport.status(Some(&package.transfer_id))?;
                        if observed.next_offset >= end {
                            observed
                        } else {
                            transport
                                .write(&package.transfer_id, offset, &package.patch[offset..end])
                                .map_err(|_| first_error)?
                        }
                    }
                };
            status.require_ok("write", result_ok)?;
            if status.next_offset < end {
                return Err(ConnectError::Protocol(
                    "FIRMWARE write made no progress".into(),
                ));
            }
            offset = status.next_offset;
            save_state(
                state_path,
                &json!({"target": target, "status": status.json()}),
            )?;
        }
    }
    if status.state == state_receiving
        || status.state == state_staged
        || status.state == state_verified
    {
        status = transport.lifecycle_reconciled(
            finish,
            &package.transfer_id,
            result_ok,
            update_kind_delta,
            &[
                state_verified,
                state_applying,
                state_pending_reboot,
                state_testing,
                state_confirmed,
            ],
        )?;
        status.require_ok("finish", result_ok)?;
    }
    if no_apply {
        let terminal = if status.state == state_verified {
            "verified"
        } else {
            "already_beyond_verified"
        };
        return Ok(json!({"target": target, "terminal": terminal, "status": status.json()}));
    }
    if status.state == state_verified {
        if status.shutdown_pending || !status.safe_to_apply {
            return Err(ConnectError::Protocol(
                "target power policy blocked FIRMWARE apply".into(),
            ));
        }
        status = transport.lifecycle_reconciled(
            apply,
            &package.transfer_id,
            result_ok,
            update_kind_delta,
            &[
                state_applying,
                state_pending_reboot,
                state_testing,
                state_confirmed,
            ],
        )?;
        status.require_ok("apply", result_ok)?;
    }
    let apply_deadline = Instant::now() + Duration::from_secs(120);
    while status.state == state_applying && Instant::now() < apply_deadline {
        std::thread::sleep(Duration::from_millis(500));
        status = transport.status(Some(&package.transfer_id))?;
        status.require_ok("apply status", result_ok)?;
    }
    if status.state != state_pending_reboot && status.state != state_testing {
        return Err(ConnectError::RemoteTimeout {
            message: "FIRMWARE apply did not reach pending reboot".into(),
            status: None,
        });
    }
    if no_activate {
        return Ok(
            json!({"target": target, "terminal": "pending_reboot", "status": status.json()}),
        );
    }
    if status.state == state_pending_reboot {
        match transport.lifecycle(activate, &package.transfer_id) {
            Ok(value) => {
                value.require_ok("activate", result_ok)?;
                status = value;
            }
            Err(error) if remote_result_is_ambiguous(&error) => {
                // The target may have persisted reboot intent before the final
                // response crossed the radio. Reconcile through durable status.
            }
            Err(error) => return Err(error),
        }
    }
    let deadline = Instant::now() + Duration::from_secs(confirm_timeout);
    while Instant::now() < deadline {
        std::thread::sleep(Duration::from_secs(2));
        match transport.status(Some(&package.transfer_id)) {
            Ok(value) if value.transfer_id != package.transfer_id => {
                return Err(ConnectError::Protocol(format!(
                    "target {target} reported a different firmware transfer"
                )));
            }
            Ok(value) if value.result != result_ok => {
                return Err(ConnectError::Protocol(format!(
                    "target {target} firmware status failed: result={}, detail={}",
                    value.result, value.detail
                )));
            }
            Ok(value) if value.update_kind != update_kind_delta => {
                return Err(ConnectError::Protocol(format!(
                    "target {target} switched to a different update kind"
                )));
            }
            Ok(value) if value.state == state_confirmed => {
                return Ok(
                    json!({"target": target, "terminal": "confirmed", "status": value.json()}),
                );
            }
            Ok(value) if value.state == state_rolled_back => {
                return Err(ConnectError::Protocol(format!(
                    "target {target} rolled back candidate"
                )));
            }
            Ok(value) => status = value,
            Err(_) => continue,
        }
    }
    Err(ConnectError::RemoteTimeout {
        message: format!(
            "target {target} did not report confirmed boot within {confirm_timeout} s"
        ),
        status: Some(status.state),
    })
}

fn remote_result_is_ambiguous(error: &ConnectError) -> bool {
    matches!(
        error,
        ConnectError::RemoteFailure {
            status: ZEPHYR_ESTALE
        } | ConnectError::RemoteTimeout { .. }
    )
}

pub fn run_firmware_delta(args: FirmwareDeltaArgs) -> Result<(), ConnectError> {
    match args.command {
        FirmwareDeltaCommand::Inspect {
            package,
            json: as_json,
        } => {
            let package = Package::load(&package)?;
            if as_json {
                println!("{}", package.json());
            } else {
                println!("transfer_id: {}", hex_bytes(&package.transfer_id));
                println!(
                    "target: {}/{}",
                    package.inventory.board_id, package.inventory.role
                );
                println!("patch_size: {}", package.patch.len());
                println!("chunks: {}", package.patch.len().div_ceil(CHUNK_SIZE));
            }
        }
        FirmwareDeltaCommand::Status {
            port,
            target,
            baudrate,
            timeout,
            remote_timeout,
            transfer_id,
            json: as_json,
        } => {
            let (port, baudrate, timeout, remote_timeout) =
                common_args(port, baudrate, timeout, remote_timeout);
            let mut transport = Transport::open(port, &target, baudrate, timeout, remote_timeout)?;
            let id = transfer_id.as_deref().map(parse_hex_usage).transpose()?;
            if id.as_ref().is_some_and(|value| value.len() != 32) {
                return Err(ConnectError::Usage("transfer ID must be 32 bytes".into()));
            }
            let status = transport.status(id.as_deref())?;
            if as_json {
                println!("{}", status.json());
            } else {
                console::write_human("firmware status", &status.json(), ConnectColor::Never);
            }
        }
        FirmwareDeltaCommand::Abort {
            port,
            target,
            transfer_id,
            baudrate,
            timeout,
            remote_timeout,
            yes,
            json: as_json,
        } => {
            if !yes {
                return Err(ConnectError::Usage("firmware abort requires --yes".into()));
            }
            let id = parse_hex_usage(&transfer_id)?;
            if id.len() != 32 {
                return Err(ConnectError::Usage("transfer ID must be 32 bytes".into()));
            }
            let mut transport = Transport::open(port, &target, baudrate, timeout, remote_timeout)?;
            let abort = transport.command("FIRMWARE_MGMT_COMMAND_ID_DELTA_ABORT")?;
            let result_ok = transport.result("FIRMWARE_RESULT_OK")?;
            let status = transport.lifecycle(abort, &id)?;
            status.require_ok("abort", result_ok)?;
            if as_json {
                println!("{}", status.json());
            } else {
                console::write_human("firmware abort", &status.json(), ConnectColor::Never);
            }
        }
        FirmwareDeltaCommand::Send {
            package,
            port,
            target,
            baudrate,
            timeout,
            remote_timeout,
            no_apply,
            no_activate,
            confirm_timeout,
            state,
            yes,
            json: as_json,
        } => {
            if !no_apply && !yes {
                return Err(ConnectError::Usage(
                    "FIRMWARE apply/activation requires --yes".into(),
                ));
            }
            let package = Package::load(&package)?;
            let mut results = Vec::new();
            let mut failed = false;
            for prefix in target {
                let mut transport =
                    match Transport::open(port.clone(), &prefix, baudrate, timeout, remote_timeout)
                    {
                        Ok(transport) => transport,
                        Err(error) => {
                            failed = true;
                            results.push(json!({
                                "target": prefix,
                                "terminal": "failed",
                                "error": error.safe_message(),
                            }));
                            continue;
                        }
                    };
                match send_one(
                    &package,
                    &mut transport,
                    &prefix,
                    no_apply,
                    no_activate,
                    confirm_timeout,
                    state.as_ref(),
                ) {
                    Ok(value) => results.push(value),
                    Err(error) => {
                        failed = true;
                        results.push(json!({"target": prefix, "terminal": "failed", "error": error.safe_message()}));
                        save_state(
                            state.as_ref(),
                            &json!({"package": package.inventory.transfer_id, "results": results}),
                        )?;
                    }
                }
            }
            let report = json!({"transfer_id": package.inventory.transfer_id, "results": results});
            save_state(state.as_ref(), &report)?;
            if as_json {
                println!("{report}");
            } else {
                console::write_human("firmware send", &report, ConnectColor::Never);
            }
            if failed {
                return Err(ConnectError::Reported(4));
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    fn smp_response(command: u8, value: CborValue) -> Vec<u8> {
        let group = firmware_group_id(&Runtime::load().unwrap()).unwrap();
        let mut payload = Vec::new();
        ciborium::ser::into_writer(&value, &mut payload).unwrap();
        let mut frame = vec![0x0b, 0];
        frame.extend_from_slice(&(payload.len() as u16).to_be_bytes());
        frame.extend_from_slice(&group.to_be_bytes());
        frame.push(7);
        frame.push(command);
        frame.extend_from_slice(&payload);
        frame
    }

    #[test]
    fn firmware_group_id_is_resolved_from_the_embedded_schema() {
        let runtime = Runtime::load().unwrap();
        assert_ne!(firmware_group_id(&runtime).unwrap(), 0);
    }

    #[test]
    fn package_rejects_path_substitution() {
        let temporary = tempdir().unwrap();
        fs::write(
            temporary.path().join("inventory.json"),
            br#"{"schema_version":1,"transfer_id":"00","board_id":"x","role":"repeater","chunk_size":448,"compression":"crle","files":{"../escape":{"size":0,"sha256":""}}}"#,
        ).unwrap();
        assert!(Package::load(temporary.path()).is_err());
    }

    #[test]
    fn package_contract_accepts_only_sequential_crle() {
        let mut patch = vec![0; NEWP_HEADER_SIZE + 1];
        patch[..4].copy_from_slice(b"NEWP");
        patch[NEWP_HEADER_SIZE] = DETOOLS_SEQUENTIAL_CRLE;
        assert!(supported_patch_contract(&patch));

        patch[NEWP_HEADER_SIZE] = 0x04;
        assert!(!supported_patch_contract(&patch));
    }

    #[test]
    fn firmware_response_extracts_protobuf_from_cbor_envelope() {
        let runtime = Runtime::load().unwrap();
        let status = firmware_command_id(&runtime, "FIRMWARE_MGMT_COMMAND_ID_STATUS").unwrap();
        let frame = smp_response(
            status,
            CborValue::Map(vec![(
                CborValue::Text("data".into()),
                CborValue::Bytes(vec![1, 2, 3]),
            )]),
        );
        let group = firmware_group_id(&runtime).unwrap();
        assert_eq!(
            firmware_smp_payload(&frame, group, status).unwrap(),
            [1, 2, 3]
        );
    }

    #[test]
    fn firmware_request_wraps_nonempty_protobuf_in_cbor_data_envelope() {
        let protobuf = vec![0x0a, 0x03, 0x01, 0x02, 0x03];
        let payload = encode_smp_data_payload(&protobuf).unwrap();
        let value: CborValue = ciborium::de::from_reader(payload.as_slice()).unwrap();
        let map = value.as_map().unwrap();

        assert_eq!(
            cbor_lookup(map, "data").and_then(CborValue::as_bytes),
            Some(&protobuf)
        );
    }

    #[test]
    fn firmware_response_rejects_mismatched_length_and_mcumgr_error() {
        let runtime = Runtime::load().unwrap();
        let write = firmware_command_id(&runtime, "FIRMWARE_MGMT_COMMAND_ID_DELTA_WRITE").unwrap();
        let mut malformed = smp_response(
            write,
            CborValue::Map(vec![(
                CborValue::Text("data".into()),
                CborValue::Bytes(vec![1]),
            )]),
        );
        malformed[3] = malformed[3].wrapping_add(1);
        let group = firmware_group_id(&runtime).unwrap();
        assert!(firmware_smp_payload(&malformed, group, write).is_err());

        let error = smp_response(
            write,
            CborValue::Map(vec![(
                CborValue::Text("rc".into()),
                CborValue::Integer(5.into()),
            )]),
        );
        assert!(
            firmware_smp_payload(&error, group, write)
                .unwrap_err()
                .safe_message()
                .contains("rc=5")
        );
    }

    #[test]
    fn status_reauthenticates_once_after_stale_session() {
        let mut attempts = 0;
        let value = retry_stale_status(|| {
            attempts += 1;
            if attempts == 1 {
                Err(ConnectError::RemoteFailure {
                    status: ZEPHYR_ESTALE,
                })
            } else {
                Ok(42)
            }
        })
        .unwrap();

        assert_eq!(value, 42);
        assert_eq!(attempts, 2);
    }

    #[test]
    fn status_returns_the_second_stale_failure_without_looping() {
        let mut attempts = 0;
        let error = retry_stale_status::<(), _>(|| {
            attempts += 1;
            Err(ConnectError::RemoteFailure {
                status: ZEPHYR_ESTALE,
            })
        })
        .unwrap_err();

        assert!(matches!(
            error,
            ConnectError::RemoteFailure {
                status: ZEPHYR_ESTALE
            }
        ));
        assert_eq!(attempts, 2);
    }

    #[test]
    fn status_does_not_retry_other_remote_failures() {
        let mut attempts = 0;
        let error = retry_stale_status::<(), _>(|| {
            attempts += 1;
            Err(ConnectError::RemoteFailure { status: -22 })
        })
        .unwrap_err();

        assert!(matches!(error, ConnectError::RemoteFailure { status: -22 }));
        assert_eq!(attempts, 1);
    }

    #[test]
    fn status_does_not_retry_remote_timeouts() {
        let mut attempts = 0;
        let error = retry_stale_status::<(), _>(|| {
            attempts += 1;
            Err(ConnectError::RemoteTimeout {
                message: "timed out".into(),
                status: Some(-110),
            })
        })
        .unwrap_err();

        assert!(matches!(
            error,
            ConnectError::RemoteTimeout {
                status: Some(-110),
                ..
            }
        ));
        assert_eq!(attempts, 1);
    }

    #[test]
    fn lifecycle_reconciliation_requires_exact_durable_identity() {
        let transfer_id = vec![0x5a; 32];
        let mut status = StatusValue {
            result: 0,
            state: 3,
            update_kind: 1,
            transfer_id: transfer_id.clone(),
            durable_received: 0,
            next_offset: 0,
            patch_size: 0,
            chunk_size: CHUNK_SIZE,
            retryable: false,
            safe_to_receive: true,
            safe_to_apply: true,
            shutdown_pending: false,
            detail: 0,
        };

        assert!(reconciled_status_matches(
            &status,
            &transfer_id,
            0,
            1,
            &[3, 4]
        ));
        status.transfer_id[0] ^= 1;
        assert!(!reconciled_status_matches(
            &status,
            &transfer_id,
            0,
            1,
            &[3, 4]
        ));
        status.transfer_id[0] ^= 1;
        status.result = 7;
        assert!(!reconciled_status_matches(
            &status,
            &transfer_id,
            0,
            1,
            &[3, 4]
        ));
        status.result = 0;
        status.update_kind = 2;
        assert!(!reconciled_status_matches(
            &status,
            &transfer_id,
            0,
            1,
            &[3, 4]
        ));
    }
}
