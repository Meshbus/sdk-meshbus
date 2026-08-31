/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/meshbus/desktop.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zui_desktop;

int desktop_app_registry_start(struct zui_desktop *desktop,
			       meshbus_desktop_app_handle_t handle);
int desktop_app_registry_complete_exit(meshbus_desktop_app_handle_t handle,
				       k_timeout_t timeout);
bool desktop_app_registry_is_running(meshbus_desktop_app_handle_t handle);
bool desktop_app_registry_is_external_handle(meshbus_desktop_app_handle_t handle);

int desktop_app_registry_external_prepare(const struct meshbus_desktop_external_app_desc *desc,
					  meshbus_desktop_app_handle_t *handle_out);
int desktop_app_registry_external_release(meshbus_desktop_app_handle_t handle);

int desktop_app_registry_detach_desktop_all(struct zui_desktop *desktop);

#ifdef __cplusplus
}
#endif
