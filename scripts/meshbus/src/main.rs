// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 FoBE Studio

fn main() {
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
