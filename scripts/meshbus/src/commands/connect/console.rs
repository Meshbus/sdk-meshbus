// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

use std::borrow::Cow;
use std::collections::HashSet;
use std::env;
use std::fs::{self, OpenOptions};
use std::io::{self, IsTerminal};
use std::path::PathBuf;

use rustyline::completion::{Completer, Pair};
use rustyline::error::ReadlineError;
use rustyline::highlight::Highlighter;
use rustyline::hint::Hinter;
use rustyline::history::DefaultHistory;
use rustyline::validate::Validator;
use rustyline::{CompletionType, Config, Context, Editor, ExternalPrinter as _, Helper};
use serde_json::{Map as JsonMap, Value as JsonValue, json};

use super::{
    CommandSpec, ConnectArgs, ConnectColor, ConnectError, Runtime, SerialSession, config_arguments,
    confirmation_command, execute_one_with_serial,
};

const ANSI_BOLD_CYAN: &str = "1;36";
const ANSI_CYAN: &str = "36";
const ANSI_GREEN: &str = "32";
const ANSI_YELLOW: &str = "33";
const ANSI_BOLD_RED: &str = "1;31";
const ANSI_GRAY: &str = "90";

pub(super) fn run_interactive(runtime: &Runtime, args: &ConnectArgs) -> Result<(), ConnectError> {
    let color = color_enabled(args.color, true);
    let config = Config::builder()
        .history_ignore_space(true)
        .completion_type(CompletionType::List)
        .auto_add_history(false)
        .build();
    let mut editor = Editor::<MeshbusHelper<'_>, DefaultHistory>::with_config(config)
        .map_err(|error| ConnectError::Transport(format!("cannot initialize console: {error}")))?;
    editor.set_helper(Some(MeshbusHelper { runtime, color }));

    let history_path = if args.no_history {
        None
    } else {
        prepare_history_path()
    };
    if let Some(path) = &history_path {
        let _ = editor.load_history(path);
    }

    let mut serial = SerialSession::open(args)?;
    if args.quiet_logs {
        serial.start_background(args, |_| Ok(()))?;
    } else {
        match editor.create_external_printer() {
            Ok(mut printer) => serial.start_background(args, move |message| {
                printer
                    .print(message)
                    .map_err(|error| format!("cannot display device log: {error}"))
            })?,
            Err(_) => serial.start_background(args, |message| {
                eprintln!("{message}");
                Ok(())
            })?,
        }
    }

    eprintln!(
        "{}",
        paint(
            &format!(
                "Meshbus UART MCUmgr connected to {} at {} baud. Type 'help' for commands; Tab completes paths and values; Ctrl-C clears input; Ctrl-D exits.",
                args.port, args.baudrate
            ),
            ANSI_CYAN,
            color_enabled(args.color, false)
        )
    );

    loop {
        let prompt = if color {
            ("meshbus> ", "\x1b[1;36mmeshbus> \x1b[0m")
        } else {
            ("meshbus> ", "meshbus> ")
        };
        let command_line = match editor.readline(&prompt) {
            Ok(line) => line.trim().to_owned(),
            Err(ReadlineError::Interrupted) => continue,
            Err(ReadlineError::Eof) => break,
            Err(error) => {
                return Err(ConnectError::Transport(format!(
                    "interactive input failed: {error}"
                )));
            }
        };
        if command_line.is_empty() {
            continue;
        }

        if let Some(path) = &history_path
            && !command_is_sensitive(runtime, &command_line)
        {
            let _ = editor.add_history_entry(command_line.as_str());
            if let Err(error) = editor.append_history(path) {
                eprintln!("meshbus: warning: cannot save command history: {error}");
            }
        }

        if matches!(command_line.to_ascii_lowercase().as_str(), "exit" | "quit") {
            break;
        }

        let mut allow_dangerous = false;
        if let Some(command) = confirmation_command(runtime, &command_line)? {
            let plain = format!("Confirm '{command}' [y/N]: ");
            let colored = paint(&plain, ANSI_YELLOW, color);
            let answer = match editor.readline(&(plain.as_str(), colored.as_str())) {
                Ok(answer) => answer,
                Err(ReadlineError::Interrupted) => String::new(),
                Err(ReadlineError::Eof) => break,
                Err(error) => {
                    return Err(ConnectError::Transport(format!(
                        "confirmation input failed: {error}"
                    )));
                }
            };
            allow_dangerous = matches!(answer.trim().to_ascii_lowercase().as_str(), "y" | "yes");
            if !allow_dangerous {
                eprintln!("{}", paint("cancelled", ANSI_YELLOW, color));
                continue;
            }
        }

        match execute_one_with_serial(
            runtime,
            args,
            &command_line,
            Some(&mut serial),
            allow_dangerous,
        ) {
            Ok((label, _)) if label == "exit" => break,
            Ok((label, value)) => write_human(&label, &value, args.color),
            Err(error) => write_human_error(&error, args.color),
        }
    }
    Ok(())
}

