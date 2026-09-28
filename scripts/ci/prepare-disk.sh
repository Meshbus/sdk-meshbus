#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
# Never use this cleanup on a developer machine or a persistent runner.
test "${RUNNER_ENVIRONMENT:-}" = github-hosted
test "${RUNNER_OS:-}" = Linux
df -h .
case "${1:-}" in
    host)
        sudo rm -rf /usr/local/lib/android /usr/share/dotnet /opt/ghc \
            /usr/local/.ghcup /opt/hostedtoolcache/CodeQL
        ;;
    container)
        # Only these unused host SDK directories are bound into the job.
        # Keep the mount points themselves; remove their contents, including dots.
        for sdk in android dotnet ghc ghcup codeql; do
            directory="/__host_sdks/$sdk"
            test -d "$directory"
            find "$directory" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
        done
        ;;
    *)
        echo 'usage: prepare-disk.sh host|container' >&2
        exit 2
        ;;
esac
df -h .
