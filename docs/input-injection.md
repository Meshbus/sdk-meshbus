# Input injection over MCUmgr

The existing Input service owns synthetic input commands in `meshbus/input.proto`.
They do not require a Shell, and are independent of Display snapshot capture.
Firmware must enable `CONFIG_MESHBUS_INPUT` and `CONFIG_MCUMGR`; Desktop
navigation additionally requires its runtime and input bridge to be active.

| Operation | Group / command | CLI command inside `connect -c` |
| --- | --- | --- |
| Inspect capabilities | 76 / 0, READ | `input status` |
| Publish a processed action | 76 / 1, WRITE | `input inject act <type> <code> <action>` |
| Publish a raw event | 76 / 2, WRITE | `input inject raw <type> <code> <value>` |

For keys, use type `key` (Zephyr `INPUT_EV_KEY`, 1). Codes are numeric Zephyr
input codes, not ZUI enum values or key names. The current Desktop bridge maps:

| Key | Code | Zephyr constant |
| --- | --- | --- |
| Up | 103 | `INPUT_KEY_UP` |
| Down | 108 | `INPUT_KEY_DOWN` |
| Left | 105 | `INPUT_KEY_LEFT` |
| Right | 106 | `INPUT_KEY_RIGHT` |
| OK / Enter | 28 | `INPUT_KEY_ENTER` |
| Back | 1 | `INPUT_KEY_ESC` |

Use `short` for a click and `long` for a synthetic long press. These publish
already interpreted actions; `long` does not physically hold a button or wait
for the configured long-press threshold. In an active Desktop, the bridge
synthesizes missing press/release edges around an action when needed.

From an activated west workspace, with the selected device already authorized:

```sh
west meshbus connect -p '<serial-port>' --json -c 'input status'
west meshbus connect -p '<serial-port>' --json -c 'input inject act key 108 short'
west meshbus connect -p '<serial-port>' --json -c 'input inject raw key 108 0'
```

The second command requests Down; the third releases the same key, including
any display-wake suppression state. To request a long OK action, use
`input inject act key 28 long`, followed by `input inject raw key 28 0`.

## Raw edges and display wake

Raw values `1` and `0` represent press and release. Publication goes directly
to the Meshbus raw ZBus channel, downstream of hardware gesture detection.
A raw press followed by release does **not** synthesize a short or long action.
Use `inject act` for ordinary navigation; use raw edges only when the test
specifically needs edge behavior, and always release a key the test pressed.

An inactive display normally consumes the first key event to wake. For key
actions, wake suppression remains armed until a matching raw release; repeating
the same action without release can continue to be suppressed. Send the matching
release after an injected action, observe that the display is active, then send
the intended navigation action if the first one only woke the display. Avoid
unbounded retries or simultaneous physical key presses during a scripted case.

## Observe the result

The response's `accepted: true` confirms successful publication on the Input
channel. It does not guarantee Desktop was available, that its queue accepted
every event, or that the active view handled the action. Check the complete CLI
JSON result and observe the resulting screen separately.

For a visual test, capture a baseline with
[the UART capture tool](display-dump.md#uart-capture-tool), send one intended
action and its release, then capture into a new directory after the expected UI
transition. Rendering is asynchronous: allow a bounded wait for the expected
content rather than assuming the command response is a display-refresh barrier.
Record the action, response and before/after frames. Physical button contacts,
debounce, hold timing and physical OLED output need their own evidence.

Injected actions have the active screen's normal effects, including settings or
power actions. Select a known screen/action within the task's authorization.
The host `connect` implementation used here is UART; this workflow makes no
claim about BLE transfer.

Technical ownership: `subsys/meshbus/services/input/mgmt.c` handles the schema;
`input.c` publishes the public events; `desktop/desktop_input.c` under the same
services directory maps them into Desktop input. The CLI command catalog and
`execute_input_inject` in `scripts/meshbus/src/commands/connect.rs` own parsing.
