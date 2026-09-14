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
    /// Build an extension against a released EDK or a local host build.
    Llext(crate::llext::LlextArgs),
    /// Export, verify or qualify a public extension development kit.
    Edk(crate::edk::EdkArgs),
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
        Command::Llext(args) => crate::llext::run(args).map_err(crate::HostError)?,
        Command::Edk(args) => crate::edk::run(args).map_err(crate::HostError)?,
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

    #[test]
    fn llext_accepts_legacy_cmake_arguments() {
        for suffix in [vec!["-DFEATURE=ON"], vec!["--", "-DFEATURE=ON"]] {
            let mut args = vec!["meshbus", "llext", "-d", "host", "extension"];
            args.extend(suffix);
            let cli = Cli::try_parse_from(args).unwrap();
            let Command::Llext(args) = cli.command else {
                panic!("wrong command")
            };
            let request = crate::llext::BuildRequest::from(args);
            assert_eq!(request.cmake_args, ["-DFEATURE=ON"]);
            assert_eq!(
                request.build_dir.as_deref(),
                Some(std::path::Path::new("host"))
            );
            assert_eq!(request.source_dir, std::path::Path::new("extension"));
            assert!(request.output_dir.is_none());
            assert!(request.llext_sdk.is_none());
            assert!(request.zephyr_sdk.is_none());
            assert!(!request.force_edk);
        }
    }
}
