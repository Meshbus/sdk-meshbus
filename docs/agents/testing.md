# Selecting validation tools

Use this guide when choosing tools or preparing device evidence. Environment,
build and serial commands live in [DEVELOPMENT.md](../../DEVELOPMENT.md);
signing and deployment policy lives in [DISTRIBUTION.md](../../DISTRIBUTION.md).
Reuse the task's existing device, authorization and acceptance decisions.

## Match the tool to the claim

| Claim or task | First tool or source | Evidence limit |
| --- | --- | --- |
| Public service behavior or focused integration | Nearest `testcase.yaml` / `sample.yaml`, then the declared Twister scenario | Fake backends and simulation do not prove a physical device. |
| Product composition and image layout | Native `west build --sysbuild`, final configuration and image reports | Linking does not prove startup. Deploy the complete image set for the authorized build mode; engineering validation does not establish signed-product qualification. |
| UART service commands | Repository `west meshbus connect … --json`; inspect its command help and schema | Check the installed firmware's capabilities and the CLI command schema before interpreting an unsupported command. |
| Startup or asynchronous device behavior | `scripts/serial_use.py` monitor/session and transcript checks | Wait for the business result; inspect relevant raw errors even when a checker reports success. |
| OLED framebuffer export | `scripts/display_capture.py`; [capture usage](../../scripts/meshbus/README.md#display-capture) | PNG proves the software frame and UART transfer. Physical OLED appearance needs explicit observation. |
| Desktop navigation and synthetic keys | Existing MCUmgr `input inject act` / `input inject raw`; [input usage](../../scripts/meshbus/README.md#input-injection) | `accepted` confirms ZBus publication, not UI consumption. Raw edges do not run hardware gesture detection. |
| Flash, reset, or device debug | Existing board runner / `pyocd`, within the approved device and operation | Serial and probe identities are separate. Debug access is an operation, not passive discovery. |
| Remote access | Existing `west remote` tools and `scripts/remote/README.md` | Remote access needs authorization; forwarding does not move build ownership. |
| BLE service transport | First inspect available client capabilities, firmware advertising and GATT/security configuration | Advertising, connection, authenticated service access and payload transfer are separate results. The UART CLI is not a BLE client. |

Choose the narrowest applicable row. Inspect the current tool's `--help` and
existing helpers before writing another transport, generating bindings, or
installing packages. Activate the existing development environment for the
whole command tree. If a necessary host capability is missing, report the gap;
when a task-local dependency installation is authorized, isolate it in the
ignored task directory and preserve shared environments and manifests.

For Python host-tool changes, run the relevant `scripts/tests/test_*.py`
unittest module. Use a fake CLI or recorded frame to test assembly and PNG
rendering without device access. Such replay validates the host tool only;
it is not another live transfer or a new physical observation.

## Device workflow

Identify the selected device and installed firmware before testing a new
command. Multiple connected boards are not interchangeable. When a deployment
is needed, reuse explicit authorization already given; ask only about a
missing target or additional action. Prepare the concrete artifact and its
layout before requesting deployment approval. Never substitute a public PEM
for signing. Do not change device security settings without explicit authorization.
MCUboot validation must record the selected authentication mode. Public SDK
builds default to hash-only validation; authenticated builds explicitly select
Ed25519 and a caller-owned key. Deploy the matching bootloader/application pair
only within the authorized device scope, especially when changing modes. Keep
build, physical recovery and signed-product qualification evidence separate.

For startup evidence, start serial capture before the authorized reset/flash.
Keep a single owner of a UART endpoint at a time; close a monitor before running
an independent CLI command against that endpoint. Preserve the boot transcript
and compare preexisting errors separately from regressions. Use bounded waits
and retain the first failing response rather than repeatedly guessing timeouts.

For BLE work specifically, derive discovery filters from the current firmware:
an advertised application service may differ from the management GATT service,
and the display name may differ from the product name. Resolve device identity
before connecting. A scan result does not establish authenticated access;
record connection/security/transfer outcomes separately, without pairing secrets.
Do not promote an unverified task-local BLE script as a supported test tool.

When the user removes a test from scope, stop the task-owned process, close its
connections and record it as cancelled/unverified. Do not continue discovery,
pairing or retry loops for that test. Preserve earlier valid evidence and note
the last observed device state rather than claiming an unobserved restoration.

## Completion record

Keep raw commands, logs, device mappings and images in ignored task storage.
Record source/build identity, selected endpoint/probe, actual commands and
results, relevant failures, cleanup and remaining acceptance gaps. Use the
evidence layers in [test rules](../../tests/subsys/AGENTS.md). A tool's
exit status or log-pattern check is supporting evidence; inspect the required
business output before marking a claim passed.

For visual acceptance, present the captured image and ask only for the physical
comparison the host cannot observe. Record the user's answer separately from
the transfer result. Once required checks pass, stop; additional transports,
soak tests and release qualification require a named remaining acceptance gap.
