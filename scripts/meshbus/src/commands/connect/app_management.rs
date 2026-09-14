// SPDX-License-Identifier: Apache-2.0
//! One UART owner reused across application management operations.
use super::*;
use anyhow::{Context, ensure};

pub struct DeviceClient {
    runtime: Runtime,
    args: ConnectArgs,
    serial: SerialSession,
}
impl DeviceClient {
    pub fn open(port: String, log_file: Option<PathBuf>) -> Result<Self, ConnectError> {
        let args = ConnectArgs {
            port,
            baudrate: 115_200,
            timeout: 10.0,
            remote_timeout: 130.0,
            command: None,
            json: true,
            quiet_logs: log_file.is_none(),
            color: ConnectColor::Never,
            log_file,
            no_history: true,
            yes: true,
        };
        let runtime = Runtime::load()?;
        let serial = SerialSession::open(&args)?;
        Ok(Self {
            runtime,
            args,
            serial,
        })
    }
    pub fn follow_logs(&mut self) -> Result<(), ConnectError> {
        self.args.quiet_logs = false;
        self.serial.start_background(&self.args, |line| {
            eprintln!("{line}");
            Ok(())
        })
    }
    pub fn command(&mut self, command: &str) -> Result<JsonValue, ConnectError> {
        execute_one_with_exchange(
            &self.runtime,
            &self.args,
            command,
            Some(&mut self.serial),
            true,
        )
        .map(|(_, value)| value)
    }
}

impl DeviceClient {
    fn fs_exchange(
        &mut self,
        write: bool,
        id: u8,
        values: Vec<(&str, CborValue)>,
    ) -> anyhow::Result<Vec<(CborValue, CborValue)>> {
        let mut command = self
            .runtime
            .registry
            .commands
            .iter()
            .find(|c| c.path == ["fs", "stat"])
            .context("missing FS command descriptor")?
            .clone();
        command.group_id = 8;
        command.command_id = id;
        command.op = if write { "write" } else { "read" }.into();
        let mut body = Vec::new();
        let map = CborValue::Map(
            values
                .into_iter()
                .map(|(k, v)| (CborValue::Text(k.into()), v))
                .collect(),
        );
        ciborium::ser::into_writer(&map, &mut body)?;
        let sequence = self.serial.allocate_sequence();
        self.serial
            .send(&build_smp_cbor_request(&command, &body, sequence)?)?;
        let frame = self.serial.receive(
            self.args.timeout,
            self.args.quiet_logs,
            self.args.log_file.as_ref(),
            self.args.color,
            &command,
            sequence,
        )?;
        Ok(parse_smp_map(&command, &frame)?)
    }
    /// Standard MCUmgr FS transfer, on the same sequenced UART as control requests.
    pub fn upload(&mut self, path: &str, bytes: &[u8]) -> anyhow::Result<()> {
        ensure!(bytes.len() <= 64 * 1024 * 1024, "upload exceeds file limit");
        let mut offset = 0;
        loop {
            let end = (offset + 320).min(bytes.len());
            let mut request = vec![
                ("name", CborValue::Text(path.into())),
                ("off", CborValue::Integer((offset as u64).into())),
                ("data", CborValue::Bytes(bytes[offset..end].to_vec())),
            ];
            if offset == 0 {
                request.push(("len", CborValue::Integer((bytes.len() as u64).into())));
            }
            let reply = self.fs_exchange(true, 0, request)?;
            ensure!(
                cbor_lookup(&reply, "off").and_then(cbor_integer) == Some(end as i128),
                "upload offset mismatch; transfer incomplete for {path}"
            );
            offset = end;
            if offset == bytes.len() {
                return Ok(());
            }
        }
    }
    pub fn download(&mut self, path: &str, maximum: usize) -> anyhow::Result<Vec<u8>> {
        let mut bytes = Vec::new();
        let mut length = None;
        loop {
            let reply = self.fs_exchange(
                false,
                0,
                vec![
                    ("name", CborValue::Text(path.into())),
                    ("off", CborValue::Integer((bytes.len() as u64).into())),
                ],
            )?;
            if bytes.is_empty() {
                length = cbor_lookup(&reply, "len").and_then(cbor_integer);
                ensure!(
                    length.is_some_and(|n| n >= 0 && n <= maximum as i128),
                    "invalid download length"
                );
            }
            ensure!(
                cbor_lookup(&reply, "off").and_then(cbor_integer) == Some(bytes.len() as i128),
                "download offset mismatch"
            );
            let data = cbor_lookup(&reply, "data")
                .and_then(CborValue::as_bytes)
                .context("missing download payload")?;
            ensure!(
                bytes.len() + data.len() <= length.unwrap() as usize,
                "download exceeds declared length"
            );
            bytes.extend(data);
            if bytes.len() == length.unwrap() as usize {
                return Ok(bytes);
            }
            ensure!(!data.is_empty(), "download made no progress");
        }
    }
    pub fn file_hash(&mut self, path: &str) -> anyhow::Result<(u64, String)> {
        let reply = self.fs_exchange(
            false,
            2,
            vec![
                ("name", CborValue::Text(path.into())),
                ("type", CborValue::Text("sha256".into())),
            ],
        )?;
        let output = cbor_lookup(&reply, "output")
            .and_then(CborValue::as_bytes)
            .context("missing device file hash")?;
        let length = cbor_lookup(&reply, "len")
            .and_then(cbor_integer)
            .context("missing device file length")?;
        ensure!(
            output.len() == 32 && length >= 0 && length <= u64::MAX as i128,
            "invalid device file digest/length"
        );
        Ok((length as u64, crate::host::hex(output)))
    }
}