pub(super) fn write_json_success(command: &str, result: &JsonValue) {
    println!(
        "{}",
        json!({"command": command, "ok": true, "result": result})
    );
}

pub(super) fn write_json_error(command: &str, error: &ConnectError) {
    let document = json_error_document(error);
    println!(
        "{}",
        json!({"command": command, "ok": false, "error": document})
    );
}

fn json_error_document(error: &ConnectError) -> JsonMap<String, JsonValue> {
    let mut document = JsonMap::new();
    document.insert("code".into(), JsonValue::String(error.code().into()));
    document.insert("message".into(), JsonValue::String(error.to_string()));
    match error {
        ConnectError::Mcumgr { group, rc, .. } => {
            document.insert("group".into(), json!(group));
            document.insert("rc".into(), json!(rc));
        }
        ConnectError::RemotePasswordContactUpdate {
            contact_prefix,
            local_error,
        } => {
            document.insert(
                "contact_prefix".into(),
                JsonValue::String(contact_prefix.clone()),
            );
            document.insert("local_error".into(), JsonValue::String(local_error.clone()));
        }
        ConnectError::RemoteFailure { status } => {
            document.insert("status".into(), json!(status));
        }
        ConnectError::RemoteTimeout {
            status: Some(status),
            ..
        } => {
            document.insert("status".into(), json!(status));
        }
        _ => {}
    }
    document
}

pub(super) fn write_human(command: &str, result: &JsonValue, color: ConnectColor) {
    let enabled = color_enabled(color, true);
    if command == "help" {
        let lines = result
            .get("lines")
            .and_then(JsonValue::as_array)
            .into_iter()
            .flatten()
            .filter_map(JsonValue::as_str);
        println!("{}", paint("Commands:", ANSI_BOLD_CYAN, enabled));
        for line in lines {
            println!("{line}");
        }
        println!("  help [path]                              Show help");
        println!("  exit                                     Close session");
        return;
    }
    if command == "exit" {
        return;
    }
    let mut lines = vec![paint(command, ANSI_BOLD_CYAN, enabled)];
    render_value(&mut lines, result, 2, None, &[], command, enabled);
    println!("{}", lines.join("\n"));
}

fn write_human_error(error: &ConnectError, color: ConnectColor) {
    let message = format!("error: {}", escape_terminal(&error.to_string()));
    eprintln!(
        "{}",
        paint(&message, ANSI_BOLD_RED, color_enabled(color, false))
    );
}

pub(super) fn command_output_label(runtime: &Runtime, command_line: &str) -> String {
    if !command_is_sensitive(runtime, command_line) {
        return command_line.into();
    }
    let words = split_partial(command_line);
    runtime
        .resolve(&words)
        .map(|(command, _)| command.path.join(" "))
        .unwrap_or_else(|_| "sensitive command".into())
}

fn command_is_sensitive(runtime: &Runtime, command_line: &str) -> bool {
    let words = split_partial(command_line);
    if words.is_empty() {
        return false;
    }
    let normalized = normalized_words(&words);
    if runtime.registry.commands.iter().any(|command| {
        command.internal && command.sensitive && normalized.starts_with(&command.path)
    }) {
        return true;
    }
    let (command, arguments) = match runtime.resolve(&words) {
        Ok(resolved) => resolved,
        Err(_) => {
            return runtime
                .registry
                .commands
                .iter()
                .any(|command| command.sensitive && normalized.starts_with(&command.path));
        }
    };
    if command.sensitive {
        return true;
    }
    if command.custom.as_deref() == Some("config") {
        let sensitive = config_options(runtime, command)
            .into_iter()
            .filter(|option| option.sensitive)
            .map(|option| option.flag)
            .collect::<HashSet<_>>();
        if arguments.iter().any(|argument| {
            let flag = argument
                .split_once('=')
                .map_or(argument.as_str(), |(flag, _)| flag)
                .to_ascii_lowercase();
            sensitive.contains(&flag)
        }) {
            return true;
        }
    }
    command.path == ["contact", "set"]
        && arguments.len() >= 2
        && arguments[1].eq_ignore_ascii_case("management_password")
}

