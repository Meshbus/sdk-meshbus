// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

use std::borrow::Cow;
use std::fs::OpenOptions;
use std::io::{self, Read, Write};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, mpsc};
use std::thread;
use std::time::{Duration, Instant};

use base64::Engine;
use base64::engine::general_purpose::STANDARD as BASE64;
use ciborium::Value as CborValue;
use clap::{Args, ValueEnum};
use crc::{CRC_16_XMODEM, Crc};
use prost::Message;
use prost_reflect::{
    Cardinality, DescriptorPool, DynamicMessage, FieldDescriptor, Kind, ReflectMessage, Value,
};
use serde::Deserialize;
use serde_json::{Map as JsonMap, Value as JsonValue, json};
use serialport::{ClearBuffer, SerialPort};
use thiserror::Error;

mod configuration;
mod console;
mod firmware_delta;

pub use firmware_delta::{FirmwareDeltaArgs, run_firmware_delta};

const DESCRIPTOR: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/meshbus.pb"));
const COMMANDS: &str = include_str!("commands.json");
const START: [u8; 2] = [6, 9];
const CONTINUE: [u8; 2] = [4, 20];
const LINE_LENGTH: usize = 128;
const MAX_FRAME_SIZE: usize = 612;
const REMOTE_REQUEST_ONE_SHOT_MAX: usize = 504;
const REMOTE_REQUEST_CHUNK_SIZE: usize = 448;
const SERIAL_BITS_PER_BYTE: u64 = 10;
const SERIAL_LINE_GUARD_US: u64 = 2_000;
const CRC16: Crc<u16> = Crc::<u16>::new(&CRC_16_XMODEM);

#[derive(Debug, Args)]
pub struct ConnectArgs {
    /// Serial port used by UART MCUmgr.
    #[arg(short = 'p', long)]
    port: String,
    /// Serial baudrate.
    #[arg(long, default_value_t = 115_200, value_parser = clap::value_parser!(u32).range(1..))]
    baudrate: u32,
    /// Serial connect and MCUmgr request timeout in seconds.
    #[arg(long, default_value_t = 2.5, value_parser = positive_float)]
    timeout: f64,
    /// LoRa remote-management result timeout in seconds.
    #[arg(long, default_value_t = 130.0, value_parser = positive_float)]
    remote_timeout: f64,
    /// Run one command non-interactively.
    #[arg(short = 'c', long)]
    command: Option<String>,
    /// Emit one machine-readable JSON result; requires --command.
    #[arg(long, requires = "command")]
    json: bool,
    /// Do not display device logs on stderr.
    #[arg(long)]
    quiet_logs: bool,
    /// Terminal color mode.
    #[arg(long, value_enum, default_value_t = ConnectColor::Auto)]
    color: ConnectColor,
    /// Append raw device log bytes to this file.
    #[arg(long)]
    log_file: Option<PathBuf>,
    /// Disable persistent interactive command history.
    #[arg(long)]
    no_history: bool,
    /// Confirm a dangerous non-interactive command.
    #[arg(short = 'y', long)]
    yes: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, ValueEnum)]
enum ConnectColor {
    Auto,
    Always,
    Never,
}

fn positive_float(value: &str) -> Result<f64, String> {
    let parsed = value.parse::<f64>().map_err(|error| error.to_string())?;
    if parsed > 0.0 {
        Ok(parsed)
    } else {
        Err("must be greater than zero".into())
    }
}

#[derive(Debug, Error)]
pub enum ConnectError {
    #[error("{0}")]
    Usage(String),
    #[error("{0}")]
    Transport(String),
    #[error("{0}")]
    Protocol(String),
    #[error("{0}")]
    Schema(String),
    #[error("{message}")]
    Mcumgr {
        code: &'static str,
        message: String,
        group: i128,
        rc: i128,
    },
    #[error(
        "remote password changed, but the local Contact update failed; use management remote smp_with_password for recovery"
    )]
    RemotePasswordContactUpdate {
        contact_prefix: String,
        local_error: String,
    },
    #[error("remote management failed with status {status}")]
    RemoteFailure { status: i128 },
    #[error("{message}")]
    RemoteTimeout {
        message: String,
        status: Option<i128>,
    },
    #[error("")]
    Reported(i32),
}

impl ConnectError {
    pub fn exit_status(&self) -> i32 {
        match self {
            Self::Usage(_) => 2,
            Self::Mcumgr {
                code: "unsupported",
                ..
            } => 3,
            Self::Reported(status) => *status,
            _ => 4,
        }
    }

    pub fn is_reported(&self) -> bool {
        matches!(self, Self::Reported(_))
    }

    pub fn safe_message(&self) -> String {
        self.to_string()
            .chars()
            .map(|character| match character {
                '\n' => "\\n".into(),
                '\r' => "\\r".into(),
                '\t' => "\\t".into(),
                character if character.is_control() => {
                    format!("\\x{:02x}", character as u32)
                }
                character => character.to_string(),
            })
            .collect()
    }

    fn code(&self) -> &'static str {
        match self {
            Self::Usage(_) => "invalid_command",
            Self::Transport(_) => "transport_error",
            Self::Protocol(_) => "protocol_error",
            Self::Schema(_) => "schema_error",
            Self::Mcumgr { code, .. } => code,
            Self::RemotePasswordContactUpdate { .. } => "remote_password_updated_contact_failed",
            Self::RemoteFailure { .. } => "remote_failed",
            Self::RemoteTimeout { .. } => "remote_timeout",
            Self::Reported(_) => "reported",
        }
    }
}

#[derive(Clone, Debug, Deserialize)]
struct Registry {
    schema_version: u32,
    commands: Vec<CommandSpec>,
}

#[derive(Clone, Debug, Deserialize)]
struct CommandSpec {
    path: Vec<String>,
    group_id: u16,
    command_id: u8,
    op: String,
    request: String,
    response: String,
    help: String,
    usage: String,
    completion_note: Option<String>,
    dangerous: bool,
    sensitive: bool,
    internal: bool,
    custom: Option<String>,
    defaults: JsonMap<String, JsonValue>,
    arguments: Vec<CommandArgument>,
    completion_choices: Vec<String>,
    config_option_aliases: JsonMap<String, JsonValue>,
    config_sensitive_paths: Vec<String>,
    config_set_path: Option<Vec<String>>,
    config_text_paths: Vec<String>,
}

#[derive(Clone, Debug, Deserialize)]
struct CommandArgument {
    path: Vec<String>,
    optional: bool,
    #[allow(dead_code)]
    choices: Vec<String>,
}

struct Runtime {
    pool: DescriptorPool,
    registry: Registry,
}

impl Runtime {
    fn load() -> Result<Self, ConnectError> {
        let pool = DescriptorPool::decode(DESCRIPTOR).map_err(|error| {
            ConnectError::Schema(format!("invalid embedded Meshbus descriptor: {error}"))
        })?;
        let registry: Registry = serde_json::from_str(COMMANDS).map_err(|error| {
            ConnectError::Schema(format!("invalid embedded command catalog: {error}"))
        })?;
        if registry.schema_version != 1 {
            return Err(ConnectError::Schema(format!(
                "unsupported command catalog version: {}",
                registry.schema_version
            )));
        }
        Ok(Self { pool, registry })
    }

    fn resolve<'a, 'b>(
        &'a self,
        words: &'b [String],
    ) -> Result<(&'a CommandSpec, &'b [String]), ConnectError> {
        let normalized = words
            .iter()
            .map(|word| word.to_ascii_lowercase())
            .collect::<Vec<_>>();
        let normalized = if normalized.first().is_some_and(|word| word == "meshbus") {
            &normalized[1..]
        } else {
            &normalized[..]
        };
        let command = self
            .registry
            .commands
            .iter()
            .filter(|command| !command.internal && normalized.starts_with(&command.path))
            .max_by_key(|command| command.path.len())
            .ok_or_else(|| {
                ConnectError::Usage(format!("unknown command: {}; use 'help'", words.join(" ")))
            })?;
        let consumed = command.path.len()
            + usize::from(
                words
                    .first()
                    .is_some_and(|word| word.eq_ignore_ascii_case("meshbus")),
            );
        Ok((command, &words[consumed..]))
    }

    fn help_lines(&self, prefix: &[String]) -> Vec<String> {
        let prefix = prefix
            .iter()
            .map(|word| word.to_ascii_lowercase())
            .collect::<Vec<_>>();
        self.registry
            .commands
            .iter()
            .filter(|command| !command.internal && command.path.starts_with(&prefix))
            .map(|command| format!("{:<42} {}", command.usage, command.help))
            .collect()
    }

    fn by_path(&self, path: &[&str]) -> Result<&CommandSpec, ConnectError> {
        self.registry
            .commands
            .iter()
            .find(|command| {
                command.path.len() == path.len()
                    && command
                        .path
                        .iter()
                        .zip(path)
                        .all(|(actual, expected)| actual == expected)
            })
            .ok_or_else(|| ConnectError::Schema(format!("command not found: {}", path.join(" "))))
    }

    fn by_string_path(&self, path: &[String]) -> Result<&CommandSpec, ConnectError> {
        self.registry
            .commands
            .iter()
            .find(|command| command.path == path)
            .ok_or_else(|| ConnectError::Schema(format!("command not found: {}", path.join(" "))))
    }

