// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

use std::collections::VecDeque;

use prost::Message;
use prost_reflect::{DynamicMessage, Kind, ReflectMessage, Value};
use serde_json::{Value as JsonValue, json};

use super::super::{
    CommandExchange, CommandSpec, ConnectArgs, ConnectColor, ConnectError, Runtime,
    confirmation_command, execute_one_with_exchange,
};

// Exercise the production command entry and protobuf decoding without opening a
// serial port. Unexpected exchanges fail the test, including writes after errors.
struct MemoryExchange<'a> {
    runtime: &'a Runtime,
    replies: VecDeque<(&'static str, Result<Vec<u8>, ConnectError>)>,
    requests: Vec<DynamicMessage>,
}

impl<'a> MemoryExchange<'a> {
    fn new(
        runtime: &'a Runtime,
        replies: Vec<(&'static str, Result<Vec<u8>, ConnectError>)>,
    ) -> Self {
        Self {
            runtime,
            replies: replies.into(),
            requests: Vec::new(),
        }
    }

    fn finish(self) -> Vec<DynamicMessage> {
        assert!(
            self.replies.is_empty(),
            "expected exchanges were not performed"
        );
        self.requests
    }
}

impl CommandExchange for MemoryExchange<'_> {
    fn exchange(
        &mut self,
        _args: &ConnectArgs,
        command: &CommandSpec,
        protobuf: &[u8],
    ) -> Result<Vec<u8>, ConnectError> {
        let (expected, reply) = self
            .replies
            .pop_front()
            .expect("unexpected device exchange");
        assert_eq!(command.path.join(" "), expected);
        let descriptor = self.runtime.new_message(&command.request)?.descriptor();
        self.requests
            .push(DynamicMessage::decode(descriptor, protobuf).unwrap());
        reply
    }
}

fn execute(
    runtime: &Runtime,
    exchange: &mut MemoryExchange<'_>,
    line: &str,
    confirmed: bool,
) -> Result<(String, JsonValue), ConnectError> {
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
        yes: confirmed,
    };
    execute_one_with_exchange(runtime, &args, line, Some(exchange), confirmed)
}

fn set(message: &mut DynamicMessage, name: &str, value: Value) {
    let field = message.descriptor().get_field_by_name(name).unwrap();
    message.set_field(&field, value);
}

fn get(message: &DynamicMessage, name: &str) -> Value {
    let field = message.descriptor().get_field_by_name(name).unwrap();
    message.get_field(&field).into_owned()
}

fn assert_config(request: &DynamicMessage, expected: &DynamicMessage) {
    let Value::Message(actual) = get(request, "config") else {
        panic!("configuration missing from request");
    };
    // Proto3 omits default-valued fields on the wire; compare observable values.
    assert_eq!(
        super::super::dynamic_to_json(&actual),
        super::super::dynamic_to_json(expected),
    );
}

fn saved_config(runtime: &Runtime) -> DynamicMessage {
    let mut config = runtime.new_message("MeshcoreConfig").unwrap();
    // Give every schema field a non-default value, independently of the command
    // catalog. An omitted preservation field must change the outgoing request.
    for field in config.descriptor().fields() {
        let value = match field.kind() {
            Kind::Bool => Value::Bool(true),
            Kind::String => Value::String("saved node".into()),
            Kind::Bytes => Value::Bytes(
                vec![
                    0x5a;
                    if field.name() == "private_key" {
                        64
                    } else {
                        32
                    }
                ]
                .into(),
            ),
            Kind::Int32 | Kind::Sint32 | Kind::Sfixed32 => Value::I32(123_456),
            Kind::Int64 | Kind::Sint64 | Kind::Sfixed64 => Value::I64(123_456),
            Kind::Uint32 | Kind::Fixed32 => Value::U32(1),
            Kind::Uint64 | Kind::Fixed64 => Value::U64(1),
            Kind::Float => Value::F32(1.25),
            Kind::Double => Value::F64(1.25),
            Kind::Enum(enumeration) => Value::EnumNumber(
                enumeration
                    .values()
                    .find(|value| value.number() != 0)
                    .unwrap()
                    .number(),
            ),
            kind => panic!("add a non-default fixture for {}: {kind:?}", field.name()),
        };
        config.set_field(&field, value);
    }
    set(&mut config, "role", Value::EnumNumber(2));
    config
}

