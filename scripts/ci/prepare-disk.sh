#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
# Never use this cleanup on a developer machine or a persistent runner.
test "${RUNNER_ENVIRONMENT:-}" = github-hosted
test "${RUNNER_OS:-}" = Linux
mode="${1:-}"
required_gib="${2:-12}"
[[ "$required_gib" =~ ^[1-9][0-9]*$ ]]
case "$mode" in
    host|container) ;;
    *) echo 'usage: prepare-disk.sh host|container [required-GiB]' >&2; exit 2 ;;
esac
required_kib=$((required_gib * 1024 * 1024))
enough_space() {
    available_kib=$(df -Pk . | awk 'NR==2 {print $4}')
    [[ "$available_kib" =~ ^[0-9]+$ ]]
    echo "Available: $((available_kib / 1024)) MiB; required: $((required_kib / 1024)) MiB"
    (( available_kib >= required_kib ))
}
df -h .
if enough_space; then exit 0; fi
host_paths=(/usr/local/lib/android /usr/share/dotnet /opt/ghc /usr/local/.ghcup /opt/hostedtoolcache/CodeQL)
container_paths=(android dotnet ghc ghcup codeql)
for index in 0 1 2 3 4; do
    if [[ "$mode" == host ]]; then
        sudo rm -rf -- "${host_paths[$index]}"
    else
        # Preserve mounted directory roots, deleting only their contents.
        directory="/__host_sdks/${container_paths[$index]}"
        test -d "$directory"
        find "$directory" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
    fi
    if enough_space; then df -h .; exit 0; fi
done
echo 'Insufficient disk space after removing unused hosted SDKs' >&2
exit 1