    fn new_message(&self, name: &str) -> Result<DynamicMessage, ConnectError> {
        self.pool
            .get_message_by_name(&format!("meshbus.{name}"))
            .map(DynamicMessage::new)
            .ok_or_else(|| ConnectError::Schema(format!("protobuf message not found: {name}")))
    }

    fn request(&self, command: &CommandSpec, words: &[String]) -> Result<Vec<u8>, ConnectError> {
        if command.custom.is_some() {
            return Err(ConnectError::Usage(format!(
                "custom request must be handled explicitly: {}",
                command.path.join(" ")
            )));
        }
        let descriptor = self
            .pool
            .get_message_by_name(&format!("meshbus.{}", command.request))
            .ok_or_else(|| {
                ConnectError::Schema(format!("protobuf message not found: {}", command.request))
            })?;
        let mut message = DynamicMessage::new(descriptor);
        let required = command
            .arguments
            .iter()
            .filter(|argument| {
                !argument.optional && !command.defaults.contains_key(&argument.path.join("."))
            })
            .count();
        let maximum = command
            .arguments
            .iter()
            .filter(|argument| !command.defaults.contains_key(&argument.path.join(".")))
            .count();
        if !(required..=maximum).contains(&words.len()) {
            return Err(ConnectError::Usage(format!(
                "expected {required}..{maximum} arguments, got {}; usage: {}",
                words.len(),
                command.usage
            )));
        }

        let mut word_index = 0;
        for argument in &command.arguments {
            let key = argument.path.join(".");
            let value = if let Some(default) = command.defaults.get(&key) {
                json_to_protobuf(&message, &argument.path, default)?
            } else if let Some(word) = words.get(word_index) {
                word_index += 1;
                parse_argument(&message, &argument.path, word)?
            } else {
                continue;
            };
            assign_path(&mut message, &argument.path, value)?;
        }
        Ok(message.encode_to_vec())
    }

    fn decode_response(
        &self,
        command: &CommandSpec,
        payload: &[u8],
    ) -> Result<DynamicMessage, ConnectError> {
        let descriptor = self
            .pool
            .get_message_by_name(&format!("meshbus.{}", command.response))
            .ok_or_else(|| {
                ConnectError::Schema(format!("protobuf message not found: {}", command.response))
            })?;
        DynamicMessage::decode(descriptor, payload).map_err(|error| {
            ConnectError::Protocol(format!("invalid {} response: {error}", command.response))
        })
    }
}

pub fn run_connect(args: ConnectArgs) -> Result<(), ConnectError> {
    let runtime = Runtime::load()?;
    if let Some(command_line) = args.command.as_deref() {
        let result = execute_one(&runtime, &args, command_line);
        match result {
            Ok((label, value)) => {
                if args.json {
                    console::write_json_success(&label, &value);
                } else {
                    console::write_human(&label, &value, args.color);
                }
                Ok(())
            }
            Err(error) if args.json => {
                let status = error.exit_status();
                console::write_json_error(
                    &console::command_output_label(&runtime, command_line),
                    &error,
                );
                Err(ConnectError::Reported(status))
            }
            Err(error) => Err(error),
        }
    } else {
        console::run_interactive(&runtime, &args)
    }
}

fn execute_one(
    runtime: &Runtime,
    args: &ConnectArgs,
    command_line: &str,
) -> Result<(String, JsonValue), ConnectError> {
    execute_one_with_exchange(runtime, args, command_line, None, args.yes)
}

fn execute_one_with_exchange(
    runtime: &Runtime,
    args: &ConnectArgs,
    command_line: &str,
    exchange: Option<&mut dyn CommandExchange>,
    allow_dangerous: bool,
) -> Result<(String, JsonValue), ConnectError> {
    let words = shell_words::split(command_line)
        .map_err(|error| ConnectError::Usage(format!("invalid command: {error}")))?;
    if words.is_empty() {
        return Err(ConnectError::Usage("empty command".into()));
    }
    if matches!(words[0].to_ascii_lowercase().as_str(), "help" | "?") {
        let lines = runtime.help_lines(&words[1..]);
        if lines.is_empty() {
            return Err(ConnectError::Usage(format!(
                "no commands below: {}",
                words[1..].join(" ")
            )));
        }
        return Ok((
            "help".into(),
            json!({"prefix": &words[1..], "lines": lines}),
        ));
    }
    if matches!(words[0].to_ascii_lowercase().as_str(), "exit" | "quit") {
        return Ok(("exit".into(), json!({"closed": true})));
    }
    let (command, arguments) = match runtime.resolve(&words) {
        Ok(resolved) => resolved,
        Err(error) => {
            let prefix = normalized_command_words(&words);
            let lines = runtime.help_lines(prefix);
            if lines.is_empty() {
                return Err(error);
            }
            return Ok(("help".into(), json!({"prefix": prefix, "lines": lines})));
        }
    };
    if requires_confirmation(command, arguments) && !allow_dangerous {
        return Err(ConnectError::Usage(format!(
            "{} requires explicit confirmation",
            command.path.join(" ")
        )));
    }
    let mut owned_serial = None;
    let serial: &mut dyn CommandExchange = match exchange {
        Some(exchange) => exchange,
        None => owned_serial.insert(SerialSession::open(args)?),
    };
    let mut value = execute_command(runtime, args, serial, command, arguments)?;
    if value.get("accepted") == Some(&JsonValue::Bool(true))
        && let Some(note) = &command.completion_note
        && let Some(object) = value.as_object_mut()
    {
        object.insert("completion".into(), JsonValue::String(note.clone()));
    }
    Ok((command.path.join(" "), value))
}

fn normalized_command_words(words: &[String]) -> &[String] {
    if words
        .first()
        .is_some_and(|word| word.eq_ignore_ascii_case("meshbus"))
    {
        &words[1..]
    } else {
        words
    }
}

fn requires_confirmation(command: &CommandSpec, arguments: &[String]) -> bool {
    command.dangerous
        || (command.custom.as_deref() == Some("config")
            && arguments.len() == 1
            && arguments[0].eq_ignore_ascii_case("--reset"))
}

fn confirmation_command(
    runtime: &Runtime,
    command_line: &str,
) -> Result<Option<String>, ConnectError> {
    let words = shell_words::split(command_line)
        .map_err(|error| ConnectError::Usage(format!("invalid command: {error}")))?;
    if words.is_empty()
        || matches!(
            words[0].to_ascii_lowercase().as_str(),
            "help" | "?" | "exit" | "quit"
        )
    {
        return Ok(None);
    }
    let (command, arguments) = match runtime.resolve(&words) {
        Ok(resolved) => resolved,
        Err(_) => return Ok(None),
    };
    Ok(requires_confirmation(command, arguments).then(|| command.path.join(" ")))
}

fn execute_command(
    runtime: &Runtime,
    args: &ConnectArgs,
    serial: &mut dyn CommandExchange,
    command: &CommandSpec,
    words: &[String],
) -> Result<JsonValue, ConnectError> {
    match command.custom.as_deref() {
        Some("config") => {
            configuration::Configuration::load(runtime, command)?.execute(args, serial, words)
        }
        Some("management_local_password") => {
            require_word_count(command, words, 1, 1)?;
            let password = parse_management_password(&words[0])?;
            let mut request = runtime.new_message(&command.request)?;
            set_named_field(&mut request, "secret", Value::Bytes(password.into()))?;
            invoke_json(runtime, args, serial, command, &request.encode_to_vec())
        }
        Some("message_send_node") => {
            require_word_count(command, words, 3, 4)?;
            let mut request = runtime.new_message(&command.request)?;
            set_named_field(
                &mut request,
                "key_prefix",
                Value::Bytes(parse_hex_usage(&words[0])?),
            )?;
            set_scalar_text(&mut request, "attempt", &words[1])?;
            set_named_field(&mut request, "message", Value::String(words[2].clone()))?;
            if let Some(flood) = words.get(3) {
                set_scalar_text(&mut request, "flood", flood)?;
            }
            invoke_json(runtime, args, serial, command, &request.encode_to_vec())
        }
        Some("radio_send") => {
            require_word_count(command, words, 1, 1)?;
            let payload = if let Some(text) = words[0].strip_prefix("text:") {
                text.as_bytes().to_vec()
            } else {
                parse_hex(&words[0])
                    .map(|value| value.to_vec())
                    .unwrap_or_else(|_| words[0].as_bytes().to_vec())
            };
            if payload.is_empty() {
                return Err(ConnectError::Usage(
                    "radio payload must not be empty".into(),
                ));
            }
            let mut request = runtime.new_message(&command.request)?;
            set_named_field(&mut request, "payload", Value::Bytes(payload.into()))?;
            invoke_json(runtime, args, serial, command, &request.encode_to_vec())
        }
        Some("fs_format") => {
            require_word_count(command, words, 1, 1)?;
            let mut request = runtime.new_message(&command.request)?;
            set_named_field(&mut request, "volume_id", Value::String(words[0].clone()))?;
            set_named_field(&mut request, "confirm", Value::String("FORMAT".into()))?;
            invoke_json(runtime, args, serial, command, &request.encode_to_vec())
        }
        Some("input_inject_act" | "input_inject_raw") => {
            execute_input_inject(runtime, args, serial, command, words)
        }
        Some("contact_set") => execute_contact_set(runtime, args, serial, command, words),
        Some("management_remote" | "management_remote_raw" | "management_remote_raw_password") => {
            execute_management_remote(runtime, args, serial, command, words)
        }
        Some(custom) => Err(ConnectError::Schema(format!(
            "unsupported custom handler {custom} for {}",
            command.path.join(" ")
        ))),
        None => {
            let protobuf = runtime.request(command, words)?;
            invoke_json(runtime, args, serial, command, &protobuf)
        }
    }
}

