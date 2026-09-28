/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
/* No physical modem exists in this fixture. Never allow a hardware operation. */
#include <zephyr/ztest.h>
#include "lbm_common.h"

void lbm_driver_antenna_configure(const struct device *dev, enum lbm_modem_mode mode)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(mode);
	zassert_unreachable("Runtime must remain paused without a radio device");
}
