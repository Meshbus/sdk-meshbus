/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MESHBUS_INCLUDE_DESKTOP_PACKAGE_H_
#define MESHBUS_INCLUDE_DESKTOP_PACKAGE_H_
#include "meshbus/desktop.pb.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Manage the registered file set of an MBA. Blocking, thread context only.
 * Inputs are caller-owned for the call; the response is copied. Negative errno
 * rejects the operation. A failed prepare/commit retains its persistent marker
 * until explicit recovery or abort. Only registered app files are removed;
 * saves are preserved unless the request explicitly enables their removal.
 * Successful commit means verified files and the current-version record agree;
 * this does not promise physical power-loss durability.
 */
int mbs_desktop_package_manage(const meshbus_DesktopPackageRequest *request,
			       meshbus_DesktopPackageResponse *response);
#ifdef __cplusplus
}
#endif
#endif
