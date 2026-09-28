/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <llext/llext.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_MBS_FIRMWARE_IMAGE_IDENTITY) && defined(CONFIG_HWINFO)
#include <firmware/image.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/app_version.h>
#include "mbs_llext_metadata_version.h"
#endif

int mbs_llext_host_info_get(meshbus_LlextHostInfoResponse *info)
{
	if (info == NULL) {
		return -EINVAL;
	}
#if defined(CONFIG_MBS_FIRMWARE_IMAGE_IDENTITY) && defined(CONFIG_HWINFO)
	meshbus_LlextHostInfoResponse result = meshbus_LlextHostInfoResponse_init_zero;
	size_t image_size;
	ssize_t count;
	int rc;

	BUILD_ASSERT(sizeof(CONFIG_BOARD_TARGET) <= sizeof(result.target));
	BUILD_ASSERT(sizeof(STRINGIFY(APP_BUILD_VERSION)) <= sizeof(result.build_revision));
	count = hwinfo_get_device_id(result.device_id.bytes, sizeof(result.device_id.bytes));
	if (count <= 0) {
		return count < 0 ? (int)count : -ENODEV;
	}
	result.device_id.size = count;
	rc = mbs_firmware_image_identity(result.image_sha256.bytes, &image_size);
	if (rc != 0) {
		return rc;
	}
	result.image_sha256.size = MBS_FIRMWARE_IMAGE_HASH_SIZE;
	result.protocol_version = 1U;
	result.metadata_version = MBS_LLEXT_METADATA_VERSION;
	strcpy(result.target, CONFIG_BOARD_TARGET);
	strcpy(result.build_revision, STRINGIFY(APP_BUILD_VERSION));
	*info = result;
	return 0;
#else
	return -ENOTSUP;
#endif
}
