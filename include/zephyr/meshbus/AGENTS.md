# Meshbus Public Header Rules

Scope: public Meshbus headers under `include/zephyr/meshbus/`.

Non-scope: service implementation, persistence, shell, mgmt, workqueue,
settings, and device-runtime policy. Those belong under `subsys/meshbus/`.

## 1. Boundary

- Anything declared here is public API or public ABI.
- Keep headers stable, minimal, and implementation-neutral.
- Do not expose private service structs, globals, mutexes, work items, settings
  descriptors, shell helpers, or mgmt helpers.
- If a header changes, keep the matching implementation, public tests, and
  board-facing samples aligned.

## 2. Current Header Map

| Header | Public role |
| --- | --- |
| `bluetooth.h` | Bluetooth config plus pairing/state channels. |
| `channel.h` | MeshCore fixed channel-slot store API. |
| `clock.h` | Clock config API. |
| `contact.h` | Contact store API plus Contact request/response channels. |
| `desktop.h` | Desktop app/widget registration, event IDs, handles, registry access. |
| `display.h` | Display config/active-state API plus state channel. |
| `gnss.h` | GNSS config/status/cache API plus Compass-widget heading leases and snapshots. |
| `indicator.h` | Light/buzzer config and playback API. |
| `input.h` | Key and action event channels. |
| `llext.h` | LLEXT config/control API, metadata ABI, app launch API, ZBus bridge ABI. |
| `meshcore.h` | MeshCore config/runtime API plus MeshCore request/response channels. |
| `meshbus.h` | Reserved Meshbus-wide header; prefer specific service headers. |
| `message.h` | Message send/receive API plus request/response channels. |
| `notify.h` | Notify protobuf aliases plus notify channel/publish API. |
| `power.h` | Power config/status/action API plus fuel-gauge channel. |
| `radio.h` | Radio config/status/control API plus TX/RX channels and optional stats. |
| `telemetry.h` | Telemetry config/sample/binding API plus data channel. |
| `time.h` | Meshbus-wide business timestamp helpers with realtime/uptime fallback. |

## 3. Header Shape

Use this order:

1. File banner and Doxygen `@file`.
2. Include guard.
3. Minimal includes.
4. `extern "C"` guard.
5. Forward declarations and typedefs.
6. Public constants/macros.
7. Public structs/enums.
8. Public ZBus declarations or iterable-section registration macros.
9. Public function declarations.
10. `extern "C"` close and include-guard close.

Include order:

- Standard headers.
- Zephyr headers.
- Generated/local headers, for example `"meshbus/radio.pb.h"`.

Prefer forward declarations when pointer types are enough. Keep feature-gated
includes and declarations under the same Kconfig condition.

## 4. Public Types

- Use `meshbus_<svc>_*` for new globally visible symbols.
- Use `MESHBUS_<SVC>_*` for macros.
- Preserve shipped legacy names unless a breaking change is intentional.
- For persisted or mgmt-shared config, prefer aliasing the service-owned
  nanopb type, for example `typedef meshbus_RadioConfig meshbus_radio_config;`.
- Do not create a parallel public C config struct for a protobuf-backed config.
- Treat public struct layouts, enum values, macro constants, metadata layouts,
  and LLEXT bridge channel numbers as ABI.

## 5. Public ZBus Channels

- Declare public channels only in the matching service header.
- Use `meshbus_<svc>_<noun>_chan` for channel symbols.
- Payload structs are public API; document direction, valid lengths, units, and
  ownership/lifetime.
- Keep validators, listeners, observer wiring, and work handoff in the service
  implementation.
- When exposing a new public channel to LLEXT bridge clients, append to
  `enum meshbus_llext_zbus_channel`; do not renumber existing values.

## 6. Common API Patterns

For config-backed services, use the established shape unless there is a clear
reason not to:

- `meshbus_<svc>_config_get(meshbus_<svc>_config *cfg)`
- `meshbus_<svc>_config_set(const meshbus_<svc>_config *cfg)`
- `meshbus_<svc>_config_reset(void)`

Document valid ranges, units, persistence behavior, and state-dependent errors.

## 7. Comments And Errors

- Every public function, struct, enum, ABI macro, and ZBus channel should have a
  concise Doxygen comment.
- Document pointer ownership, buffer sizes, units, and scaled values.
- Use standard negative errno values:
  - `-EINVAL`: invalid argument or config value.
  - `-ENOENT`: missing object or index out of range.
  - `-EEXIST`: duplicate object.
  - `-EBUSY`: state conflict.
  - `-ENODEV`: service/device unavailable.
  - `-EIO`: device or bus failure.

## 8. Special ABI Surfaces

- `desktop.h`: app handles, event IDs, registration macros, and iterable-section
  descriptors are public ABI for desktop modules. Read the desktop `AGENTS.md`
  files before changing them.
- `llext.h`: metadata structs, magic/version values, max lengths, section names,
  icon payload shape, state/restart enums, and ZBus bridge numbers are external
  ABI. Append values; do not renumber existing values.

## 9. Finish Checklist

- No private implementation type leaked into a public header.
- Declarations match service implementation and enabled build configurations.
- Protobuf-backed aliases still match the owning `.proto` schema.
- Public ZBus payloads match the service channels and tests.
- Desktop or LLEXT ABI users are updated when their public surface changes.
