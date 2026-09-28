# Indicator feedback

The product uses a single light to show recent user operations and persistent
conditions. Mesh Probe R1 and R2 use a blue GPIO LED; meaning is encoded in
rhythm, independent of color. The buzzer has independent routing and playback priority.

| Meaning | On / off / count | Repetition |
| --- | --- | --- |
| Idle heartbeat | 10 ms / 4990 ms | Every 5 seconds |
| Message request accepted locally | 80 ms / 0 / 1 | Once |
| Incoming direct or channel message | 80 ms / 120 ms / 2 | Once |
| Peer ACK, startup ready, pairing success | 400 ms / 0 / 1 | Once |
| Direct message not confirmed before its deadline | 400 ms / 200 ms / 2 | Once |
| Message or pairing operation failed | 80 ms / 120 ms / 3 | Once |
| Low battery | 1000 ms / 0 / 1 | Every 60 seconds |
| Critical startup or sustained radio fault | 200 ms / 200 ms / 3 | Every 10 seconds |

Acceptance means that the local request was submitted. A peer ACK confirms
receipt, not reading. Channel messages never claim delivery. The Message service
tracks direct-message ACKs locally by peer prefix and attempt. FAILED means the
public send API rejected submission synchronously. Later backend queue or radio
failures are not associated with individual messages. A missing ACK is reported
only as UNCONFIRMED; it does not identify the cause. Radio health still tracks
sustained driver failures independently. Channel sends have no completion slots
or delivery indication, only local submission acceptance/rejection.
The existing contact request timeout controls direct-message confirmation deadlines.

All public Message send APIs publish operation events. Indicator configuration
controls the resulting lights and sounds for Desktop, shell, MCUmgr, Companion
and MBA callers. Sending requests do not carry presentation preferences. Raw ZBus send requests are transport-level
requests without Message lifecycle ownership and default to silent.
Background advertisements, forwarding, bonded reconnections and unread-message
state do not generate transient light feedback.

The priority order is fault, failed/unconfirmed operation, received/accepted/
successful operation, low battery, then heartbeat. Higher priority playback
interrupts and discards the old pattern. Equal priority events wait. Each event
kind has one pending slot, merges arrivals within one second, and expires after
three seconds without extending its original deadline. The light is not a
message counter. Repeating conditions leave gaps for operations to be displayed.
Heartbeat resumes its normal period without replaying missed beats.

`light_enabled` is the master switch. The independent `heartbeat_enabled`,
`message_enabled` and `system_enabled` fields under `light_feedback` control
routing. All default to enabled. Disabling a category drops its pending events
and ends its active semantic feedback. Desktop indicator settings, shell and
MCUmgr expose these preferences; settings reset restores all defaults.

New pairing is tracked from actual passkey display through one success, failure
or cancellation callback. Cancellation is silent; disconnect during that session
is failure. Ordinary connected/disconnected state is insufficient to trigger a
pairing result. This identifies a new pairing flow, not proof of human intent.

Startup checks enabled Message, MeshCore and radio service readiness after the
application initializers finish. Optional display, sensor and buzzer failures do
not cause the critical startup light. Failed startup is rechecked until recovery.
Radio TX health trips on three consecutive driver failures within a rolling
60-second window; successful TX or modem reinitialization clears TX health.
Invalid parameters, busy, access-policy refusal and cancellation do not count.
RX start/recovery failure clears only on explicit successful RX start or an
actual received packet; a busy return and successful TX do not prove RX recovery.
Disabling the radio suspends its runtime fault indication.

Low battery latches at 15% or below and clears at 20% or above. Charging pauses
its indication. Invalid battery samples do not create a low-battery condition.

Contract tests use GPIO emulation for light output and synthetic power events.
Message, Bluetooth and radio tests substitute their documented peers/drivers.
These tests do not prove physical LED readability, real battery accuracy,
Bluetooth pairing, radio delivery or power consumption on hardware.

## Sound feedback

Sound uses the same operation semantics, with an independent bounded scheduler.
There is no acceptance sound. Peer ACK sounds follow the system buzzer preference.

| Meaning | Sound |
| --- | --- |
| Direct message received | 2400 Hz / 80 ms, rest 100 ms, 3000 Hz / 80 ms |
| Channel message received | 2400 Hz / 80 ms |
| Send API rejected submission | Three 2200 Hz / 80 ms notes, 100 ms gaps |
| Confirmation deadline elapsed | Two 1400 Hz / 220 ms notes, 160 ms gap |
| Pairing success / peer ACK | 2000 Hz / 80 ms, rest 60 ms, 3000 Hz / 100 ms |
| Pairing failed | 3000 Hz / 100 ms, rest 60 ms, 1600 Hz / 120 ms |
| Startup ready | Existing short ascending startup melody, after critical readiness |
| Low battery | 1800 Hz / 150 ms, rest 120 ms, 1200 Hz / 200 ms |
| Sustained fault | 1800 Hz / 150 ms twice, 100 ms gaps, 1200 Hz / 250 ms |

Same-kind sounds merge within one second; each kind has one pending slot which
expires after three seconds. Higher priority interrupts and discards the old
sound, equal priority waits for the owned playback to actually finish. Nominal
melody duration only estimates the next completion check; workqueue delays do
not truncate remaining notes. Key sounds cannot interrupt semantic feedback.
Low battery and sustained fault sounds repeat at most every ten minutes;
condition flapping or toggling preferences does not reset that cooldown.
Charging stops low-battery sound, and recovery cancels pending/active condition
sounds. There are no heartbeat, forwarding, advertisement or reconnect sounds.

`buzzer_enabled` controls the master. Under `buzzer_feedback`, the existing
`direct_message_enabled`, `channel_message_enabled`, and `system_enabled` remain
independent. `input_enabled` controls key/encoder sounds. `system_enabled` also
controls outgoing rejection, unconfirmed-deadline and peer ACK sounds. These
preferences default on and are exposed through Desktop, shell and MCUmgr/CLI.
Sending does not change the recipient's indicator preferences.

The buzzer scheduler owns only its own playback token, so completing or disabling
a semantic sound does not stop an unrelated replacement melody. Explicit buzzer
stop also drops pending semantic sounds. Shutdown retains its existing melody.