// Exchanges return an MCUmgr-validated protobuf payload. Message decoding stays
// in invoke so serial and in-memory adapters share the command response contract.
trait CommandExchange {
    fn exchange(
        &mut self,
        args: &ConnectArgs,
        command: &CommandSpec,
        protobuf: &[u8],
    ) -> Result<Vec<u8>, ConnectError>;
}

impl CommandExchange for SerialSession {
    fn exchange(
        &mut self,
        args: &ConnectArgs,
        command: &CommandSpec,
        protobuf: &[u8],
    ) -> Result<Vec<u8>, ConnectError> {
        let sequence = self.allocate_sequence();
        let frame = build_smp_request(command, protobuf, sequence)?;
        self.send(&frame)?;
        let response = self.receive(
            args.timeout,
            args.quiet_logs,
            args.log_file.as_ref(),
            args.color,
            command,
            sequence,
        )?;
        parse_smp_response(command, &response)
    }
}

fn invoke(
    runtime: &Runtime,
    args: &ConnectArgs,
    serial: &mut dyn CommandExchange,
    command: &CommandSpec,
    protobuf: &[u8],
) -> Result<DynamicMessage, ConnectError> {
    let data = serial.exchange(args, command, protobuf)?;
    runtime.decode_response(command, &data)
}

fn invoke_json(
    runtime: &Runtime,
    args: &ConnectArgs,
    serial: &mut dyn CommandExchange,
    command: &CommandSpec,
    protobuf: &[u8],
) -> Result<JsonValue, ConnectError> {
    invoke(runtime, args, serial, command, protobuf).map(|message| dynamic_to_json(&message))
}

fn execute_input_inject(
    runtime: &Runtime,
    args: &ConnectArgs,
    serial: &mut dyn CommandExchange,
    command: &CommandSpec,
    words: &[String],
) -> Result<JsonValue, ConnectError> {
    require_word_count(command, words, 3, 3)?;
    let mut request = runtime.new_message(&command.request)?;
    let input_type = match words[0].to_ascii_lowercase().as_str() {
        "key" => "1",
        "rel" => "2",
        "abs" => "3",
        "msc" | "type" => "4",
        _ => &words[0],
    };
    set_scalar_text(&mut request, "type", input_type)?;
    set_scalar_text(&mut request, "code", &words[1])?;
    if command.custom.as_deref() == Some("input_inject_act") {
        let action = match words[2].to_ascii_lowercase().as_str() {
            "short" => "0",
            "long" => "1",
            "cw" | "scroll_cw" => "2",
            "ccw" | "scroll_ccw" => "3",
            _ => &words[2],
        };
        set_scalar_text(&mut request, "action", action)?;
    } else {
        set_scalar_text(&mut request, "value", &words[2])?;
    }
    invoke_json(runtime, args, serial, command, &request.encode_to_vec())
}

fn execute_contact_set(
    runtime: &Runtime,
    args: &ConnectArgs,
    serial: &mut dyn CommandExchange,
    command: &CommandSpec,
    words: &[String],
) -> Result<JsonValue, ConnectError> {
    require_word_count(command, words, 3, 5)?;
    let prefix = parse_hex_usage(&words[0])?;
    let find = runtime.by_path(&["contact", "find_prefix"])?;
    let mut find_request = runtime.new_message(&find.request)?;
    set_named_field(
        &mut find_request,
        "public_key_prefix",
        Value::Bytes(prefix.clone()),
    )?;
    let find_response = invoke(runtime, args, serial, find, &find_request.encode_to_vec())?;
    let contact_field = find_response
        .descriptor()
        .get_field_by_name("contact")
        .ok_or_else(|| ConnectError::Schema("contact response has no contact field".into()))?;
    let mut contact = match find_response.get_field(&contact_field).as_ref() {
        Value::Message(value) => value.clone(),
        _ => return Err(ConnectError::Protocol("contact was not returned".into())),
    };
    let field = words[1].to_ascii_lowercase();
    let mut clear_secret = false;
    match (field.as_str(), words.len()) {
        ("alias", 3) => set_named_field(&mut contact, "alias", Value::String(words[2].clone()))?,
        ("out_path", 5) => {
            set_scalar_text(&mut contact, "path_hash_size", &words[2])?;
            set_scalar_text(&mut contact, "is_neighbor", &words[3])?;
            let out_path = if words[4] == "-" {
                Vec::new().into()
            } else {
                parse_hex_usage(&words[4])?
            };
            let hash_size = integer_field(&contact, "path_hash_size")? as usize;
            if !out_path.is_empty() && hash_size == 0 {
                return Err(ConnectError::Usage(
                    "non-empty out_path requires a non-zero hash size".into(),
                ));
            }
            if hash_size > 0 && !out_path.len().is_multiple_of(hash_size) {
                return Err(ConnectError::Usage(
                    "out_path length must be a multiple of hash size".into(),
                ));
            }
            set_named_field(&mut contact, "out_path", Value::Bytes(out_path))?;
        }
        ("management_password", 3) => {
            clear_secret = words[2] == "--clear";
            let secret = if clear_secret {
                Vec::new()
            } else {
                parse_management_password(&words[2])?
            };
            set_named_field(
                &mut contact,
                "management_secret",
                Value::Bytes(secret.into()),
            )?;
        }
        ("position", 4) => {
            let latitude = parse_integer(&words[2])
                .map_err(|_| ConnectError::Usage("latitude expects an integer".into()))?;
            let longitude = parse_integer(&words[3])
                .map_err(|_| ConnectError::Usage("longitude expects an integer".into()))?;
            if !(-90_000_000..=90_000_000).contains(&latitude) {
                return Err(ConnectError::Usage(
                    "latitude is outside signed microdegree range".into(),
                ));
            }
            if !(-180_000_000..=180_000_000).contains(&longitude) {
                return Err(ConnectError::Usage(
                    "longitude is outside signed microdegree range".into(),
                ));
            }
            set_scalar_text(&mut contact, "latitude", &words[2])?;
            set_scalar_text(&mut contact, "longitude", &words[3])?;
        }
        _ => return Err(ConnectError::Usage(format!("usage: {}", command.usage))),
    }
    let mut request = runtime.new_message(&command.request)?;
    set_named_field(&mut request, "public_key_prefix", Value::Bytes(prefix))?;
    set_named_field(&mut request, "contact", Value::Message(contact))?;
    set_named_field(
        &mut request,
        "clear_management_secret",
        Value::Bool(clear_secret),
    )?;
    invoke_json(runtime, args, serial, command, &request.encode_to_vec())
}

fn execute_management_remote(
    runtime: &Runtime,
    args: &ConnectArgs,
    serial: &mut dyn CommandExchange,
    command: &CommandSpec,
    words: &[String],
) -> Result<JsonValue, ConnectError> {
    if matches!(
        command.custom.as_deref(),
        Some("management_remote_raw" | "management_remote_raw_password")
    ) {
        let with_password = command.custom.as_deref() == Some("management_remote_raw_password");
        let minimum = if with_password { 5 } else { 4 };
        require_word_count(command, words, minimum, minimum + 1)?;
        let prefix = parse_hex_usage(&words[0])?;
        let mut offset = 1;
        let secret = if with_password {
            let password = parse_management_password(&words[offset])?;
            offset += 1;
            Some(password)
        } else {
            None
        };
        let group = parse_u16_usage(&words[offset], "group")?;
        let command_id = parse_u8_usage(&words[offset + 1], "command")?;
        let operation = match words[offset + 2].to_ascii_lowercase().as_str() {
            "read" | "r" | "0" => "read",
            "write" | "w" | "1" | "2" => "write",
            _ => {
                return Err(ConnectError::Usage(
                    "operation expects read or write".into(),
                ));
            }
        };
        let payload = words
            .get(offset + 3)
            .map(|value| parse_hex_usage(value).map(|bytes| bytes.to_vec()))
            .transpose()?
            .unwrap_or_default();
        let frame = remote_exchange_wait(
            runtime,
            args,
            serial,
            command,
            RemoteExchange {
                prefix,
                group,
                command: command_id,
                operation,
                payload,
                secret,
            },
        )?;
        if frame.len() < 8 {
            return Err(ConnectError::Protocol(
                "remote SMP response header is truncated".into(),
            ));
        }
        return Ok(json!({
            "op": frame[0] & 0x07,
            "flags": frame[1],
            "payload_len": u16::from_be_bytes([frame[2], frame[3]]),
            "group": u16::from_be_bytes([frame[4], frame[5]]),
            "sequence": frame[6],
            "command": frame[7],
            "payload": hex_bytes(&frame[8..]),
        }));
    }

    let (target_path, password_rotation): (&[&str], bool) = match command.path.as_slice() {
        [management, remote, radio, status]
            if management == "management"
                && remote == "remote"
                && radio == "radio"
                && status == "status" =>
        {
            (&["radio", "status"], false)
        }
        [management, remote, radio, config, get]
            if management == "management"
                && remote == "remote"
                && radio == "radio"
                && config == "config"
                && get == "get" =>
        {
            (&["radio", "config"], false)
        }
        [management, remote, power, status]
            if management == "management"
                && remote == "remote"
                && power == "power"
                && status == "status" =>
        {
            (&["power", "status"], false)
        }
        [management, remote, power, config, get]
            if management == "management"
                && remote == "remote"
                && power == "power"
                && config == "config"
                && get == "get" =>
        {
            (&["power", "config"], false)
        }
        [management, remote, password, set]
            if management == "management"
                && remote == "remote"
                && password == "password"
                && set == "set" =>
        {
            (&["management", "config", "set", "password"], true)
        }
        _ => {
            return Err(ConnectError::Schema(format!(
                "unknown remote management wrapper: {}",
                command.path.join(" ")
            )));
        }
    };
    require_word_count(
        command,
        words,
        if password_rotation { 2 } else { 1 },
        if password_rotation { 2 } else { 1 },
    )?;
    let prefix = parse_hex_usage(&words[0])?;
    let target = runtime.by_path(target_path)?;
    let mut target_request = runtime.new_message(&target.request)?;
    let rotated_password = if password_rotation {
        let password = parse_management_password(&words[1])?;
        set_named_field(
            &mut target_request,
            "secret",
            Value::Bytes(password.clone().into()),
        )?;
        Some(password)
    } else {
        None
    };
    let inner_frame = build_smp_request(target, &target_request.encode_to_vec(), 0)?;
    let response = remote_exchange_wait(
        runtime,
        args,
        serial,
        command,
        RemoteExchange {
            prefix: prefix.clone(),
            group: target.group_id,
            command: target.command_id,
            operation: &target.op,
            payload: inner_frame[8..].to_vec(),
            secret: None,
        },
    )?;
    let data = parse_smp_response(target, &response)?;
    let decoded = runtime.decode_response(target, &data)?;
    if let Some(password) = rotated_password {
        if !bool_field(&decoded, "accepted")? {
            return Err(ConnectError::Protocol(
                "remote endpoint did not accept the management password update".into(),
            ));
        }
        if let Err(error) = store_contact_password(runtime, args, serial, &prefix, &password) {
            return Err(remote_password_contact_update_error(&prefix, &error));
        }
        let mut result = dynamic_to_json(&decoded);
        result
            .as_object_mut()
            .expect("protobuf JSON is an object")
            .insert("contact_updated".into(), JsonValue::Bool(true));
        Ok(result)
    } else {
        Ok(dynamic_to_json(&decoded))
    }
}

