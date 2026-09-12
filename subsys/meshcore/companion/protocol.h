/*
 * Copyright (c) 2026 FoBE Studio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOBE_SUBSYS_MBS_SERVICES_MESHCORE_COMPANION_PROTOCOL_H_
#define FOBE_SUBSYS_MBS_SERVICES_MESHCORE_COMPANION_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MESHCORE_COMPANION_MAX_FRAME_SIZE 172U

struct meshcore_companion_transport {
	/*
	 * Copy a complete frame into the transport's bounded TX queue without
	 * waiting for space or performing I/O. Called from both command handlers
	 * and event listeners, with no adapter lock held. Return 0 only after
	 * accepting the frame, or -ENOSPC when full. The transport owns delivery,
	 * retry, and discarding pending frames on disconnect or reinitialization.
	 */
	int (*send)(const uint8_t *frame, size_t len, void *user_data);
	void *user_data;
};

int meshcore_companion_adapter_init(const struct meshcore_companion_transport *transport);
void meshcore_companion_adapter_connected(void);
void meshcore_companion_adapter_disconnected(void);
int meshcore_companion_adapter_rx_frame(const uint8_t *frame, size_t len);
int meshcore_companion_adapter_queue_frame(const uint8_t *frame, size_t len);
void meshcore_companion_adapter_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* FOBE_SUBSYS_MBS_SERVICES_MESHCORE_COMPANION_PROTOCOL_H_ */
