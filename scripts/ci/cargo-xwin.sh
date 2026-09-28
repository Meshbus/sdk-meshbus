#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
if [[ ${1:-} == build ]]; then
    shift
    exec cargo xwin build "$@"
fi
exec cargo "$@"