struct MeshbusHelper<'a> {
    runtime: &'a Runtime,
    color: bool,
}

impl Completer for MeshbusHelper<'_> {
    type Candidate = Pair;

    fn complete(
        &self,
        line: &str,
        position: usize,
        _context: &Context<'_>,
    ) -> rustyline::Result<(usize, Vec<Self::Candidate>)> {
        let (start, candidates) = complete_line(self.runtime, line, position);
        Ok((
            start,
            candidates
                .into_iter()
                .map(|candidate| Pair {
                    display: candidate.clone(),
                    replacement: candidate,
                })
                .collect(),
        ))
    }
}

impl Hinter for MeshbusHelper<'_> {
    type Hint = String;
}

impl Highlighter for MeshbusHelper<'_> {
    fn highlight_prompt<'b, 's: 'b, 'p: 'b>(
        &'s self,
        prompt: &'p str,
        _default: bool,
    ) -> Cow<'b, str> {
        if self.color {
            Cow::Owned(paint(prompt, ANSI_BOLD_CYAN, true))
        } else {
            Cow::Borrowed(prompt)
        }
    }

    fn highlight_candidate<'c>(
        &self,
        candidate: &'c str,
        _completion: CompletionType,
    ) -> Cow<'c, str> {
        if self.color {
            Cow::Owned(paint(candidate, ANSI_CYAN, true))
        } else {
            Cow::Borrowed(candidate)
        }
    }
}

impl Validator for MeshbusHelper<'_> {}
impl Helper for MeshbusHelper<'_> {}

fn complete_line(runtime: &Runtime, line: &str, position: usize) -> (usize, Vec<String>) {
    let text = &line[..position];
    let tokens = split_partial(text);
    let trailing = text.chars().last().is_some_and(char::is_whitespace);
    let current = if trailing {
        ""
    } else {
        tokens.last().map(String::as_str).unwrap_or("")
    };
    let start = position.saturating_sub(current.len());
    let completed = if trailing || tokens.is_empty() {
        &tokens[..]
    } else {
        &tokens[..tokens.len() - 1]
    };
    let mut normalized = normalized_words(completed);
    let help_mode = normalized
        .first()
        .is_some_and(|word| matches!(word.as_str(), "help" | "?"));
    if help_mode {
        normalized.remove(0);
    }

    let mut candidates = Vec::new();
    if normalized.is_empty() {
        candidates.extend(
            runtime
                .registry
                .commands
                .iter()
                .filter(|command| !command.internal)
                .map(|command| command.path[0].clone()),
        );
        if !help_mode {
            candidates.extend(["help".into(), "exit".into()]);
        }
        if !help_mode {
            return (start, filter_candidates(candidates, current));
        }
    }

    for command in runtime
        .registry
        .commands
        .iter()
        .filter(|command| !command.internal)
    {
        if normalized.len() < command.path.len() && normalized == command.path[..normalized.len()] {
            candidates.push(command.path[normalized.len()].clone());
        }
    }
    if help_mode {
        return (start, filter_candidates(candidates, current));
    }

    let mut command_words = tokens.clone();
    if command_words
        .first()
        .is_some_and(|word| word.eq_ignore_ascii_case("meshbus"))
    {
        command_words.remove(0);
    }
    let all_words = command_words
        .iter()
        .map(|word| word.to_ascii_lowercase())
        .collect::<Vec<_>>();
    for command in runtime
        .registry
        .commands
        .iter()
        .filter(|command| !command.internal)
    {
        if all_words.get(..command.path.len()) != Some(command.path.as_slice()) {
            continue;
        }
        if command.custom.as_deref() == Some("config") {
            return complete_config(
                runtime,
                command,
                &command_words[command.path.len()..],
                trailing,
                start,
                position,
            );
        }
        let mut argument_index = all_words.len() as isize - command.path.len() as isize;
        if !trailing {
            argument_index -= 1;
        }
        let choices: Vec<String> = if command.custom.as_deref() == Some("contact_set")
            && argument_index == 1
        {
            command.completion_choices.clone()
        } else if command.custom.as_deref() == Some("contact_set")
            && argument_index == 2
            && command_words
                .get(command.path.len() + 1)
                .is_some_and(|word| word.eq_ignore_ascii_case("management_password"))
        {
            vec!["--clear".into()]
        } else if matches!(
            command.custom.as_deref(),
            Some("input_inject_act" | "input_inject_raw")
        ) {
            match argument_index {
                0 => vec!["key".into(), "rel".into(), "abs".into(), "msc".into()],
                2 if command.custom.as_deref() == Some("input_inject_act") => vec![
                    "short".into(),
                    "long".into(),
                    "cw".into(),
                    "ccw".into(),
                    "scroll_cw".into(),
                    "scroll_ccw".into(),
                ],
                _ => Vec::new(),
            }
        } else if command.custom.as_deref() == Some("message_send_node") && argument_index == 3 {
            vec!["on".into(), "off".into()]
        } else if matches!(
            command.custom.as_deref(),
            Some("management_remote_raw" | "management_remote_raw_password")
        ) && argument_index
            == if command.custom.as_deref() == Some("management_remote_raw_password") {
                4
            } else {
                3
            }
        {
            command.completion_choices.clone()
        } else if argument_index >= 0 {
            command
                .arguments
                .get(argument_index as usize)
                .map(|argument| argument.choices.clone())
                .unwrap_or_default()
        } else {
            Vec::new()
        };
        candidates.extend(choices);
        break;
    }
    (start, filter_candidates(candidates, current))
}

