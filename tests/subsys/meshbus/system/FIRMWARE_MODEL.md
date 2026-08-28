# FIRMWARE deterministic system model

`firmware_model.py` is a host-only fault model for target-truth resume,
checkpoint rollback, direct-route rejection, power gates, idempotent lifecycle
operations, confirmation, and rollback. It deliberately does not reuse the C
service and therefore cannot prove protobuf compatibility, firmware behavior,
RRAM writes, MCUboot swap, LoRa timing, or hardware safety. Those boundaries
remain with focused firmware tests and the Phase 6/7 hardware matrices.
