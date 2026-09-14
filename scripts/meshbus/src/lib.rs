// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

pub mod cli;
pub mod commands;
pub mod device;
pub mod probe;

pub use cli::run;

/// Preserve the former west/Python host-tool failure status.
#[derive(Debug, thiserror::Error)]
#[error("{0}")]
pub struct HostError(pub anyhow::Error);

pub fn error_exit_status(error: &(dyn std::error::Error + 'static)) -> i32 {
    if error.is::<HostError>()
        || matches!(
            error.downcast_ref::<commands::firmware::FirmwareError>(),
            Some(commands::firmware::FirmwareError::Package(_))
        )
    {
        return 1;
    }
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

pub mod delta;
pub mod host;
pub mod package;

pub mod archive;
pub mod edk;
pub mod elf;
pub mod llext;
pub mod metadata;

pub mod app;
