// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

fn main() {
    if let Some(result) = meshbus_cli::llext::internal_tool() {
        match result {
            Ok(code) => std::process::exit(code),
            Err(e) => {
                eprintln!("meshbus: {e}");
                std::process::exit(2);
            }
        }
    }
    if let Err(error) = meshbus_cli::run() {
        let status = meshbus_cli::error_exit_status(error.as_ref());
        if !meshbus_cli::error_was_reported(error.as_ref()) {
            eprintln!(
                "meshbus: error: {}",
                meshbus_cli::error_message(error.as_ref())
            );
        }
        std::process::exit(status);
    }
}
