/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/llext/symbol.h>
#include <zephyr/meshbus/indicator.h>
#include <zephyr/meshbus/llext.h>
#include <zephyr/sys/printk.h>

void beeper_thread_entry(void *p1, void *p2, void *p3)
{
	struct meshbus_indicator_buzzer_play_event event = {
		.source = INDICATOR_SOURCE_SYSTEM,
		.freq_hz = 1760,
		.duration_ms = 120,
	};
	int rc;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	printk("[beeper] beeper_thread_entry: start\n");

	k_sleep(K_MSEC(1200));

	rc = meshbus_llext_zbus_publish(MESHBUS_LLEXT_ZBUS_INDICATOR_BUZZER_PLAY_CHAN,
					&event, sizeof(event));
	if (rc != 0) {
		printk("[beeper] buzzer zbus publish failed: %d\n", rc);
		return;
	}

	printk("[beeper] buzzer zbus publish ok: freq=%u duration=%u\n",
	       event.freq_hz, event.duration_ms);
}

LL_EXTENSION_SYMBOL(beeper_thread_entry);
