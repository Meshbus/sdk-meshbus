# Zephyr Testing Guide

Read this guide for Twister, ztest, test creation, hardware evidence, serial
validation, or test failures.

## Select The Smallest Proof

Use the nearest `testcase.yaml` or `sample.yaml` as the platform and scenario
source of truth.

| Change | First verification |
| --- | --- |
| Public Meshbus API or ZBus behavior | Matching service contract suite |
| Service implementation or settings | Matching service suite; add persistence cases when observable |
| MeshCore protocol behavior | Matching `tests/lib/meshcore` suite |
| Driver, DTS, Kconfig, or board metadata | Closest driver test or consuming sample build |
| Desktop/ZUI behavior | Desktop integration suite, then current consuming product build when composition matters |
| Documentation only | `git diff --check` |

QEMU/build results do not prove physical behavior.

## Twister

```sh
source ~/.zephyr/env/bin/activate
sdk_root="$(git rev-parse --show-toplevel)"
west_root="$(west topdir)"
cd "$west_root"
west twister -T "$sdk_root/tests/<path>" -p <platform> \
  -O "$west_root/twister-out/sdk-<name>" --inline-logs -v -c
```

If a narrow parallel run fails with setup or generated-config noise, rerun the
same path once with `-j 1` before classifying a product regression.

For service contracts, follow `tests/subsys/meshbus/AGENTS.md`. Contract tests
exercise public headers and public ZBus behavior; a private-seam integration
scenario remains separate. Use the exact platform and leaf application from
the local YAML.

## Adding Or Changing Tests

- Keep tests beside the affected subsystem.
- Prefer public APIs and observable events over private hooks.
- Use ordinary ztest code and the lowest dependency double needed for a
  deterministic fault.
- Keep QEMU, private integration, service-DUT, and multi-device claims distinct.
- Update `testcase.yaml`, configuration, overlays, and sources together.
- Use bounded waits and deterministic cleanup.

## Hardware Authorization And Evidence

Do not run flashing, debug, device-testing, reset, erase, power interruption,
destructive shell commands, or instrument control without explicit user
authorization for the action.

For an authorized hardware run, record only the information needed to reproduce
and classify it: testcase, board/configuration, artifact, device/fixture,
command, timeout, expected observation, result, cleanup, and final state. Keep
transcripts and artifacts in ignored output locations. Never commit credentials,
keys, pairing secrets, or live host-specific device maps.

A fixture match or boot banner is not a physical assertion. Report missing
fixtures, authorization, manual observation, or cleanup as unverified rather
than pass.

## Serial Evidence

When the user requests live serial work, load the project `serial-use` skill.
Start passively, save a transcript for evidence, and wait for the business
result rather than only the first shell prompt. Serial access does not imply
authorization to reset, flash, or send destructive commands.

## Remote Tests

For explicitly requested remote Twister or remote hardware transport, read
`scripts/remote/README.md` and the selected `west remote ... --help` output.
Do not preload the remote runbook for local tests.

## Failure Handling

Find the first relevant build log, runtime log, or assertion. Classify obvious
infrastructure and fixture failures separately from product failures. Patch
once when the cause is clear and in scope, rerun the same narrow scenario, and
report a persistent failure without unbounded scope expansion.
