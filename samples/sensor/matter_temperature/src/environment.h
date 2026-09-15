/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <lib/core/CHIPError.h>

/* Called on the Matter thread after server initialization. */
CHIP_ERROR environment_init();
/* Called on the application thread; takes the Matter lock only to publish. */
void environment_update();