struct RemoteExchange<'a> {
    prefix: prost::bytes::Bytes,
    group: u16,
    command: u8,
    operation: &'a str,
    payload: Vec<u8>,
    secret: Option<Vec<u8>>,
}

fn remote_request_chunk_end(total_length: usize, offset: usize) -> usize {
    if total_length > REMOTE_REQUEST_ONE_SHOT_MAX {
        (offset + REMOTE_REQUEST_CHUNK_SIZE).min(total_length)
    } else {
        total_length
    }
}

fn remote_exchange_wait(
    runtime: &Runtime,
    args: &ConnectArgs,
    serial: &mut dyn CommandExchange,
    exchange_command: &CommandSpec,
    exchange: RemoteExchange<'_>,
) -> Result<Vec<u8>, ConnectError> {
    let payload = exchange.payload;
    let total_length = payload.len();
    let chunked = total_length > REMOTE_REQUEST_ONE_SHOT_MAX;
    let mut offset = 0usize;
    let mut transfer_id = 0i128;
    let accepted = loop {
        let end = remote_request_chunk_end(total_length, offset);
        let mut request = runtime.new_message(&exchange_command.request)?;
        if offset == 0 {
            set_named_field(
                &mut request,
                "contact_prefix",
                Value::Bytes(exchange.prefix.clone()),
            )?;
            set_scalar_text(&mut request, "group", &exchange.group.to_string())?;
            set_scalar_text(&mut request, "command", &exchange.command.to_string())?;
            set_scalar_text(
                &mut request,
                "op",
                if exchange.operation == "read" {
                    "0"
                } else {
                    "1"
                },
            )?;
            if let Some(secret) = exchange.secret.as_ref() {
                set_named_field(&mut request, "secret", Value::Bytes(secret.clone().into()))?;
            }
        }
        set_named_field(
            &mut request,
            "payload",
            Value::Bytes(payload[offset..end].to_vec().into()),
        )?;
        if chunked {
            set_scalar_text(&mut request, "total_length", &total_length.to_string())?;
            set_scalar_text(&mut request, "offset", &offset.to_string())?;
            if transfer_id != 0 {
                set_scalar_text(&mut request, "transfer_id", &transfer_id.to_string())?;
            }
        }

        let response = invoke(
            runtime,
            args,
            serial,
            exchange_command,
            &request.encode_to_vec(),
        )?;
        if !chunked || end == total_length {
            break response;
        }
        if bool_field(&response, "accepted")? {
            return Err(ConnectError::Protocol(
                "firmware accepted an incomplete remote management upload".into(),
            ));
        }
        transfer_id = integer_field(&response, "transfer_id")?;
        let next_offset = usize::try_from(integer_field(&response, "next_offset")?)
            .map_err(|_| ConnectError::Protocol("invalid remote upload offset".into()))?;
        if transfer_id == 0 || next_offset != end {
            return Err(ConnectError::Protocol(
                "firmware returned inconsistent remote upload progress".into(),
            ));
        }
        offset = next_offset;
    };
    if !bool_field(&accepted, "accepted")? {
        return Err(ConnectError::Protocol(
            "firmware did not accept the remote management exchange".into(),
        ));
    }
    let tag = integer_field(&accepted, "tag")?;
    if tag == 0 {
        return Err(ConnectError::Protocol(
            "firmware returned an invalid remote management tag".into(),
        ));
    }

    let result_command = runtime.by_path(&["management", "remote", "result"])?;
    let deadline = Instant::now() + Duration::from_secs_f64(args.remote_timeout);
    let mut response = Vec::new();
    while Instant::now() < deadline {
        let mut result_request = runtime.new_message(&result_command.request)?;
        set_scalar_text(&mut result_request, "tag", &tag.to_string())?;
        set_named_field(&mut result_request, "consume", Value::Bool(true))?;
        set_scalar_text(&mut result_request, "offset", &response.len().to_string())?;
        let result = invoke(
            runtime,
            args,
            serial,
            result_command,
            &result_request.encode_to_vec(),
        )?;
        if bool_field(&result, "ready")? {
            let status = integer_field(&result, "status")?;
            if let Some(error) = remote_result_error(status) {
                return Err(error);
            }
            let total = usize::try_from(integer_field(&result, "total_length")?)
                .map_err(|_| ConnectError::Protocol("invalid remote response length".into()))?;
            let chunk = bytes_field(&result, "response")?;
            if total == 0 {
                return Err(ConnectError::Protocol(
                    "remote management completed without an SMP response".into(),
                ));
            }
            if response.len() + chunk.len() > total {
                return Err(ConnectError::Protocol(
                    "remote management result exceeded its declared length".into(),
                ));
            }
            if chunk.is_empty() {
                return Err(ConnectError::Protocol(
                    "remote management result made no reassembly progress".into(),
                ));
            }
            response.extend_from_slice(&chunk);
            if response.len() == total {
                return Ok(response);
            }
        }
        std::thread::sleep(Duration::from_millis(250));
    }
    Err(ConnectError::RemoteTimeout {
        message: format!(
            "remote management timed out after {} s",
            args.remote_timeout
        ),
        status: None,
    })
}

fn remote_password_contact_update_error(prefix: &[u8], local_error: &ConnectError) -> ConnectError {
    ConnectError::RemotePasswordContactUpdate {
        contact_prefix: hex_bytes(prefix),
        local_error: local_error.code().into(),
    }
}

fn remote_result_error(status: i128) -> Option<ConnectError> {
    const ZEPHYR_ETIMEDOUT: i128 = -110;

    match status {
        0 => None,
        ZEPHYR_ETIMEDOUT => Some(ConnectError::RemoteTimeout {
            message: "remote management exchange timed out".into(),
            status: Some(status),
        }),
        _ => Some(ConnectError::RemoteFailure { status }),
    }
}

fn store_contact_password(
    runtime: &Runtime,
    args: &ConnectArgs,
    serial: &mut dyn CommandExchange,
    prefix: &prost::bytes::Bytes,
    password: &[u8],
) -> Result<(), ConnectError> {
    let find = runtime.by_path(&["contact", "find_prefix"])?;
    let mut find_request = runtime.new_message(&find.request)?;
    set_named_field(
        &mut find_request,
        "public_key_prefix",
        Value::Bytes(prefix.clone()),
    )?;
    let found = invoke(runtime, args, serial, find, &find_request.encode_to_vec())?;
    let contact = read_path(&found, &["contact".into()])?;
    let mut contact = match contact {
        Value::Message(contact) => contact,
        _ => return Err(ConnectError::Protocol("contact was not returned".into())),
    };
    set_named_field(
        &mut contact,
        "management_secret",
        Value::Bytes(password.to_vec().into()),
    )?;
    let set = runtime.by_path(&["contact", "set"])?;
    let mut set_request = runtime.new_message(&set.request)?;
    set_named_field(
        &mut set_request,
        "public_key_prefix",
        Value::Bytes(prefix.clone()),
    )?;
    set_named_field(&mut set_request, "contact", Value::Message(contact))?;
    invoke(runtime, args, serial, set, &set_request.encode_to_vec())?;
    Ok(())
}

