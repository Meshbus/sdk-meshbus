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

static struct k_event indicator_events;

static void blinky_log_indicator_events(void)
{
	uint64_t pending;
	int rc;

	rc = meshbus_llext_zbus_take_pending(&indicator_events, &pending);
	if (rc != 0) {
		return;
	}

	if ((pending & MESHBUS_LLEXT_ZBUS_CH_BIT(MESHBUS_LLEXT_ZBUS_INDICATOR_LIGHT_PLAY_CHAN))
	    != 0ULL) {
		struct meshbus_indicator_light_play_event event;

		rc = meshbus_llext_zbus_read(MESHBUS_LLEXT_ZBUS_INDICATOR_LIGHT_PLAY_CHAN,
					     &event, sizeof(event));
		if (rc == 0) {
			printk("[blinky] zbus light: on=%u off=%u count=%u\n",
			       event.on_duration_ms, event.off_duration_ms, event.count);
		}
	}

	if ((pending & MESHBUS_LLEXT_ZBUS_CH_BIT(MESHBUS_LLEXT_ZBUS_INDICATOR_BUZZER_PLAY_CHAN))
	    != 0ULL) {
		struct meshbus_indicator_buzzer_play_event event;

		rc = meshbus_llext_zbus_read(MESHBUS_LLEXT_ZBUS_INDICATOR_BUZZER_PLAY_CHAN,
					     &event, sizeof(event));
		if (rc == 0) {
			printk("[blinky] zbus buzzer: source=%d freq=%u duration=%u\n",
			       event.source, event.freq_hz, event.duration_ms);
		}
	}
}

void blink_thread_entry(void *p1, void *p2, void *p3)
{
	uint64_t indicator_mask;
	int rc;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	printk("[blinky] blink_thread_main: start\n");

	k_event_init(&indicator_events);
	indicator_mask =
		MESHBUS_LLEXT_ZBUS_CH_BIT(MESHBUS_LLEXT_ZBUS_INDICATOR_LIGHT_PLAY_CHAN) |
		MESHBUS_LLEXT_ZBUS_CH_BIT(MESHBUS_LLEXT_ZBUS_INDICATOR_BUZZER_PLAY_CHAN);
	rc = meshbus_llext_zbus_subscribe(&indicator_events, indicator_mask);
	if (rc != 0) {
		printk("[blinky] indicator zbus subscribe failed: %d\n", rc);
	}

	while (true) {
		uint32_t events;

		rc = meshbus_indicator_light_play(160, 160, 1);

		if (rc != 0) {
			printk("[blinky] indicator play failed: %d\n", rc);
		}

		events = k_event_wait(&indicator_events, MESHBUS_LLEXT_ZBUS_EVT_PENDING,
				      true, K_MSEC(420));
		if ((events & MESHBUS_LLEXT_ZBUS_EVT_PENDING) != 0U) {
			blinky_log_indicator_events();
		}
	}
}

LL_EXTENSION_SYMBOL(blink_thread_entry);
