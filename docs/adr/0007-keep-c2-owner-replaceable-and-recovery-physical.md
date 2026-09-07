---
status: accepted
---

# Keep C2 owner-replaceable and recovery physical

The C2 development-board product does not enable programming or debug-access
protection. Its owner may fully erase the device and replace both MCUboot and the
application over SWD. The FoBE-provided MCUboot exposes UART serial recovery but
does not expose BLE recovery.

## Consequences

- Under the FoBE-provided MCUboot, normal boot and UART recovery accept only
  applications authorized by the Production Image Key.
- The MCUboot BLE bridge and its unencrypted radio recovery path must be removed
  from the C2 production configuration.
- Removing MCUboot BLE recovery does not remove separately authenticated
  application-level BLE management features.
- Firmware signing does not prevent an owner with physical access from replacing
  the trust root, copying firmware, or running modified firmware.
- The Official Firmware Trust Boundary ends after an owner replaces MCUboot or
  the application outside the verified FoBE boot chain. Restoring the factory
  image restores that boundary.
- Factory and recovery documentation must distinguish UART recovery through
  MCUboot from full erase and programming through SWD.