fn parse_u16_usage(value: &str, label: &str) -> Result<u16, ConnectError> {
    parse_integer(value)
        .ok()
        .and_then(|value| u16::try_from(value).ok())
        .ok_or_else(|| ConnectError::Usage(format!("{label} expects an in-range integer")))
}

fn parse_u8_usage(value: &str, label: &str) -> Result<u8, ConnectError> {
    parse_integer(value)
        .ok()
        .and_then(|value| u8::try_from(value).ok())
        .ok_or_else(|| ConnectError::Usage(format!("{label} expects an in-range integer")))
}

fn bytes_field(message: &DynamicMessage, name: &str) -> Result<Vec<u8>, ConnectError> {
    match read_path(message, &[name.to_owned()])? {
        Value::Bytes(value) => Ok(value.to_vec()),
        _ => Err(ConnectError::Schema(format!(
            "protobuf field is not bytes: {name}"
        ))),
    }
}

fn hex_bytes(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

fn require_word_count(
    command: &CommandSpec,
    words: &[String],
    minimum: usize,
    maximum: usize,
) -> Result<(), ConnectError> {
    if (minimum..=maximum).contains(&words.len()) {
        Ok(())
    } else {
        Err(ConnectError::Usage(format!("usage: {}", command.usage)))
    }
}

fn parse_management_password(value: &str) -> Result<Vec<u8>, ConnectError> {
    let bytes = value.as_bytes();
    if !(8..=16).contains(&bytes.len()) || !bytes.iter().all(|byte| (0x21..=0x7e).contains(byte)) {
        return Err(ConnectError::Usage(
            "management password must contain 8..16 printable ASCII characters".into(),
        ));
    }
    Ok(bytes.to_vec())
}

fn parse_hex_usage(value: &str) -> Result<prost::bytes::Bytes, ConnectError> {
    parse_hex(value).map_err(|_| ConnectError::Usage("expected hexadecimal bytes".into()))
}

fn set_named_field(
    message: &mut DynamicMessage,
    name: &str,
    value: Value,
) -> Result<(), ConnectError> {
    let field = message
        .descriptor()
        .get_field_by_name(name)
        .ok_or_else(|| ConnectError::Schema(format!("protobuf field not found: {name}")))?;
    message.set_field(&field, value);
    Ok(())
}

fn set_scalar_text(
    message: &mut DynamicMessage,
    name: &str,
    text: &str,
) -> Result<(), ConnectError> {
    let path = vec![name.to_owned()];
    let value = parse_argument(message, &path, text)?;
    assign_path(message, &path, value)
}

fn read_path(message: &DynamicMessage, path: &[String]) -> Result<Value, ConnectError> {
    let field = message
        .descriptor()
        .get_field_by_name(&path[0])
        .ok_or_else(|| {
            ConnectError::Schema(format!("protobuf field not found: {}", path.join(".")))
        })?;
    let value = message.get_field(&field).into_owned();
    if path.len() == 1 {
        return Ok(value);
    }
    match value {
        Value::Message(child) => read_path(&child, &path[1..]),
        _ => Err(ConnectError::Schema(format!(
            "protobuf field is not a message: {}",
            path[0]
        ))),
    }
}

fn bool_field(message: &DynamicMessage, name: &str) -> Result<bool, ConnectError> {
    match read_path(message, &[name.to_owned()])? {
        Value::Bool(value) => Ok(value),
        _ => Err(ConnectError::Schema(format!(
            "protobuf field is not bool: {name}"
        ))),
    }
}

fn integer_field(message: &DynamicMessage, name: &str) -> Result<i128, ConnectError> {
    match read_path(message, &[name.to_owned()])? {
        Value::I32(value) => Ok(value.into()),
        Value::I64(value) => Ok(value.into()),
        Value::U32(value) => Ok(value.into()),
        Value::U64(value) => Ok(value.into()),
        Value::EnumNumber(value) => Ok(value.into()),
        _ => Err(ConnectError::Schema(format!(
            "protobuf field is not an integer: {name}"
        ))),
    }
}

struct SerialSession {
    port: Box<dyn SerialPort>,
    baudrate: u32,
    pending: Vec<u8>,
    background: Option<BackgroundReader>,
    next_sequence: u8,
}

enum SerialEvent {
    Frame(Vec<u8>),
    Error(String),
}

struct BackgroundReader {
    events: mpsc::Receiver<SerialEvent>,
    stop: Arc<AtomicBool>,
    thread: Option<thread::JoinHandle<()>>,
}

impl SerialSession {
    fn open(args: &ConnectArgs) -> Result<Self, ConnectError> {
        let port = serialport::new(&args.port, args.baudrate)
            .timeout(Duration::from_millis(20))
            .open()
            .map_err(|error| {
                ConnectError::Transport(format!("cannot open serial port {}: {error}", args.port))
            })?;
        let _ = port.clear(ClearBuffer::Input);
        Ok(Self {
            port,
            baudrate: args.baudrate,
            pending: Vec::new(),
            background: None,
            next_sequence: 0,
        })
    }

    fn allocate_sequence(&mut self) -> u8 {
        allocate_sequence(&mut self.next_sequence)
    }

    fn start_background<F>(&mut self, args: &ConnectArgs, printer: F) -> Result<(), ConnectError>
    where
        F: FnMut(String) -> Result<(), String> + Send + 'static,
    {
        let reader = self.port.try_clone().map_err(|error| {
            ConnectError::Transport(format!("cannot clone serial port for device logs: {error}"))
        })?;
        let log_file = args
            .log_file
            .as_ref()
            .map(|path| {
                OpenOptions::new()
                    .create(true)
                    .append(true)
                    .open(path)
                    .map_err(|error| {
                        ConnectError::Transport(format!(
                            "cannot open log file {}: {error}",
                            path.display()
                        ))
                    })
            })
            .transpose()?;
        let stop = Arc::new(AtomicBool::new(false));
        let reader_stop = Arc::clone(&stop);
        let (sender, events) = mpsc::channel();
        let quiet_logs = args.quiet_logs;
        let color = args.color;
        let thread = thread::Builder::new()
            .name("meshbus-serial-reader".into())
            .spawn(move || {
                background_read_loop(
                    reader,
                    reader_stop,
                    sender,
                    printer,
                    log_file,
                    quiet_logs,
                    color,
                );
            })
            .map_err(|error| {
                ConnectError::Transport(format!("cannot start serial reader: {error}"))
            })?;
        self.background = Some(BackgroundReader {
            events,
            stop,
            thread: Some(thread),
        });
        Ok(())
    }

    fn send(&mut self, frame: &[u8]) -> Result<(), ConnectError> {
        let mut wrapped = Vec::with_capacity(frame.len() + 4);
        wrapped.extend_from_slice(&((frame.len() + 2) as u16).to_be_bytes());
        wrapped.extend_from_slice(frame);
        wrapped.extend_from_slice(&CRC16.checksum(frame).to_be_bytes());
        let encoded = BASE64.encode(wrapped);
        let packet_size = ((LINE_LENGTH - 4) / 4) * 4;
        for (index, chunk) in encoded.as_bytes().chunks(packet_size).enumerate() {
            let mut line = Vec::with_capacity(chunk.len() + 3);
            line.extend_from_slice(if index == 0 { &START } else { &CONTINUE });
            line.extend_from_slice(chunk);
            line.push(b'\n');
            self.port.write_all(&line).map_err(|error| {
                ConnectError::Transport(format!("serial write failed: {error}"))
            })?;
            thread::sleep(serial_line_pacing_delay(line.len(), self.baudrate));
        }
        /*
         * A POSIX serial flush maps to tcdrain(), which has no bounded timeout
         * on macOS USB CDC ports. The per-line physical-rate delay bounds how
         * far the host can run ahead of a small USB-to-UART bridge; the matching
         * SMP response remains the packet-level delivery synchronization point.
         */
        Ok(())
    }

    fn receive(
        &mut self,
        timeout: f64,
        quiet_logs: bool,
        log_file: Option<&PathBuf>,
        color: ConnectColor,
        command: &CommandSpec,
        sequence: u8,
    ) -> Result<Vec<u8>, ConnectError> {
        if let Some(background) = &self.background {
            return receive_background(background, timeout, command, sequence);
        }
        let deadline = Instant::now() + Duration::from_secs_f64(timeout);
        let mut decoded = Vec::new();
        let mut started = false;
        while Instant::now() < deadline {
            let mut buffer = [0_u8; 1024];
            match self.port.read(&mut buffer) {
                Ok(count) if count != 0 => self.pending.extend_from_slice(&buffer[..count]),
                Ok(_) => {}
                Err(error) if error.kind() == io::ErrorKind::TimedOut => {}
                Err(error) => {
                    return Err(ConnectError::Transport(format!(
                        "serial read failed: {error}"
                    )));
                }
            }
            while let Some(line_end) = self.pending.iter().position(|byte| *byte == b'\n') {
                let line = self.pending.drain(..=line_end).collect::<Vec<_>>();
                let delimiter = line.get(..2);
                if delimiter == Some(&START) || delimiter == Some(&CONTINUE) {
                    if delimiter == Some(&START) {
                        decoded.clear();
                        started = true;
                    } else if !started {
                        continue;
                    }
                    let payload = &line[2..line.len() - 1];
                    let chunk = BASE64.decode(payload).map_err(|error| {
                        ConnectError::Protocol(format!("invalid SMP base64 response: {error}"))
                    })?;
                    decoded.extend_from_slice(&chunk);
                    if let Some(frame) = complete_serial_frame(&decoded)? {
                        if frame_matches(&frame, command, sequence) {
                            return Ok(frame);
                        }
                        decoded.clear();
                        started = false;
                    }
                } else {
                    write_device_log(&line, quiet_logs, log_file, color)?;
                }
            }
        }
        Err(ConnectError::Transport(format!(
            "timed out after {timeout} s waiting for firmware"
        )))
    }
}

fn serial_line_pacing_delay(line_length: usize, baudrate: u32) -> Duration {
    let wire_bits = u64::try_from(line_length)
        .unwrap_or(u64::MAX / SERIAL_BITS_PER_BYTE)
        .saturating_mul(SERIAL_BITS_PER_BYTE);
    let baudrate = u64::from(baudrate.max(1));
    let wire_time_us = wire_bits
        .saturating_mul(1_000_000)
        .saturating_add(baudrate - 1)
        / baudrate;
    Duration::from_micros(wire_time_us.saturating_add(SERIAL_LINE_GUARD_US))
}

fn allocate_sequence(next: &mut u8) -> u8 {
    let sequence = *next;
    *next = next.wrapping_add(1);
    sequence
}

impl Drop for SerialSession {
    fn drop(&mut self) {
        if let Some(background) = &mut self.background {
            background.stop.store(true, Ordering::Relaxed);
            if let Some(thread) = background.thread.take() {
                let _ = thread.join();
            }
        }
    }
}

fn receive_background(
    background: &BackgroundReader,
    timeout: f64,
    command: &CommandSpec,
    sequence: u8,
) -> Result<Vec<u8>, ConnectError> {
    let deadline = Instant::now() + Duration::from_secs_f64(timeout);
    loop {
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            break;
        }
        match background.events.recv_timeout(remaining) {
            Ok(SerialEvent::Frame(frame)) if frame_matches(&frame, command, sequence) => {
                return Ok(frame);
            }
            Ok(SerialEvent::Frame(_)) => {}
            Ok(SerialEvent::Error(error)) => return Err(ConnectError::Transport(error)),
            Err(mpsc::RecvTimeoutError::Timeout) => break,
            Err(mpsc::RecvTimeoutError::Disconnected) => {
                return Err(ConnectError::Transport(
                    "serial reader stopped unexpectedly".into(),
                ));
            }
        }
    }
    Err(ConnectError::Transport(format!(
        "timed out after {timeout} s waiting for firmware"
    )))
}