fn complete_config(
    runtime: &Runtime,
    command: &CommandSpec,
    words: &[String],
    trailing: bool,
    start: usize,
    position: usize,
) -> (usize, Vec<String>) {
    let options = config_options(runtime, command);
    let current = if trailing {
        ""
    } else {
        words.last().map(String::as_str).unwrap_or("")
    };
    if let Some((flag, partial)) = current.split_once('=')
        && let Some(option) = options
            .iter()
            .find(|option| option.flag.eq_ignore_ascii_case(flag))
    {
        return (
            position.saturating_sub(partial.len()),
            filter_candidates(option.choices.clone(), partial),
        );
    }

    let completed = if trailing || words.is_empty() {
        words
    } else {
        &words[..words.len() - 1]
    };
    let mut used = HashSet::new();
    let mut pending = None;
    let mut index = 0;
    while index < completed.len() {
        let token = &completed[index];
        let (flag, has_value) = token
            .split_once('=')
            .map_or((token.as_str(), false), |(flag, _)| (flag, true));
        let normalized = flag.to_ascii_lowercase();
        if normalized == "--reset" {
            used.insert(normalized);
            index += 1;
            continue;
        }
        let Some(option_index) = options.iter().position(|option| option.flag == normalized) else {
            index += 1;
            continue;
        };
        used.insert(normalized);
        if has_value {
            index += 1;
        } else if index + 1 < completed.len() {
            index += 2;
        } else {
            pending = Some(option_index);
            index += 1;
        }
    }
    if let Some(index) = pending {
        return (
            start,
            filter_candidates(options[index].choices.clone(), current),
        );
    }
    if used.contains("--reset") {
        return (start, Vec::new());
    }
    let mut candidates = options
        .iter()
        .filter(|option| !used.contains(&option.flag))
        .map(|option| option.flag.clone())
        .collect::<Vec<_>>();
    candidates.push("--reset".into());
    (start, filter_candidates(candidates, current))
}

struct ConfigOption {
    flag: String,
    choices: Vec<String>,
    sensitive: bool,
}

fn config_options(runtime: &Runtime, command: &CommandSpec) -> Vec<ConfigOption> {
    let set_path = command.config_set_path.clone().unwrap_or_else(|| {
        let mut path = command.path.clone();
        path.push("set".into());
        path
    });
    let Ok(set) = runtime.by_string_path(&set_path) else {
        return Vec::new();
    };
    let sensitive = command
        .config_sensitive_paths
        .iter()
        .map(String::as_str)
        .collect::<HashSet<_>>();
    config_arguments(command, set)
        .into_iter()
        .map(|argument| {
            let key = argument.path.join(".");
            let flag = command
                .config_option_aliases
                .get(&key)
                .and_then(JsonValue::as_str)
                .map(str::to_owned)
                .unwrap_or_else(|| {
                    let relative = if argument.path.first().is_some_and(|part| part == "config") {
                        &argument.path[1..]
                    } else {
                        &argument.path[..]
                    };
                    format!("--{}", relative.join("-").replace('_', "-"))
                });
            ConfigOption {
                flag: flag.to_ascii_lowercase(),
                choices: argument.choices.clone(),
                sensitive: sensitive.contains(key.as_str()),
            }
        })
        .collect()
}

