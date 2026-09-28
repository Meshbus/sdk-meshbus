#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
# Reuse shallow image repositories through west's native path cache.
test -f "$MESHBUS_WEST_SEED/west-frozen.yml"
test "$(git -C "$MESHBUS_WEST_SEED/zephyr" rev-parse --is-shallow-repository)" = true
git config --global --add safe.directory /work/meshbus
python meshbus/scripts/ci/workspace.py --workspace /work --record /work/snapshot
(cd meshbus && actionlint)
pip check
gitleaks version
trivy --version
grype version
ccache --version
# Prove both ILP32 C/C++ execution and preservation of the ARM64 cross tools.
printf '#include <errno.h>\nint main(void) { return sizeof(void *) != 4; }\n' > /work/smoke-native.c
gcc -m32 /work/smoke-native.c -o /work/smoke-native-c
/work/smoke-native-c
g++ -m32 /work/smoke-native.c -o /work/smoke-native-cxx
/work/smoke-native-cxx
aarch64-linux-gnu-gcc -c /work/smoke-native.c -o /work/smoke-arm64.o
file /work/smoke-arm64.o | grep -q 'ARM aarch64'
for board in qemu_cortex_m3 qemu_x86 qemu_riscv32; do
    west build -b "$board" zephyr/samples/hello_world -d "/work/smoke-$board"
done
west twister -T meshbus/tests/subsys/clock --integration --filter runnable \
    --inline-logs -j 2 -O /work/smoke-clock
west twister -T meshbus/tests/subsys/meshcore/role_lifecycle \
    -s subsys.meshbus.meshcore.integration.role_lifecycle.runtime \
    --integration --filter runnable --inline-logs -j 2 -O /work/smoke-meshcore
python meshbus/scripts/ci/checks.py twister /work/smoke-clock/twister.json --runnable
python meshbus/scripts/ci/checks.py twister /work/smoke-meshcore/twister.json --runnable
# A compiler launcher must actually be set, not merely mentioned by picolibc.
grep -R -q 'LAUNCHER = /usr/local/bin/ccache' /work/smoke-clock /work/smoke-meshcore --include=build.ninja
ccache --show-stats
python -m unittest discover -s meshbus/scripts/ci/tests -q
cargo test --locked --manifest-path meshbus/scripts/meshbus/Cargo.toml
cargo xwin build --locked --release --manifest-path meshbus/scripts/meshbus/Cargo.toml \
    --target x86_64-pc-windows-msvc