fn response(runtime: &Runtime, name: &str, config: &DynamicMessage) -> Vec<u8> {
    let mut message = runtime.new_message(name).unwrap();
    set(&mut message, "config", Value::Message(config.clone()));
    message.encode_to_vec()
}

#[test]
fn rename_preserves_every_persisted_field_and_returns_applied_device_state() {
    let runtime = Runtime::load().unwrap();
    let current = saved_config(&runtime);
    let mut expected = current.clone();
    set(&mut expected, "name", Value::String("new node".into()));
    let mut applied = expected.clone();
    set(&mut applied, "name", Value::String("device result".into()));
    let mut exchange = MemoryExchange::new(
        &runtime,
        vec![
            (
                "meshcore config",
                Ok(response(&runtime, "MeshcoreConfigGetResponse", &current)),
            ),
            (
                "meshcore config set",
                Ok(response(&runtime, "MeshcoreConfigSetResponse", &applied)),
            ),
        ],
    );
    let (label, result) = execute(
        &runtime,
        &mut exchange,
        "meshcore config --name 'new node'",
        false,
    )
    .unwrap();
    assert_eq!(label, "meshcore config");
    assert_eq!(result["config"]["name"], "device result");
    assert_eq!(result["config"]["role"], "meshcore_role_repeater");
    let requests = exchange.finish();
    assert!(requests[0].encode_to_vec().is_empty());
    assert_config(&requests[1], &expected);
}

#[test]
fn role_updates_preserve_every_other_persisted_field() {
    let runtime = Runtime::load().unwrap();
    for (name, number) in [("chat", 1), ("repeater", 2), ("room", 3), ("sensor", 4)] {
        let current = saved_config(&runtime);
        let mut expected = current.clone();
        set(&mut expected, "role", Value::EnumNumber(number));
        let mut exchange = MemoryExchange::new(
            &runtime,
            vec![
                (
                    "meshcore config",
                    Ok(response(&runtime, "MeshcoreConfigGetResponse", &current)),
                ),
                (
                    "meshcore config set",
                    Ok(response(&runtime, "MeshcoreConfigSetResponse", &expected)),
                ),
            ],
        );
        let (_, result) = execute(
            &runtime,
            &mut exchange,
            &format!("meshcore config --role meshcore_role_{name}"),
            false,
        )
        .unwrap();
        assert_eq!(result["config"]["role"], format!("meshcore_role_{name}"));
        assert_config(&exchange.finish()[1], &expected);
    }
}

#[test]
fn display_reads_configuration_without_writing() {
    let runtime = Runtime::load().unwrap();
    let current = saved_config(&runtime);
    let mut exchange = MemoryExchange::new(
        &runtime,
        vec![(
            "meshcore config",
            Ok(response(&runtime, "MeshcoreConfigGetResponse", &current)),
        )],
    );
    let (_, result) = execute(&runtime, &mut exchange, "meshcore config", false).unwrap();
    assert_eq!(result["config"]["name"], "saved node");
    assert_eq!(exchange.finish().len(), 1);
}

#[test]
fn failed_read_prevents_write() {
    let runtime = Runtime::load().unwrap();
    let mut exchange = MemoryExchange::new(
        &runtime,
        vec![(
            "meshcore config",
            Err(ConnectError::Transport("read failed".into())),
        )],
    );
    let error = execute(
        &runtime,
        &mut exchange,
        "meshcore config --name changed",
        false,
    )
    .unwrap_err();
    assert!(matches!(error, ConnectError::Transport(ref message) if message == "read failed"));
    assert_eq!(exchange.finish().len(), 1);
}

#[test]
fn malformed_read_response_prevents_write() {
    let runtime = Runtime::load().unwrap();
    let mut exchange = MemoryExchange::new(&runtime, vec![("meshcore config", Ok(vec![0xff]))]);
    let error = execute(
        &runtime,
        &mut exchange,
        "meshcore config --name changed",
        false,
    )
    .unwrap_err();
    assert!(matches!(error, ConnectError::Protocol(_)));
    assert_eq!(exchange.finish().len(), 1);
}

