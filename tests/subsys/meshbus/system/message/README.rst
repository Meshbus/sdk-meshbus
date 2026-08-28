Meshbus Message two-DUT endpoint
================================

This image is the deterministic endpoint for two-device Message and MeshCore
system tests. It uses the real SX1262, MeshCore runtime, Contact store, and
Message service on either ``idea_mesh_tracker_c2`` or ``devkit_nrf54l15``. It
is headless and therefore does not depend on display hardware.

The DevKit validation image uses the project's established 900 MHz shield
frequency of 915.125 MHz at 0 dBm for close-range bench testing. It reapplies
those values when changing the test TX lock so stale persisted radio settings
cannot silently change a run.

The endpoint boots with ``receive_only`` enabled and immediately reapplies
that lock if a prior test persisted TX-enabled state. It cannot send an advert
or message until the host explicitly runs ``mb_msg_test tx 1``. Cleanup runs
``mb_msg_test cleanup``, which disables TX, removes contacts from the
test-owned store, drains received messages, and clears endpoint counters.

Only the public identity is printed. The endpoint never prints or accepts a
private identity key, management secret, or pairing secret.

The checked-in endpoint default is ``470.125 MHz`` at ``14 dBm``. Changing
the RF configuration requires rebuilding the endpoint and separate approval
for the resulting frequency and power.

Storage and claim boundary
--------------------------

The final 64 KiB of slot1 is temporarily mapped as test-only Settings storage.
Product storage at ``0x174000..0x17cfff`` remains mapped but unselected and is
never erased by the endpoint. Running the image invalidates any secondary
upgrade image occupying the shortened slot1.

This endpoint can prove real RF Message delivery and ACK behavior only when a
separately authorized host run controls two devices. A build or one-device
boot proves endpoint readiness only. It does not by itself close the complete
Phase 5 client/client scenario, which also covers telemetry, management,
reboot recovery, and direct-path evidence.

Build from the west workspace root::

   west build -p always -d /tmp/meshbus-message-system-c2 \
     -b idea_mesh_tracker_c2/nrf54l15/cpuapp \
     sdk-meshbus/tests/subsys/meshbus/system/message

Control commands
----------------

The host uses these serial shell commands::

   mb_msg_test identity
   mb_msg_test identity_new <run_name>
   mb_msg_test tx <0|1>
   mb_msg_test peer_add <64_hex_public_key> <name>
   mb_msg_test peer_path <64_hex_public_key> <hash_size> <path_hex|->
   mb_msg_test peer_remove <8_hex_prefix>
   mb_msg_test advert <0|1>
   mb_msg_test anon <normal|direct|path> <64_hex_public_key> <delay_ms> <hash_size> <path_hex|-> <payload>
   mb_msg_test send <8_hex_prefix> <attempt> <flood:0|1> <text>
   mb_msg_test burst <8_hex_prefix> <first_attempt> <count> <text_prefix>
   mb_msg_test next
   mb_msg_test stats
   mb_msg_test radio_diag
   mb_msg_test radio_rearm
   mb_msg_test radio_cycle
   mb_msg_test clear
   mb_msg_test cleanup

``identity_new`` disables TX, clears test-owned contacts/messages, generates a
new local identity, and prints only its public key. Test payloads intentionally
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