fn background_read_loop<F>(
    mut port: Box<dyn SerialPort>,
    stop: Arc<AtomicBool>,
    sender: mpsc::Sender<SerialEvent>,
    mut printer: F,
    mut log_file: Option<std::fs::File>,
    quiet_logs: bool,
    color: ConnectColor,
) where
    F: FnMut(String) -> Result<(), String>,
{
    let mut pending = Vec::new();
    let mut decoded = Vec::new();
    let mut started = false;
    'read: while !stop.load(Ordering::Relaxed) {
        let mut buffer = [0_u8; 1024];
        match port.read(&mut buffer) {
            Ok(count) if count != 0 => pending.extend_from_slice(&buffer[..count]),
            Ok(_) => {}
            Err(error) if error.kind() == io::ErrorKind::TimedOut => {}
            Err(error) => {
                let _ = sender.send(SerialEvent::Error(format!("serial read failed: {error}")));
                break 'read;
            }
        }
        while let Some(line_end) = pending.iter().position(|byte| *byte == b'\n') {
            let line = pending.drain(..=line_end).collect::<Vec<_>>();
            let delimiter = line.get(..2);
            if delimiter == Some(&START) || delimiter == Some(&CONTINUE) {
                if delimiter == Some(&START) {
                    decoded.clear();
                    started = true;
                } else if !started {
                    continue;
                }
                let payload = &line[2..line.len() - 1];
                let chunk = match BASE64.decode(payload) {
                    Ok(chunk) => chunk,
                    Err(error) => {
                        let _ = sender.send(SerialEvent::Error(format!(
                            "invalid SMP base64 response: {error}"
                        )));
                        decoded.clear();
                        started = false;
                        continue;
                    }
                };
                decoded.extend_from_slice(&chunk);
                match complete_serial_frame(&decoded) {
                    Ok(Some(frame)) => {
                        if sender.send(SerialEvent::Frame(frame)).is_err() {
                            break 'read;
                        }
                        decoded.clear();
                        started = false;
                    }
                    Ok(None) => {}
                    Err(error) => {
                        let _ = sender.send(SerialEvent::Error(error.to_string()));
                        decoded.clear();
                        started = false;
                    }
                }
            } else if let Err(error) =
                background_device_log(&line, &mut printer, log_file.as_mut(), quiet_logs, color)
            {
                let _ = sender.send(SerialEvent::Error(error));
                break 'read;
            }
        }
    }
    /* The background reader is used only by the interactive console. */
}

fn background_device_log<F>(
    data: &[u8],
    printer: &mut F,
    mut log_file: Option<&mut std::fs::File>,
    quiet: bool,
    color: ConnectColor,
) -> Result<(), String>
where
    F: FnMut(String) -> Result<(), String>,
{
    if let Some(file) = log_file.as_mut() {
        file.write_all(data)
            .and_then(|_| file.flush())
            .map_err(|error| format!("cannot write device log: {error}"))?;
    }
    if !quiet {
        let text = String::from_utf8_lossy(data);
        let text = text.trim_end_matches(['\r', '\n']);
        if !text.is_empty() {
            printer(console::colorize_device_log(text, color))?;
        }
    }
    Ok(())
}

fn complete_serial_frame(decoded: &[u8]) -> Result<Option<Vec<u8>>, ConnectError> {
    if decoded.len() < 2 {
        return Ok(None);
    }
    let length = u16::from_be_bytes([decoded[0], decoded[1]]) as usize;
    if length > MAX_FRAME_SIZE {
        return Err(ConnectError::Protocol(format!(
            "SMP response exceeds {MAX_FRAME_SIZE}-byte buffer"
        )));
    }
    if decoded.len() < length + 2 {
        return Ok(None);
    }
    if length < 2 {
        return Err(ConnectError::Protocol("SMP response is too short".into()));
    }
    let frame_end = 2 + length - 2;
    let frame = &decoded[2..frame_end];
    let expected = u16::from_be_bytes([decoded[frame_end], decoded[frame_end + 1]]);
    let actual = CRC16.checksum(frame);
    if expected != actual {
        return Err(ConnectError::Protocol(format!(
            "SMP CRC mismatch: received 0x{expected:04x}, calculated 0x{actual:04x}"
        )));
    }
    Ok(Some(frame.to_vec()))
}

fn write_device_log(
    data: &[u8],
    quiet: bool,
    log_file: Option<&PathBuf>,
    color: ConnectColor,
) -> Result<(), ConnectError> {
    if let Some(path) = log_file {
        OpenOptions::new()
            .create(true)
            .append(true)
            .open(path)
            .and_then(|mut file| file.write_all(data))
            .map_err(|error| {
                ConnectError::Transport(format!(
                    "cannot write log file {}: {error}",
                    path.display()
                ))
            })?;
    }
    if !quiet {
        let text = String::from_utf8_lossy(data);
        let text = console::colorize_device_log(&text, color);
        io::stderr()
            .write_all(text.as_bytes())
            .and_then(|_| io::stderr().flush())
            .map_err(|error| {
                ConnectError::Transport(format!("cannot write device log: {error}"))
            })?;
    }
    Ok(())
}

fn build_smp_request(
    command: &CommandSpec,
    protobuf: &[u8],
    sequence: u8,
) -> Result<Vec<u8>, ConnectError> {
    let cbor = encode_smp_data_payload(protobuf)?;
    let operation = match command.op.as_str() {
        "read" => 0_u8,
        "write" => 2_u8,
        value => {
            return Err(ConnectError::Schema(format!(
                "invalid SMP operation: {value}"
            )));
        }
    };
    let mut frame = Vec::with_capacity(8 + cbor.len());
    frame.push((1 << 3) | operation);
    frame.push(0);
    frame.extend_from_slice(&(cbor.len() as u16).to_be_bytes());
    frame.extend_from_slice(&command.group_id.to_be_bytes());
    frame.push(sequence);
    frame.push(command.command_id);
    frame.extend_from_slice(&cbor);
    Ok(frame)
}

fn encode_smp_data_payload(protobuf: &[u8]) -> Result<Vec<u8>, ConnectError> {
    let mut cbor = Vec::new();
    ciborium::ser::into_writer(
        &CborValue::Map(vec![(
            CborValue::Text("data".into()),
            CborValue::Bytes(protobuf.to_vec()),
        )]),
        &mut cbor,
    )
    .map_err(|error| ConnectError::Protocol(format!("cannot encode SMP request: {error}")))?;
    Ok(cbor)
}

fn frame_matches(frame: &[u8], command: &CommandSpec, sequence: u8) -> bool {
    frame.len() >= 8
        && u16::from_be_bytes([frame[4], frame[5]]) == command.group_id
        && frame[6] == sequence
        && frame[7] == command.command_id
}

