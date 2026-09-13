Indicator owned playback contract
#################################

``subsys.meshbus.indicator.playback`` runs on ``qemu_x86`` with the real
indicator service, playback worker and kernel synchronization. Its test-only
PWM driver records output periods instead of driving hardware. Public tokens
verify completion, preemption, stale-stop isolation, held notes and disabled
requests. It does not establish audible output or physical PWM accuracy.
