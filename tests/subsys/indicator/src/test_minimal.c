/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#include <indicator/indicator.h>
#include <zephyr/ztest.h>

ZTEST(mbs_indicator_minimal, test_configuration_without_optional_services_or_hardware)
{
	mbs_indicator_config config;

	zassert_ok(mbs_indicator_config_get(&config));
	zassert_ok(mbs_indicator_config_set(&config));
	zassert_ok(mbs_indicator_config_reset());
	zassert_false(mbs_indicator_light_is_ready());
	zassert_false(mbs_indicator_buzzer_is_ready());
}

ZTEST_SUITE(mbs_indicator_minimal, NULL, NULL, NULL, NULL, NULL);
