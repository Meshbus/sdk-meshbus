Meshbus Bluetooth Sample
#######################

This sample enables the Meshbus Bluetooth service to provide:

- Secure BLE connections (LE Secure Connections + MITM)
- MCUmgr over BLE (SMP over GATT via ``CONFIG_MCUMGR_TRANSPORT_BT``)
- Meshbus notify BLE transport via a vendor-specific GATT notify characteristic

Meshbus Notify BLE V1
*********************

When ``CONFIG_MESHBUS_BLUETOOTH_GATT_NOTIFY=y`` this sample also exposes a
vendor-specific Meshbus notify service for real-time best-effort protobuf
notifications:

- Service UUID: ``9d8e9f10-6137-4c6a-9a6a-9f3ad4c00100``
- Characteristic UUID: ``9d8e9f10-6137-4c6a-9a6a-9f3ad4c00101``
- Characteristic properties: notify + CCC
- Characteristic value: raw protobuf-encoded ``meshbus.Notify``

V1 semantics are intentionally narrow:

- notify is an invalidation signal, not a message-content snapshot
- ``message{event=RECV}`` means the client should fetch queued messages from the
  message service
- ``message{event=ACK, ack_token=...}`` means outgoing delivery state changed
- notify payloads do not carry a timestamp; receivers should timestamp locally
- only single-packet GATT notify, no fragmentation
- only when connected and subscribed
- oversize payloads larger than ``ATT_MTU - 3`` are dropped
- no persistence, replay, or retransmission

Building
********

From workspace root::

  west build -b idea_mesh_tracker_c2/nrf54l15/cpuapp -s sdk-meshbus/samples/subsys/meshbus/services/bluetooth

Running
*******

After flashing, the shell prompt is available on the board's default UART console.
Use BLE for pairing and MCUmgr SMP.

Useful shell commands::

  meshbus bluetooth status
  meshbus bluetooth config get
  meshbus bluetooth config set 1 fixed 123456
