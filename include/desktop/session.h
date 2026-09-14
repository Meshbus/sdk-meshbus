/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MESHBUS_INCLUDE_DESKTOP_SESSION_H_
#define MESHBUS_INCLUDE_DESKTOP_SESSION_H_
#include <stdint.h>
#include "meshbus/desktop.pb.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Copied state of the latest foreground managed Session. */
typedef meshbus_DesktopMbaResponse mbs_desktop_mba_status;
/** Queue a foreground launch. ID must match MBA metadata; paths are confined
 * to the app root by the loader. No UI input is synthesized.
 * @return 0 means accepted, not started; query the returned Session to observe
 * completion. -EBUSY retains the current app; negative errno rejects requests.
 * All input strings are copied; output is caller-owned and valid on success.
 */
int mbs_desktop_mba_start(const char *id, const char *path, mbs_desktop_mba_status *status);
/** Request cooperative stop of exactly this Session, timeout 1..30000 ms.
 * A timed-out app remains loaded until it returns and cleanup completes.
 * 0 accepts the request; resources_reclaimed authorizes subsequent replacement.
 */
int mbs_desktop_mba_stop(uint64_t session, uint32_t timeout_ms, mbs_desktop_mba_status *status);
/** Read a copied snapshot, without loading, stopping or reclaiming anything.
 * Zero selects the latest Session. -ESTALE rejects old/rebooted Session IDs.
 */
int mbs_desktop_mba_get_status(uint64_t session, mbs_desktop_mba_status *status);
#ifdef __cplusplus
}
#endif
#endif /* MESHBUS_INCLUDE_DESKTOP_SESSION_H_ */
