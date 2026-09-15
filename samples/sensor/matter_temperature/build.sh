#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

sample_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(git -C "$sample_dir" rev-parse --show-toplevel)"
workspace_dir="$(cd "$repo_dir" && west topdir)"
: "${CHIP_ROOT:=$workspace_dir/modules/lib/matter}"
: "${MATTER_TOOLS_ROOT:=$repo_dir/.scratch/matter-temperature/tools}"
: "${ZAP_INSTALL_PATH:=$MATTER_TOOLS_ROOT/zap}"
: "${MATTER_BUILD_DIR:=$workspace_dir/build/matter-temperature-fork}"
CHIP_ROOT="$(cd "$CHIP_ROOT" && pwd)"
git -C "$CHIP_ROOT" rev-parse --verify HEAD >/dev/null
export PATH="$MATTER_TOOLS_ROOT:$PATH"
export PYTHONPATH="$MATTER_TOOLS_ROOT/python${PYTHONPATH:+:$PYTHONPATH}"
export ZAP_INSTALL_PATH CHIP_ROOT
command -v gn >/dev/null
[[ -x "$ZAP_INSTALL_PATH/zap-cli" ]]
# Refresh the generated IDL before CMake determines cluster source files.
if [[ ! -f "$sample_dir/environment.matter" ||
      "$sample_dir/environment.zap" -nt "$sample_dir/environment.matter" ]]; then
    python "$CHIP_ROOT/scripts/tools/zap/generate.py" \
        "$sample_dir/environment.zap" --no-prettify-output
fi
cd "$workspace_dir"
# Ninja invokes CMake when tracked build inputs change. Do not force a configure
# on each application-only or no-op build. Explicit CMake arguments still win.
if [[ $# -eq 0 && -f "$MATTER_BUILD_DIR/CMakeCache.txt" ]] &&
    python - "$MATTER_BUILD_DIR/CMakeCache.txt" "$sample_dir" "$CHIP_ROOT" <<'PY_CACHE'
from pathlib import Path
import sys
cache = {}
for line in Path(sys.argv[1]).read_text().splitlines():
    if '=' in line and ':' in line and not line.startswith(('//', '#')):
        key, value = line.split('=', 1)
        cache[key.split(':', 1)[0]] = value
matches = (cache.get('BOARD') == 'devkit_esp32c6/esp32c6/hpcore'
           and Path(cache.get('CMAKE_HOME_DIRECTORY', '')).resolve() == Path(sys.argv[2]).resolve()
           and Path(cache.get('CHIP_ROOT', '')).resolve() == Path(sys.argv[3]).resolve())
sys.exit(0 if matches else 1)
PY_CACHE
then
    exec west build -d "$MATTER_BUILD_DIR"
fi
exec west build -b devkit_esp32c6/esp32c6/hpcore "$sample_dir" \
    -d "$MATTER_BUILD_DIR" -- -DCHIP_ROOT="$CHIP_ROOT" "$@"