#[test]
fn failed_write_preserves_device_error_and_does_not_report_success() {
    let runtime = Runtime::load().unwrap();
    let current = saved_config(&runtime);
    let group = runtime
        .by_path(&["meshcore", "config", "set"])
        .unwrap()
        .group_id;
    let mut exchange = MemoryExchange::new(
        &runtime,
        vec![
            (
                "meshcore config",
                Ok(response(&runtime, "MeshcoreConfigGetResponse", &current)),
            ),
            (
                "meshcore config set",
                Err(ConnectError::Mcumgr {
                    code: "permission_denied",
                    message: "write denied".into(),
                    group: group.into(),
                    rc: -13,
                }),
            ),
        ],
    );
    let error = execute(
        &runtime,
        &mut exchange,
        "meshcore config --name changed",
        false,
    )
    .unwrap_err();
    assert_eq!(error.code(), "permission_denied");
    assert_eq!(error.exit_status(), 4);
    assert!(matches!(error, ConnectError::Mcumgr { rc: -13, .. }));
    assert_eq!(exchange.finish().len(), 2);
}

#[test]
fn reset_confirmation_precedes_any_device_exchange() {
    let runtime = Runtime::load().unwrap();
    for line in ["meshcore config --reset", "MESHBUS meshcore config --RESET"] {
        assert_eq!(
            confirmation_command(&runtime, line).unwrap().as_deref(),
            Some("meshcore config")
        );
        let mut exchange = MemoryExchange::new(&runtime, vec![]);
        let error = execute(&runtime, &mut exchange, line, false).unwrap_err();
        assert!(matches!(error, ConnectError::Usage(_)));
        assert!(error.to_string().contains("explicit confirmation"));
        assert!(exchange.finish().is_empty());
    }
}

#[test]
fn confirmed_reset_sends_only_reset_and_returns_device_defaults() {
    let runtime = Runtime::load().unwrap();
    let mut defaults = runtime.new_message("MeshcoreConfig").unwrap();
    set(&mut defaults, "role", Value::EnumNumber(1));
    let mut exchange = MemoryExchange::new(
        &runtime,
        vec![(
            "meshcore config reset",
            Ok(response(&runtime, "MeshcoreConfigResetResponse", &defaults)),
        )],
    );
    let (_, result) = execute(&runtime, &mut exchange, "meshcore config --RESET", true).unwrap();
    assert_eq!(result["config"]["role"], "meshcore_role_chat");
    let requests = exchange.finish();
    assert_eq!(requests.len(), 1);
    assert!(requests[0].encode_to_vec().is_empty());
}

#[test]
fn reset_cannot_be_combined_with_updates() {
    let runtime = Runtime::load().unwrap();
    for line in [
        "meshcore config --reset --name changed",
        "meshcore config --name changed --reset",
    ] {
        let mut exchange = MemoryExchange::new(&runtime, vec![]);
        let error = execute(&runtime, &mut exchange, line, true).unwrap_err();
        assert!(error.to_string().contains("cannot be combined"));
        assert!(exchange.finish().is_empty());
    }
}

#[test]
fn invalid_updates_never_write_configuration() {
    let runtime = Runtime::load().unwrap();
    let current = saved_config(&runtime);
    for (options, message) in [
        ("--unknown value", "unknown option"),
        ("--name one --NAME=two", "more than once"),
        ("--name", "requires a value"),
        ("--role unsupported", "valid enum"),
        ("name changed", "expected a --field option"),
    ] {
        let mut exchange = MemoryExchange::new(
            &runtime,
            vec![(
                "meshcore config",
                Ok(response(&runtime, "MeshcoreConfigGetResponse", &current)),
            )],
        );
        let error = execute(
            &runtime,
            &mut exchange,
            &format!("meshcore config {options}"),
            false,
        )
        .unwrap_err();
        assert!(matches!(error, ConnectError::Usage(_)));
        assert!(error.to_string().contains(message), "{error}");
        assert_eq!(exchange.finish().len(), 1);
    }
}

