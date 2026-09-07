// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

//! Configuration interpretation shared by execution, completion and history.
//! The update transaction owns preservation of untouched fields; callers never
//! construct replacement requests or interpret catalog option aliases.

use super::{
    CommandArgument, CommandExchange, CommandSpec, ConnectArgs, ConnectError, DynamicMessage,
    JsonValue, Message, Runtime, Value, assign_path, bool_field, dynamic_to_json, integer_field,
    invoke, invoke_json, json, parse_argument, parse_management_password, read_path,
};

pub(super) struct ConfigOption {
    pub(super) flag: String,
    pub(super) choices: Vec<String>,
    path: Vec<String>,
    sensitive: bool,
    text: bool,
}

pub(super) struct Configuration<'a> {
    runtime: &'a Runtime,
    command: &'a CommandSpec,
    set: &'a CommandSpec,
    options: Vec<ConfigOption>,
}

impl<'a> Configuration<'a> {
    pub(super) fn load(
        runtime: &'a Runtime,
        command: &'a CommandSpec,
    ) -> Result<Self, ConnectError> {
        let set_path = command.config_set_path.clone().unwrap_or_else(|| {
            let mut path = command.path.clone();
            path.push("set".into());
            path
        });
        let set = runtime.by_string_path(&set_path)?;
        let arguments = if set.arguments.is_empty() {
            command
                .config_text_paths
                .iter()
                .map(|path| CommandArgument {
                    path: path.split('.').map(str::to_owned).collect(),
                    optional: false,
                    choices: Vec::new(),
                })
                .collect()
        } else {
            set.arguments.clone()
        };
        let options = arguments
            .into_iter()
            .map(|argument| {
                let key = argument.path.join(".");
                let flag = command
                    .config_option_aliases
                    .get(&key)
                    .and_then(JsonValue::as_str)
                    .map(str::to_owned)
                    .unwrap_or_else(|| {
                        let relative = if argument.path.first().is_some_and(|part| part == "config")
                        {
                            &argument.path[1..]
                        } else {
                            &argument.path[..]
                        };
                        format!("--{}", relative.join("-").replace('_', "-"))
                    });
                ConfigOption {
                    flag: flag.to_ascii_lowercase(),
                    choices: argument.choices,
                    path: argument.path,
                    sensitive: command.config_sensitive_paths.contains(&key),
                    text: command.config_text_paths.contains(&key),
                }
            })
            .collect();
        Ok(Self {
            runtime,
            command,
            set,
            options,
        })
    }

    pub(super) fn options(&self) -> &[ConfigOption] {
        &self.options
    }

    pub(super) fn is_sensitive(&self, words: &[String]) -> bool {
        words.iter().any(|word| {
            let flag = word.split_once('=').map_or(word.as_str(), |(flag, _)| flag);
            self.options
                .iter()
                .any(|option| option.sensitive && option.flag.eq_ignore_ascii_case(flag))
        })
    }

    pub(super) fn execute(
        &self,
        args: &ConnectArgs,
        exchange: &mut dyn CommandExchange,
        words: &[String],
    ) -> Result<JsonValue, ConnectError> {
        let runtime = self.runtime;
        let command = self.command;
        if words.is_empty() {
            let request = runtime.new_message(&command.request)?;
            let response = invoke(runtime, args, exchange, command, &request.encode_to_vec())?;
            if command.path == ["management", "config"] {
                let configured = bool_field(&response, "configured")?;
                let maximum = integer_field(&response, "effective_max_len")?;
                return Ok(json!({
                    "password": if configured { "Set" } else { "Not set" },
                    "effective_max_len": maximum,
                }));
            }
            return Ok(dynamic_to_json(&response));
        }

        if words
            .iter()
            .any(|word| word.eq_ignore_ascii_case("--reset"))
        {
            if words.len() != 1 || !words[0].eq_ignore_ascii_case("--reset") {
                return Err(ConnectError::Usage(
                    "--reset cannot be combined with configuration fields".into(),
                ));
            }
            let mut reset_path = command.path.clone();
            reset_path.push("reset".into());
            let reset = runtime.by_string_path(&reset_path)?;
            let request = runtime.new_message(&reset.request)?;
            return invoke_json(runtime, args, exchange, reset, &request.encode_to_vec());
        }

        let mut request = runtime.new_message(&self.set.request)?;
        if command.path != ["management", "config"] {
            let current_request = runtime.new_message(&command.request)?;
            let current = invoke(
                runtime,
                args,
                exchange,
                command,
                &current_request.encode_to_vec(),
            )?;
            for option in &self.options {
                let value = read_path(&current, &option.path)?;
                assign_path(&mut request, &option.path, value)?;
            }
        }
        for (path, value) in self.parse_updates(&request, words)? {
            assign_path(&mut request, &path, value)?;
        }
        invoke_json(runtime, args, exchange, self.set, &request.encode_to_vec())
    }

    fn parse_updates(
        &self,
        request: &DynamicMessage,
        words: &[String],
    ) -> Result<Vec<(Vec<String>, Value)>, ConnectError> {
        let mut updates = Vec::new();
        let mut seen = std::collections::HashSet::new();
        let mut index = 0;
        while index < words.len() {
            let token = &words[index];
            if !token.starts_with("--") {
                return Err(ConnectError::Usage(format!(
                    "expected a --field option, got {token:?}; usage: {}",
                    self.command.usage
                )));
            }
            let (flag, raw_value, consumed) = if let Some((flag, value)) = token.split_once('=') {
                (flag, value, 1)
            } else {
                let value = words.get(index + 1).ok_or_else(|| {
                    ConnectError::Usage(format!(
                        "{token} requires a value; usage: {}",
                        self.command.usage
                    ))
                })?;
                (token.as_str(), value.as_str(), 2)
            };
            index += consumed;
            let normalized = flag.to_ascii_lowercase();
            let option = self
                .options
                .iter()
                .find(|option| option.flag == normalized)
                .ok_or_else(|| {
                    let mut names = self
                        .options
                        .iter()
                        .map(|option| option.flag.as_str())
                        .collect::<Vec<_>>();
                    names.sort();
                    ConnectError::Usage(format!(
                        "unknown option {flag}; expected one of: {}",
                        names.join(", ")
                    ))
                })?;
            if !seen.insert(normalized) {
                return Err(ConnectError::Usage(format!(
                    "option {flag} was provided more than once"
                )));
            }
            let path = &option.path;
            let value = if option.text {
                if path.len() == 1 && path[0] == "secret" {
                    Value::Bytes(parse_management_password(raw_value)?.into())
                } else {
                    Value::Bytes(raw_value.as_bytes().to_vec().into())
                }
            } else {
                parse_argument(request, path, raw_value)?
            };
            updates.push((path.clone(), value));
        }
        Ok(updates)
    }
}

#[cfg(test)]
mod tests;
