Meshbus Message two-DUT endpoint
================================

Select ``<qualified-board-target>`` from a system endpoint scenario in
``testcase.yaml`` and use its exact configuration and fixture. These commands
do not extend platform support or authorize device execution.


This image is the deterministic endpoint for two-device Message and MeshCore
system tests. It uses the real SX1262, MeshCore runtime, Contact store, and
Message service on the platforms declared in ``testcase.yaml``. It
is headless and therefore does not depend on display hardware.

RF frequency and power are defined by the selected test configuration. The
endpoint reapplies those values when changing the TX lock so stale persisted
settings cannot silently change a run. Review the final configuration and
attached radio before authorizing transmission.

The endpoint boots with ``receive_only`` enabled and immediately reapplies
that lock if a prior test persisted TX-enabled state. It cannot send an advert
or message until the host explicitly runs ``mbs_msg_test tx 1``. Cleanup runs
``mbs_msg_test cleanup``, which disables TX, removes contacts from the
test-owned store, drains received messages, and clears endpoint counters.

Only the public identity is printed. The endpoint never prints or accepts a
private identity key, management secret, or pairing secret.

The checked-in endpoint default is ``915.125 MHz`` at ``0 dBm``. Changing
the RF configuration requires rebuilding the endpoint and separate approval
for the resulting frequency and power.

Storage and claim boundary
--------------------------

The selected test overlay reuses part of the secondary image slot as test-only
Settings storage. Product storage remains unselected and is never erased by the
endpoint. Inspect the final devicetree for exact bounds. Running the image
invalidates any secondary upgrade image occupying the reused range.

This endpoint can prove real RF Message delivery and ACK behavior only when a
separately authorized host run controls two devices. A build or one-device
boot proves endpoint readiness only. Full client/client qualification also
requires telemetry, management, reboot recovery, and direct-path evidence.

Build from the west workspace root::

   west build -p always -d 'build/<task>-message-system' \
     -b '<qualified-board-target>' \
     meshbus/tests/subsys/message/system

Control commands
----------------

The host uses these serial shell commands::

   mbs_msg_test identity
   mbs_msg_test identity_new
   mbs_msg_test tx <0|1>
   mbs_msg_test peer_add <64_hex_public_key> <name>
   mbs_msg_test peer_path <64_hex_public_key> <hash_size> <path_hex|->
   mbs_msg_test peer_remove <8_hex_prefix>
   mbs_msg_test advert <0|1>
   mbs_msg_test anon <normal|direct|path> <64_hex_public_key> <delay_ms> <hash_size> <path_hex|-> <payload>
   mbs_msg_test send <8_hex_prefix> <attempt> <flood:0|1> <text>
   mbs_msg_test burst <8_hex_prefix> <first_attempt> <count> <text_prefix>
   mbs_msg_test next
   mbs_msg_test stats
   mbs_msg_test radio_diag
   mbs_msg_test radio_rearm
   mbs_msg_test radio_cycle
   mbs_msg_test clear
   mbs_msg_test cleanup

``identity_new`` disables TX, clears test-owned contacts/messages, and requests
a live MeshCore reset. After MeshCore configuration activation completes, run
``identity`` to read the new public identity. Test payloads intentionally
use a single shell argument so transcript parsing stays deterministic.
``burst`` performs immediate public Message API calls inside the endpoint, so
UART command-injection timing cannot confound back-to-back send tests.
Received Message and ACK markers include the most recent packet RSSI in dBm and
SNR in quarter-dB units so a two-DUT run can distinguish a weak RF link from
message scheduling or half-duplex contention.

The diagnostic commands separate raw radio delivery from MeshCore dispatch.
``radio_diag`` reports the service state, an instantaneous RSSI read, and the
SX1262 DIO1/BUSY GPIO levels without changing RX state. ``radio_rearm`` uses
the public AGC-reset path to stop and restart RX, while ``radio_cycle`` performs
a full public-service disable/enable cycle. They are test controls, not product
recovery policy.

The ``anon`` command exercises the public delayed anonymous-data APIs. A
``normal`` send may fall back to flood, ``direct`` fails closed without a known
Contact path, and ``path`` uses caller-owned path bytes (``-`` is a zero-hop
verified-neighbor path). TX completion markers include submission-to-completion
elapsed time so the host can verify the requested dispatcher delay.
