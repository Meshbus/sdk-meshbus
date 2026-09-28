/* SPDX-FileCopyrightText: FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <clock/clock.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

ZTEST(mbs_clock_shell, test_registered_commands_parse_and_apply_configuration)
{
	const struct shell *sh = shell_backend_dummy_get_ptr();
	mbs_clock_config config;

	zassert_not_null(sh);
	zassert_ok(shell_execute_cmd(sh, "meshbus clock config set 12h -60"));
	zassert_ok(mbs_clock_config_get(&config));
	zassert_equal(config.time_format, meshbus_ClockConfig_ClockTimeFormat_TIME_FORMAT_12H);
	zassert_equal(config.utc_offset_minutes, -60);
	zassert_equal(shell_execute_cmd(sh, "meshbus clock config set 24h invalid"), -EINVAL);
	zassert_ok(mbs_clock_config_get(&config));
	zassert_equal(config.utc_offset_minutes, -60);
	zassert_ok(shell_execute_cmd(sh, "meshbus clock config reset"));
	zassert_ok(mbs_clock_config_get(&config));
	zassert_equal(config.utc_offset_minutes, CONFIG_MBS_CLOCK_DEFAULT_UTC_OFFSET_MINUTES);
}

ZTEST_SUITE(mbs_clock_shell, NULL, NULL, NULL, NULL, NULL);