fn parse_smp_response(command: &CommandSpec, frame: &[u8]) -> Result<Vec<u8>, ConnectError> {
    if frame.len() < 8 {
        return Err(ConnectError::Protocol(
            "SMP response header is truncated".into(),
        ));
    }
    let payload_length = u16::from_be_bytes([frame[2], frame[3]]) as usize;
    if frame.len() != 8 + payload_length {
        return Err(ConnectError::Protocol(
            "SMP response length is invalid".into(),
        ));
    }
    let value: CborValue = ciborium::de::from_reader(&frame[8..])
        .map_err(|error| ConnectError::Protocol(format!("invalid SMP CBOR response: {error}")))?;
    let map = value
        .as_map()
        .ok_or_else(|| ConnectError::Protocol("SMP response is not a CBOR map".into()))?;
    if let Some(data) = cbor_lookup(map, "data").and_then(CborValue::as_bytes) {
        return Ok(data.to_vec());
    }
    if let Some(rc) = cbor_lookup(map, "rc").and_then(cbor_integer) {
        let reason = cbor_lookup(map, "rsn")
            .and_then(CborValue::as_text)
            .map(str::to_owned);
        return Err(mcumgr_error(command, command.group_id.into(), rc, reason));
    }
    if let Some(error) = cbor_lookup(map, "err").and_then(CborValue::as_map) {
        let group = cbor_lookup(error, "group")
            .and_then(cbor_integer)
            .unwrap_or(command.group_id as i128);
        let rc = cbor_lookup(error, "rc")
            .and_then(cbor_integer)
            .unwrap_or(-1);
        let reason = cbor_lookup(map, "rsn")
            .and_then(CborValue::as_text)
            .map(str::to_owned);
        return Err(mcumgr_error(command, group, rc, reason));
    }
    Err(ConnectError::Protocol(
        "SMP response contains neither data nor an MCUmgr error".into(),
    ))
}

fn mcumgr_error(
    command: &CommandSpec,
    group: i128,
    rc: i128,
    reason: Option<String>,
) -> ConnectError {
    const MGMT_ERR_ENOTSUP: i128 = 8;
    const POSIX_EACCES: i128 = 13;
    const POSIX_ENOTSUP: i128 = 95;
    let code = if rc == MGMT_ERR_ENOTSUP || rc == POSIX_ENOTSUP || rc == -POSIX_ENOTSUP {
        "unsupported"
    } else if rc == POSIX_EACCES || rc == -POSIX_EACCES {
        "permission_denied"
    } else {
        "mcumgr_error"
    };
    let message = if code == "unsupported" {
        format!("command is unsupported by firmware (group={group}, rc={rc})")
    } else if code == "permission_denied"
        && command
            .path
            .starts_with(&["indicator".into(), "buzzer".into()])
    {
        format!(
            "indicator buzzer playback is disabled by current feedback settings; check 'indicator config' and enable both config.buzzer_enabled and config.buzzer_feedback.system_enabled (group={group}, rc={rc})"
        )
    } else {
        let mut message = format!("MCUmgr request failed (group={group}, rc={rc})");
        if let Some(reason) = reason {
            message.push_str(": ");
            message.push_str(&reason);
        }
        message
    };
    ConnectError::Mcumgr {
        code,
        message,
        group,
        rc,
    }
}

fn cbor_lookup<'a>(map: &'a [(CborValue, CborValue)], key: &str) -> Option<&'a CborValue> {
    map.iter()
        .find(|(candidate, _)| candidate.as_text() == Some(key))
        .map(|(_, value)| value)
}

fn cbor_integer(value: &CborValue) -> Option<i128> {
    value.as_integer().map(Into::into)
}

fn leaf_field(message: &DynamicMessage, path: &[String]) -> Result<FieldDescriptor, ConnectError> {
    let mut descriptor = message.descriptor();
    let mut leaf = None;
    for (index, component) in path.iter().enumerate() {
        let field = descriptor.get_field_by_name(component).ok_or_else(|| {
            ConnectError::Schema(format!("protobuf field not found: {}", path.join(".")))
        })?;
        if index + 1 != path.len() {
            descriptor = match field.kind() {
                Kind::Message(message) => message,
                _ => {
                    return Err(ConnectError::Schema(format!(
                        "protobuf field is not a message: {component}"
                    )));
                }
            };
        }
        leaf = Some(field);
    }
    leaf.ok_or_else(|| ConnectError::Schema("empty protobuf field path".into()))
}

fn parse_argument(
    message: &DynamicMessage,
    path: &[String],
    word: &str,
) -> Result<Value, ConnectError> {
    let field = leaf_field(message, path)?;
    let name = path.join(".");
    let invalid = |expectation: &str| ConnectError::Usage(format!("{name} expects {expectation}"));
    match field.kind() {
        Kind::Bool => match word.to_ascii_lowercase().as_str() {
            "1" | "true" | "yes" | "on" | "enable" | "enabled" => Ok(Value::Bool(true)),
            "0" | "false" | "no" | "off" | "disable" | "disabled" => Ok(Value::Bool(false)),
            _ => Err(invalid("on/off")),
        },
        Kind::Int32 | Kind::Sint32 | Kind::Sfixed32 => parse_integer(word)
            .and_then(|value| i32::try_from(value).map_err(|_| ()))
            .map(Value::I32)
            .map_err(|_| invalid("an in-range integer")),
        Kind::Int64 | Kind::Sint64 | Kind::Sfixed64 => parse_integer(word)
            .and_then(|value| i64::try_from(value).map_err(|_| ()))
            .map(Value::I64)
            .map_err(|_| invalid("an in-range integer")),
        Kind::Uint32 | Kind::Fixed32 => parse_integer(word)
            .and_then(|value| u32::try_from(value).map_err(|_| ()))
            .map(Value::U32)
            .map_err(|_| invalid("a non-negative in-range integer")),
        Kind::Uint64 | Kind::Fixed64 => parse_integer(word)
            .and_then(|value| u64::try_from(value).map_err(|_| ()))
            .map(Value::U64)
            .map_err(|_| invalid("a non-negative in-range integer")),
        Kind::Float => word
            .parse::<f32>()
            .map(Value::F32)
            .map_err(|_| invalid("a number")),
        Kind::Double => word
            .parse::<f64>()
            .map(Value::F64)
            .map_err(|_| invalid("a number")),
        Kind::String => Ok(Value::String(word.into())),
        Kind::Bytes => parse_hex(word)
            .map(Value::Bytes)
            .map_err(|_| invalid("hexadecimal bytes")),
        Kind::Enum(enumeration) => {
            let normalized = word.to_ascii_lowercase();
            if let Some(value) = enumeration.values().find(|value| {
                let full = value.name().to_ascii_lowercase();
                normalized == full || full.ends_with(&format!("_{normalized}"))
            }) {
                return Ok(Value::EnumNumber(value.number()));
            }
            let number = parse_integer(word)
                .and_then(|value| i32::try_from(value).map_err(|_| ()))
                .map_err(|_| invalid("a valid enum name or number"))?;
            if enumeration.get_value(number).is_some() {
                Ok(Value::EnumNumber(number))
            } else {
                Err(invalid("a valid enum name or number"))
            }
        }
        _ => Err(invalid("a supported scalar value")),
    }
}

fn json_to_protobuf(
    message: &DynamicMessage,
    path: &[String],
    value: &JsonValue,
) -> Result<Value, ConnectError> {
    let field = leaf_field(message, path)?;
    match (field.kind(), value) {
        (Kind::Bool, JsonValue::Bool(value)) => Ok(Value::Bool(*value)),
        _ => Err(ConnectError::Schema(format!(
            "unsupported default for {}",
            path.join(".")
        ))),
    }
}

fn assign_path(
    message: &mut DynamicMessage,
    path: &[String],
    value: Value,
) -> Result<(), ConnectError> {
    let field = message
        .descriptor()
        .get_field_by_name(&path[0])
        .ok_or_else(|| {
            ConnectError::Schema(format!("protobuf field not found: {}", path.join(".")))
        })?;
    if path.len() == 1 {
        message.set_field(&field, value);
        return Ok(());
    }
    let mut child = match message.get_field(&field) {
        Cow::Borrowed(Value::Message(child)) => child.clone(),
        Cow::Owned(Value::Message(child)) => child,
        _ => match field.kind() {
            Kind::Message(descriptor) => DynamicMessage::new(descriptor),
            _ => {
                return Err(ConnectError::Schema(format!(
                    "protobuf field is not a message: {}",
                    path[0]
                )));
            }
        },
    };
    assign_path(&mut child, &path[1..], value)?;
    message.set_field(&field, Value::Message(child));
    Ok(())
}

fn parse_integer(value: &str) -> Result<i128, ()> {
    let value = value.trim();
    let (negative, unsigned) = value
        .strip_prefix('-')
        .map(|value| (true, value))
        .or_else(|| value.strip_prefix('+').map(|value| (false, value)))
        .unwrap_or((false, value));
    let (radix, digits) = if let Some(value) = unsigned
        .strip_prefix("0x")
        .or_else(|| unsigned.strip_prefix("0X"))
    {
        (16, value)
    } else if let Some(value) = unsigned
        .strip_prefix("0o")
        .or_else(|| unsigned.strip_prefix("0O"))
    {
        (8, value)
    } else if let Some(value) = unsigned
        .strip_prefix("0b")
        .or_else(|| unsigned.strip_prefix("0B"))
    {
        (2, value)
    } else {
        (10, unsigned)
    };
    let parsed = i128::from_str_radix(digits, radix).map_err(|_| ())?;
    Ok(if negative { -parsed } else { parsed })
}

