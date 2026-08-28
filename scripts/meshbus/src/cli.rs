// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

use clap::{Parser, Subcommand};

use crate::commands::connect::{ConnectArgs, run_connect};
use crate::commands::firmware::{FirmwareArgs, run_firmware};

#[derive(Debug, Parser)]
#[command(name = "meshbus", version, about = "Meshbus command-line utilities")]
pub struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Debug, Subcommand)]
enum Command {
    /// Connect to Meshbus UART MCUmgr while preserving device logs.
    Connect(ConnectArgs),
    /// Program, update, and inspect Meshbus firmware.
    Firmware(FirmwareArgs),
}

pub fn run() -> Result<(), Box<dyn std::error::Error>> {
    run_from(Cli::parse())
}

fn run_from(cli: Cli) -> Result<(), Box<dyn std::error::Error>> {
    match cli.command {
        Command::Connect(args) => run_connect(args)?,
        Command::Firmware(args) => run_firmware(args)?,
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use clap::CommandFactory;

    #[test]
    fn cli_definition_is_valid() {
        Cli::command().debug_assert();
    }

    #[test]
    fn firmware_requires_exact_device_name() {
        assert!(
            Cli::try_parse_from([
                "meshbus", "firmware", "inspect", "--device", "c2", "--app", "app.bin",
            ])
            .is_err()
        );
    }
}
