/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <desktop/desktop.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zui_desktop;

int desktop_app_registry_start(struct zui_desktop *desktop,
			       mbs_desktop_app_handle_t handle);
int desktop_app_registry_complete_exit(mbs_desktop_app_handle_t handle,
				       k_timeout_t timeout);
bool desktop_app_registry_is_running(mbs_desktop_app_handle_t handle);
bool desktop_app_registry_is_external_handle(mbs_desktop_app_handle_t handle);

int desktop_app_registry_external_prepare(const struct mbs_desktop_external_app_desc *desc,
					  mbs_desktop_app_handle_t *handle_out);
int desktop_app_registry_external_release(mbs_desktop_app_handle_t handle);

int desktop_app_registry_detach_desktop_all(struct zui_desktop *desktop);

#ifdef __cplusplus
}
#endif
