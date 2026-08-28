// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

pub mod cli;
pub mod commands;
pub mod device;
pub mod probe;

pub use cli::run;

pub fn error_exit_status(error: &(dyn std::error::Error + 'static)) -> i32 {
    error
        .downcast_ref::<commands::connect::ConnectError>()
        .map_or(2, commands::connect::ConnectError::exit_status)
}

pub fn error_was_reported(error: &(dyn std::error::Error + 'static)) -> bool {
    error
        .downcast_ref::<commands::connect::ConnectError>()
        .is_some_and(commands::connect::ConnectError::is_reported)
}

pub fn error_message(error: &(dyn std::error::Error + 'static)) -> String {
    error
        .downcast_ref::<commands::connect::ConnectError>()
        .map_or_else(
            || error.to_string(),
            commands::connect::ConnectError::safe_message,
        )
}
