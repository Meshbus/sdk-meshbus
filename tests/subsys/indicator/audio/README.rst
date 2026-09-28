Indicator audio contract
########################

Run ``west twister -T meshbus/tests/subsys/indicator/audio -p qemu_x86``
from the west workspace with a task-specific output directory.

The scenario substitutes a PWM driver and publishes synthetic fuel-gauge
samples. It exercises public semantic feedback/configuration APIs, real audio
scheduling and PWM playback. Coverage includes source routing, ACK system routing,
coalescing, preemption, independent key sounds, condition cooldown, explicit
startup readiness, disabled-category cancellation and complete melodies after
workqueue delays. The sibling Indicator contract checks Message result ZBus
routing and suppression through the system preference.

These checks do not establish acoustic output, perceived loudness, BLE pairing,
real battery measurements or RF failure recovery. Device evidence belongs in
the task's ignored hardware record.