#[test]
fn aliased_and_case_insensitive_options_update_their_fields() {
    let runtime = Runtime::load().unwrap();
    let current = saved_config(&runtime);
    let mut expected = current.clone();
    set(&mut expected, "telemetry_mode_locat", Value::EnumNumber(2));
    set(&mut expected, "disable_fwd", Value::Bool(false));
    let mut exchange = MemoryExchange::new(
        &runtime,
        vec![
            (
                "meshcore config",
                Ok(response(&runtime, "MeshcoreConfigGetResponse", &current)),
            ),
            (
                "meshcore config set",
                Ok(response(&runtime, "MeshcoreConfigSetResponse", &expected)),
            ),
        ],
    );
    execute(
        &runtime,
        &mut exchange,
        "meshcore config --telemetry-mode-location=telemetry_allow_all --DISABLE-FWD off",
        false,
    )
    .unwrap();
    assert_config(&exchange.finish()[1], &expected);
}

#[test]
fn management_password_alias_writes_text_bytes_without_reading() {
    let runtime = Runtime::load().unwrap();
    let mut reply = runtime.new_message("ManagementSecretSetResponse").unwrap();
    set(&mut reply, "accepted", Value::Bool(true));
    let mut exchange = MemoryExchange::new(
        &runtime,
        vec![("management config set password", Ok(reply.encode_to_vec()))],
    );
    let (_, result) = execute(
        &runtime,
        &mut exchange,
        "management config --password=new-pass-123",
        false,
    )
    .unwrap();
    assert_eq!(result["accepted"], true);
    assert_eq!(
        get(&exchange.finish()[0], "secret"),
        Value::Bytes(b"new-pass-123".to_vec().into())
    );
}

#[test]
fn management_status_reports_provisioning_without_credentials() {
    let runtime = Runtime::load().unwrap();
    let mut reply = runtime.new_message("ManagementSecretGetResponse").unwrap();
    set(&mut reply, "configured", Value::Bool(true));
    set(&mut reply, "effective_max_len", Value::U32(504));
    let mut exchange = MemoryExchange::new(
        &runtime,
        vec![("management config", Ok(reply.encode_to_vec()))],
    );
    let (_, result) = execute(&runtime, &mut exchange, "management config", false).unwrap();
    assert_eq!(result, json!({"password": "Set", "effective_max_len": 504}));
    assert_eq!(exchange.finish().len(), 1);
}

#[test]
fn indicator_message_and_system_routing_are_independent() {
    let runtime = Runtime::load().unwrap();
    let mut current = runtime.new_message("IndicatorConfig").unwrap();
    set(&mut current, "light_enabled", Value::Bool(true));
    let mut buzzer = runtime
        .new_message("IndicatorConfig.BuzzerFeedback")
        .unwrap();
    set(&mut buzzer, "direct_message_enabled", Value::Bool(true));
    set(&mut current, "buzzer_feedback", Value::Message(buzzer));
    let mut light = runtime
        .new_message("IndicatorConfig.LightFeedback")
        .unwrap();
    set(&mut light, "heartbeat_enabled", Value::Bool(true));
    set(&mut light, "message_enabled", Value::Bool(true));
    set(&mut light, "system_enabled", Value::Bool(true));
    set(
        &mut current,
        "light_feedback",
        Value::Message(light.clone()),
    );
    let mut expected = current.clone();
    set(&mut light, "message_enabled", Value::Bool(false));
    set(&mut expected, "light_feedback", Value::Message(light));
    let mut exchange = MemoryExchange::new(
        &runtime,
        vec![
            (
                "indicator config",
                Ok(response(&runtime, "IndicatorConfigGetResponse", &current)),
            ),
            (
                "indicator config set",
                Ok(response(&runtime, "IndicatorConfigSetResponse", &expected)),
            ),
        ],
    );
    execute(
        &runtime,
        &mut exchange,
        "indicator config --light-feedback-message-enabled off",
        false,
    )
    .unwrap();
    assert_config(&exchange.finish()[1], &expected);
}