fn split_partial(line: &str) -> Vec<String> {
    shell_words::split(line).unwrap_or_else(|_| {
        line.split_whitespace()
            .map(str::to_owned)
            .collect::<Vec<_>>()
    })
}

fn normalized_words(words: &[String]) -> Vec<String> {
    let words = if words
        .first()
        .is_some_and(|word| word.eq_ignore_ascii_case("meshbus"))
    {
        &words[1..]
    } else {
        words
    };
    words.iter().map(|word| word.to_ascii_lowercase()).collect()
}

fn filter_candidates(mut candidates: Vec<String>, current: &str) -> Vec<String> {
    let current = current.to_ascii_lowercase();
    candidates.retain(|candidate| candidate.to_ascii_lowercase().starts_with(&current));
    candidates.sort();
    candidates.dedup();
    candidates
}

fn prepare_history_path() -> Option<PathBuf> {
    let base = env::var_os("XDG_STATE_HOME")
        .filter(|value| !value.is_empty())
        .map(PathBuf::from)
        .or_else(|| {
            env::var_os("HOME")
                .or_else(|| env::var_os("USERPROFILE"))
                .map(|home| PathBuf::from(home).join(".local/state"))
        })?;
    let path = base.join("meshbus/history");
    if fs::create_dir_all(path.parent()?).is_err() {
        return None;
    }
    if OpenOptions::new()
        .create(true)
        .append(true)
        .open(&path)
        .is_err()
    {
        return None;
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = fs::set_permissions(path.parent()?, fs::Permissions::from_mode(0o700));
        let _ = fs::set_permissions(&path, fs::Permissions::from_mode(0o600));
    }
    Some(path)
}

pub(super) fn colorize_device_log(text: &str, color: ConnectColor) -> String {
    if text.contains("\x1b[") {
        return text.into();
    }
    let style = if text.contains("<err>") {
        ANSI_BOLD_RED
    } else if text.contains("<wrn>") {
        ANSI_YELLOW
    } else if text.contains("<inf>") {
        ANSI_CYAN
    } else if text.contains("<dbg>") {
        ANSI_GRAY
    } else {
        ""
    };
    paint(text, style, !style.is_empty() && color_enabled(color, true))
}

fn color_enabled(color: ConnectColor, stdout: bool) -> bool {
    match color {
        ConnectColor::Always => true,
        ConnectColor::Never => false,
        ConnectColor::Auto => {
            if env::var_os("NO_COLOR").is_some()
                || env::var("TERM").is_ok_and(|term| term.eq_ignore_ascii_case("dumb"))
            {
                false
            } else if stdout {
                io::stdout().is_terminal()
            } else {
                io::stderr().is_terminal()
            }
        }
    }
}

fn paint(text: &str, style: &str, enabled: bool) -> String {
    if enabled && !style.is_empty() {
        format!("\x1b[{style}m{text}\x1b[0m")
    } else {
        text.into()
    }
}

fn escape_terminal(value: &str) -> String {
    value
        .chars()
        .map(|character| match character {
            '\n' => "\\n".into(),
            '\r' => "\\r".into(),
            '\t' => "\\t".into(),
            character if character.is_control() => format!("\\x{:02x}", character as u32),
            character => character.to_string(),
        })
        .collect()
}

fn render_value(
    lines: &mut Vec<String>,
    value: &JsonValue,
    mut indent: usize,
    key: Option<&str>,
    path: &[String],
    command: &str,
    color: bool,
) {
    let prefix = " ".repeat(indent);
    let mut current_path = path.to_vec();
    if let Some(key) = key {
        current_path.push(key.into());
    }
    match value {
        JsonValue::Object(values) => {
            if let Some(key) = key {
                lines.push(format!(
                    "{prefix}{}",
                    paint(&format!("{}:", label(key)), ANSI_CYAN, color)
                ));
                indent += 2;
            }
            for (child_key, child) in values {
                render_value(
                    lines,
                    child,
                    indent,
                    Some(child_key),
                    &current_path,
                    command,
                    color,
                );
            }
        }
        JsonValue::Array(values) => {
            lines.push(format!(
                "{prefix}{}",
                paint(
                    &format!("{}:", label(key.unwrap_or("items"))),
                    ANSI_CYAN,
                    color
                )
            ));
            for item in values {
                if item.is_object() {
                    lines.push(format!("{prefix}  -"));
                    render_value(lines, item, indent + 4, None, &current_path, command, color);
                } else {
                    lines.push(format!(
                        "{prefix}  - {}",
                        display_value(item, command, &current_path)
                    ));
                }
            }
        }
        _ => {
            let label = paint(
                &format!("{}:", label(key.unwrap_or("value"))),
                ANSI_CYAN,
                color,
            );
            let display = display_value(value, command, &current_path);
            let display = match value {
                JsonValue::Bool(true) => paint(&display, ANSI_GREEN, color),
                JsonValue::Bool(false) | JsonValue::Null => paint(&display, ANSI_GRAY, color),
                _ => display,
            };
            lines.push(format!("{prefix}{label} {display}"));
        }
    }
}