fn parse_hex(value: &str) -> Result<prost::bytes::Bytes, ()> {
    let normalized = value
        .strip_prefix("0x")
        .unwrap_or(value)
        .replace([':', '-'], "");
    if !normalized.len().is_multiple_of(2) {
        return Err(());
    }
    (0..normalized.len())
        .step_by(2)
        .map(|offset| u8::from_str_radix(&normalized[offset..offset + 2], 16).map_err(|_| ()))
        .collect::<Result<Vec<_>, _>>()
        .map(Into::into)
}

fn dynamic_to_json(message: &DynamicMessage) -> JsonValue {
    let mut object = JsonMap::new();
    for field in message.descriptor().fields() {
        if field.cardinality() == Cardinality::Repeated
            || !field.supports_presence()
            || message.has_field(&field)
        {
            object.insert(
                field.name().into(),
                protobuf_value_to_json(&field, message.get_field(&field)),
            );
        }
    }
    JsonValue::Object(object)
}

fn protobuf_value_to_json(field: &FieldDescriptor, value: Cow<'_, Value>) -> JsonValue {
    match value.as_ref() {
        Value::Bool(value) => JsonValue::Bool(*value),
        Value::I32(value) => json!(value),
        Value::I64(value) => json!(value),
        Value::U32(value) => json!(value),
        Value::U64(value) => json!(value),
        Value::F32(value) => json!(value),
        Value::F64(value) => json!(value),
        Value::String(value) => JsonValue::String(value.clone()),
        Value::Bytes(value) => {
            JsonValue::String(value.iter().map(|byte| format!("{byte:02x}")).collect())
        }
        Value::EnumNumber(number) => match field.kind() {
            Kind::Enum(enumeration) => enumeration
                .get_value(*number)
                .map(|value| JsonValue::String(value.name().to_ascii_lowercase()))
                .unwrap_or_else(|| json!(number)),
            _ => json!(number),
        },
        Value::Message(message) => dynamic_to_json(message),
        Value::List(values) => JsonValue::Array(
            values
                .iter()
                .map(|value| protobuf_value_to_json(field, Cow::Borrowed(value)))
                .collect(),
        ),
        Value::Map(values) => JsonValue::Object(
            values
                .iter()
                .map(|(key, value)| {
                    (
                        format!("{key:?}"),
                        protobuf_value_to_json(field, Cow::Borrowed(value)),
                    )
                })
                .collect(),
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn command_registry_matches_embedded_schema() {
        let runtime = Runtime::load().unwrap();
        assert_eq!(runtime.registry.commands.len(), 126);
        for command in &runtime.registry.commands {
            assert!(
                runtime
                    .pool
                    .get_message_by_name(&format!("meshbus.{}", command.request))
                    .is_some(),
                "missing request {}",
                command.request
            );
            assert!(
                runtime
                    .pool
                    .get_message_by_name(&format!("meshbus.{}", command.response))
                    .is_some(),
                "missing response {}",
                command.response
            );
        }
    }

    #[test]
    fn radio_status_request_is_empty() {
        let runtime = Runtime::load().unwrap();
        let words = vec!["radio".into(), "status".into()];
        let (command, arguments) = runtime.resolve(&words).unwrap();
        assert!(arguments.is_empty());
        assert!(runtime.request(command, arguments).unwrap().is_empty());
        assert_eq!(command.group_id, 71);
        assert_eq!(command.command_id, 1);
    }

    #[test]
    fn serial_frame_round_trip() {
        let frame = b"meshbus frame";
        let mut encoded = Vec::new();
        encoded.extend_from_slice(&((frame.len() + 2) as u16).to_be_bytes());
        encoded.extend_from_slice(frame);
        encoded.extend_from_slice(&CRC16.checksum(frame).to_be_bytes());
        assert_eq!(
            complete_serial_frame(&encoded).unwrap(),
            Some(frame.to_vec())
        );
    }

    #[test]
    fn serial_line_pacing_covers_wire_time_and_guard() {
        assert_eq!(
            serial_line_pacing_delay(127, 115_200),
            Duration::from_micros(13_025)
        );
        assert_eq!(
            serial_line_pacing_delay(63, 115_200),
            Duration::from_micros(7_469)
        );
    }

    #[test]
    fn request_sequences_advance_and_wrap() {
        let mut next = 0;
        assert_eq!(allocate_sequence(&mut next), 0);
        assert_eq!(allocate_sequence(&mut next), 1);

        next = u8::MAX;
        assert_eq!(allocate_sequence(&mut next), u8::MAX);
        assert_eq!(allocate_sequence(&mut next), 0);
    }

    #[test]
    fn remote_result_status_keeps_failure_and_timeout_identity() {
        assert!(remote_result_error(0).is_none());

        let failure = remote_result_error(-22).unwrap();
        assert_eq!(failure.code(), "remote_failed");
        assert!(matches!(
            failure,
            ConnectError::RemoteFailure { status: -22 }
        ));

        let timeout = remote_result_error(-110).unwrap();
        assert_eq!(timeout.code(), "remote_timeout");
        assert!(matches!(
            timeout,
            ConnectError::RemoteTimeout {
                status: Some(-110),
                ..
            }
        ));
    }

    #[test]
    fn remote_request_preserves_legacy_one_shot_capacity() {
        assert_eq!(remote_request_chunk_end(448, 0), 448);
        assert_eq!(remote_request_chunk_end(449, 0), 449);
        assert_eq!(remote_request_chunk_end(504, 0), 504);

        assert_eq!(remote_request_chunk_end(505, 0), 448);
        assert_eq!(remote_request_chunk_end(505, 448), 505);
        assert_eq!(remote_request_chunk_end(600, 448), 600);
    }

    #[test]
    fn remote_one_shot_max_fits_local_uart_mtu() {
        let runtime = Runtime::load().unwrap();
        let command = runtime.by_path(&["management", "remote", "smp"]).unwrap();
        let mut request = runtime.new_message(&command.request).unwrap();
        set_named_field(
            &mut request,
            "contact_prefix",
            Value::Bytes(vec![1, 2, 3, 4, 5].into()),
        )
        .unwrap();
        set_scalar_text(&mut request, "group", "71").unwrap();
        set_scalar_text(&mut request, "command", "1").unwrap();
        set_scalar_text(&mut request, "op", "0").unwrap();
        set_named_field(
            &mut request,
            "payload",
            Value::Bytes(vec![0x5a; REMOTE_REQUEST_ONE_SHOT_MAX].into()),
        )
        .unwrap();

        let frame = build_smp_request(command, &request.encode_to_vec(), 0).unwrap();
        assert_eq!(frame.len(), 535);
        assert!(frame.len() <= MAX_FRAME_SIZE);
    }

    #[test]
    fn remote_password_contact_failure_reports_partial_success_without_secret() {
        let error = remote_password_contact_update_error(
            &[1, 2, 3, 4],
            &ConnectError::Mcumgr {
                code: "mcumgr_error",
                message: "contact write failed".into(),
                group: 70,
                rc: -5,
            },
        );

        assert_eq!(error.code(), "remote_password_updated_contact_failed");
        assert!(error.to_string().contains("remote password changed"));
        assert!(matches!(
            error,
            ConnectError::RemotePasswordContactUpdate {
                ref contact_prefix,
                ref local_error,
            } if contact_prefix == "01020304" && local_error == "mcumgr_error"
        ));
    }

    #[test]
    fn every_catalog_custom_handler_is_implemented() {
        let runtime = Runtime::load().unwrap();
        let supported = [
            "config",
            "contact_set",
            "fs_format",
            "input_inject_act",
            "input_inject_raw",
            "management_local_password",
            "management_remote",
            "management_remote_raw",
            "management_remote_raw_password",
            "message_send_node",
            "radio_send",
        ];
        for command in &runtime.registry.commands {
            if let Some(custom) = command.custom.as_deref() {
                assert!(supported.contains(&custom), "unsupported handler: {custom}");
            }
        }
    }

    #[test]
    fn config_reset_requires_confirmation_before_opening_serial() {
        let runtime = Runtime::load().unwrap();
        let args = ConnectArgs {
            port: "/not/a/serial/port".into(),
            baudrate: 115_200,
            timeout: 1.0,
            remote_timeout: 1.0,
            command: None,
            json: false,
            quiet_logs: true,
            color: ConnectColor::Never,
            log_file: None,
            no_history: true,
            yes: false,
        };
        let error = execute_one(&runtime, &args, "radio config --reset").unwrap_err();
        assert!(error.to_string().contains("explicit confirmation"));
    }

    #[test]
    fn mcumgr_errors_preserve_machine_readable_status() {
        let runtime = Runtime::load().unwrap();
        let command = runtime.by_path(&["radio", "status"]).unwrap();
        let unsupported = mcumgr_error(command, 71, 95, None);
        assert_eq!(unsupported.code(), "unsupported");
        assert_eq!(unsupported.exit_status(), 3);

        let denied = mcumgr_error(command, 71, -13, Some("denied".into()));
        assert_eq!(denied.code(), "permission_denied");
        assert_eq!(denied.exit_status(), 4);
        assert!(denied.to_string().contains("denied"));
    }
}
