// SPDX-License-Identifier: Apache-2.0
//! One UART owner reused across application management operations.
use super::*;

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