fn label(value: &str) -> String {
    if value == "management_secret" {
        "Management password".into()
    } else {
        let mut value = value.replace('_', " ");
        if let Some(first) = value.get_mut(..1) {
            first.make_ascii_uppercase();
        }
        value
    }
}

fn display_value(value: &JsonValue, command: &str, path: &[String]) -> String {
    match value {
        JsonValue::Bool(true) => "On".into(),
        JsonValue::Bool(false) => "Off".into(),
        JsonValue::Null => "Unavailable".into(),
        JsonValue::Number(number)
            if matches!(command, "bluetooth config" | "bluetooth status")
                && path == ["config", "fixed_passkey"]
                && number.as_u64().is_some_and(|value| value <= 999_999) =>
        {
            format!("{:06}", number.as_u64().unwrap())
        }
        JsonValue::String(value) => escape_terminal(value),
        other => escape_terminal(&other.to_string()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn completion_covers_paths_config_values_and_custom_values() {
        let runtime = Runtime::load().unwrap();
        assert_eq!(complete_line(&runtime, "rad", 3).1, ["radio"]);
        assert_eq!(complete_line(&runtime, "radio st", 8).1, ["status"]);
        assert!(
            complete_line(&runtime, "radio config --", 15)
                .1
                .contains(&"--frequency".into())
        );
        assert_eq!(
            complete_line(&runtime, "radio config --enabled o", 24).1,
            ["off", "on"]
        );
        assert_eq!(
            complete_line(&runtime, "contact set 01020304 man", 24).1,
            ["management_password"]
        );
        assert_eq!(complete_line(&runtime, "RAD", 3).1, ["radio"]);
        assert_eq!(
            complete_line(&runtime, "radio config --enabled=o", 24).1,
            ["off", "on"]
        );
    }

    #[test]
    fn sensitive_commands_are_not_history_safe() {
        let runtime = Runtime::load().unwrap();
        assert!(command_is_sensitive(
            &runtime,
            "management config --password new-pass-123"
        ));
        assert!(command_is_sensitive(
            &runtime,
            "contact set 01020304 management_password new-pass-123"
        ));
        assert!(command_is_sensitive(
            &runtime,
            "meshcore config --private-key 001122"
        ));
        assert!(!command_is_sensitive(&runtime, "radio status"));
        assert!(command_is_sensitive(
            &runtime,
            "MANAGEMENT CONFIG --PASSWORD new-pass-123"
        ));
        assert_eq!(
            command_output_label(&runtime, "management config --password new-pass-123"),
            "management config"
        );
    }

    #[test]
    fn terminal_escape_blocks_control_sequences() {
        assert_eq!(escape_terminal("ok\x1b[31m"), "ok\\x1b[31m");
    }

    #[test]
    fn remote_errors_preserve_machine_readable_recovery_details() {
        let failed = json_error_document(&ConnectError::RemoteFailure { status: -22 });
        assert_eq!(failed["code"], "remote_failed");
        assert_eq!(failed["status"], -22);

        let timeout = json_error_document(&ConnectError::RemoteTimeout {
            message: "remote management exchange timed out".into(),
            status: Some(-110),
        });
        assert_eq!(timeout["code"], "remote_timeout");
        assert_eq!(timeout["status"], -110);

        let partial = json_error_document(&ConnectError::RemotePasswordContactUpdate {
            contact_prefix: "01020304".into(),
            local_error: "mcumgr_error".into(),
        });
        assert_eq!(partial["code"], "remote_password_updated_contact_failed");
        assert_eq!(partial["contact_prefix"], "01020304");
        assert_eq!(partial["local_error"], "mcumgr_error");
    }
}
