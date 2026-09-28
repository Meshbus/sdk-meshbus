Meshbus Firmware system model
=============================

This directory contains the deterministic host-only model described in
``FIRMWARE_MODEL.md``. Run ``python -m pytest`` on ``test_firmware_model.py``.
The model does not execute the C firmware or prove a device backend.

Real multi-device Message endpoints live under ``../../message/system/``.
A build-only endpoint is not a system PASS; external orchestration and evidence
must identify every device, role, fixture, command, cleanup action, and result.
