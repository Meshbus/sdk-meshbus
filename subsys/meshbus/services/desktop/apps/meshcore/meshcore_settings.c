/* SPDX-License-Identifier: Apache-2.0 */

#include "meshcore_private.h"

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "text/desktop_text.h"

static const meshbus_meshcore_loop_detect meshcore_loop_detect_values[] = {
	MESHBUS_MESHCORE_LOOP_DETECT_OFF,
	MESHBUS_MESHCORE_LOOP_DETECT_MINIMAL,
	MESHBUS_MESHCORE_LOOP_DETECT_MODERATE,
	MESHBUS_MESHCORE_LOOP_DETECT_STRICT,
};

static const char *const meshcore_path_hash_options[] = {
	"1",
	"2",
	"3",
};

static const char *const meshcore_telemetry_mode_text[] = {
	DESKTOP_TEXT_MESHCORE_TELEMETRY_MODE_DENY,
	DESKTOP_TEXT_MESHCORE_TELEMETRY_MODE_ALL,
};

static bool meshcore_role_is_repeater(meshbus_meshcore_role role)
{
	return role == MESHBUS_MESHCORE_ROLE_REPEATER;
}

static size_t meshcore_role_to_index(meshbus_meshcore_role role)
{
	if (role < MESHBUS_MESHCORE_ROLE_CHAT || role > MESHBUS_MESHCORE_ROLE_SENSOR) {
		return 0U;
	}

	return (size_t)(role - MESHBUS_MESHCORE_ROLE_CHAT);
}

static uint8_t meshcore_sanitize_path_hash_size(uint8_t hash_size)
{
	if (hash_size < 1U || hash_size > MESHBUS_MESHCORE_PATH_HASH_SIZE_MAX) {
		return 1U;
	}

	return hash_size;
}

static float meshcore_sanitize_delay_factor(float factor)
{
	if (!(factor == factor) || factor < 0.0f) {
		return 0.0f;
	}
	if (factor > 2.0f) {
		return 2.0f;
	}

	return factor;
}

static uint16_t meshcore_delay_factor_to_x100(float factor)
{
	factor = meshcore_sanitize_delay_factor(factor);
	return (uint16_t)(factor * 100.0f + 0.5f);
}

static void meshcore_format_delay_factor(char *buf, size_t buf_size, float factor)
{
	uint16_t value = meshcore_delay_factor_to_x100(factor);

	if (buf == NULL || buf_size == 0U) {
		return;
	}

	(void)snprintk(buf, buf_size, "%u.%02u", (unsigned int)(value / 100U),
		       (unsigned int)(value % 100U));
}

static bool meshcore_telemetry_mode_valid(meshbus_MeshcoreConfig_TelemetryMode mode)
{
	return mode >= meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_DENY &&
	       mode <= meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_ALLOW_ALL;
}

static bool meshcore_telemetry_mode_enabled(meshbus_MeshcoreConfig_TelemetryMode mode)
{
	return mode != meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_DENY;
}

static meshbus_MeshcoreConfig_TelemetryMode
meshcore_telemetry_mode_from_enabled(size_t option, meshbus_MeshcoreConfig_TelemetryMode current,
				     meshbus_MeshcoreConfig_TelemetryMode default_enabled)
{
	if (option == 0U) {
		return meshbus_MeshcoreConfig_TelemetryMode_TELEMETRY_DENY;
	}
	if (meshcore_telemetry_mode_enabled(current)) {
		return current;
	}

	return default_enabled;
}

void meshcore_settings_sanitize(meshbus_meshcore_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	cfg->name[sizeof(cfg->name) - 1U] = '\0';
	if (cfg->public_key.size > sizeof(cfg->public_key.bytes)) {
		cfg->public_key.size = sizeof(cfg->public_key.bytes);
	}
	if (cfg->private_key.size > sizeof(cfg->private_key.bytes)) {
		cfg->private_key.size = sizeof(cfg->private_key.bytes);
	}
	cfg->multi_acks = (uint8_t)CLAMP((uint32_t)cfg->multi_acks, 0U, 255U);
	cfg->flood_max = (uint8_t)CLAMP((uint32_t)cfg->flood_max, 0U, 255U);
	cfg->path_hash_size = meshcore_sanitize_path_hash_size((uint8_t)cfg->path_hash_size);
	if (cfg->loop_detect < MESHBUS_MESHCORE_LOOP_DETECT_OFF ||
	    cfg->loop_detect > MESHBUS_MESHCORE_LOOP_DETECT_STRICT) {
		cfg->loop_detect = MESHBUS_MESHCORE_LOOP_DETECT_OFF;
	}
	if (!meshcore_telemetry_mode_valid(cfg->telemetry_mode_base)) {
		cfg->telemetry_mode_base = MESHBUS_MESHCORE_TELEMETRY_MODE_ALL;
	}
	if (!meshcore_telemetry_mode_valid(cfg->telemetry_mode_locat)) {
		cfg->telemetry_mode_locat = MESHBUS_MESHCORE_TELEMETRY_MODE_ALL;
	}
	if (!meshcore_telemetry_mode_valid(cfg->telemetry_mode_environment)) {
		cfg->telemetry_mode_environment = MESHBUS_MESHCORE_TELEMETRY_MODE_FLAGS;
	}
	cfg->tx_delay_factor = meshcore_sanitize_delay_factor(cfg->tx_delay_factor);
	cfg->direct_tx_delay_factor = meshcore_sanitize_delay_factor(cfg->direct_tx_delay_factor);
	cfg->add_contact_hops_limit =
		(uint8_t)CLAMP((uint32_t)cfg->add_contact_hops_limit, 0U,
			       MESHCORE_CONTACT_ADD_HOPS_LIMIT_MAX);
	cfg->add_contact_config &= 0xFFU;
	cfg->add_contact_config |= MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE;
}
void meshcore_reload_node(struct meshcore_app *app)
{
	meshbus_meshcore_config cfg = meshbus_MeshcoreConfig_init_zero;

	if (app == NULL) {
		return;
	}

	if (meshbus_meshcore_config_get(&cfg) != 0) {
		cfg = (meshbus_meshcore_config)meshbus_MeshcoreConfig_init_zero;
	}
	meshcore_settings_sanitize(&cfg);
	app->applied = cfg;
	app->editing = cfg;
	app->settings_page = MESHCORE_SETTINGS_PAGE_ROOT;
	app->settings_root_selected = 0U;
}
static size_t meshcore_loop_detect_to_index(meshbus_meshcore_loop_detect value)
{
	for (size_t i = 0U; i < ARRAY_SIZE(meshcore_loop_detect_values); i++) {
		if (meshcore_loop_detect_values[i] == value) {
			return i;
		}
	}

	return 0U;
}

static void meshcore_form_add_value(struct meshcore_app *app, uint32_t id, const char *label,
				    const char *value)
{
	if (app == NULL || app->form_item_count >= MESHCORE_FORM_ITEMS_MAX) {
		return;
	}

	app->form_items[app->form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.value_text = value,
	};
}

static void meshcore_form_add_route(struct meshcore_app *app, uint32_t id, const char *label)
{
	if (app == NULL || app->form_item_count >= MESHCORE_FORM_ITEMS_MAX) {
		return;
	}

	app->form_items[app->form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.value_text = DESKTOP_TEXT_COMMON_ROUTE,
		.value_align = ZUI_FORM_VALUE_ALIGN_RIGHT,
	};
}

static void meshcore_form_add_option(struct meshcore_app *app, uint32_t id, const char *label,
				     const char *const *options, size_t count, size_t selected)
{
	if (app == NULL || app->form_item_count >= MESHCORE_FORM_ITEMS_MAX) {
		return;
	}

	app->form_items[app->form_item_count++] = (struct zui_form_item){
		.id = id,
		.label = label,
		.options = options,
		.option_count = count,
		.option_index = selected,
	};
}

static void meshcore_update_settings_form(struct meshcore_app *app, const char *title)
{
	(void)zui_form_update(app->settings_form, &(struct zui_form_config){
						      .title = title,
						      .items = app->form_items,
						      .item_count = app->form_item_count,
						      .changed = NULL,
						      .activated = meshcore_settings_activated,
						      .user_data = app,
					      });
}

static void meshcore_select_settings_form(struct meshcore_app *app, size_t index)
{
	if (app == NULL || zui_form_count(app->settings_form) == 0U) {
		return;
	}

	(void)zui_form_select(app->settings_form,
			      MIN(index, zui_form_count(app->settings_form) - 1U));
}

static void meshcore_restore_root_settings_selection(struct meshcore_app *app)
{
	meshcore_select_settings_form(app, app != NULL ? app->settings_root_selected : 0U);
}

static void meshcore_build_root_settings_form(struct meshcore_app *app)
{
	size_t value_idx = 0U;
	char *buf;

	if (app == NULL) {
		return;
	}

	meshcore_settings_sanitize(&app->editing);
	app->form_item_count = 0U;
	app->settings_page = MESHCORE_SETTINGS_PAGE_ROOT;

	meshcore_form_add_value(app, MESHCORE_FORM_NAME, DESKTOP_TEXT_MESHCORE_SETTINGS_NAME,
				app->editing.name[0] != '\0' ? app->editing.name
							     : DESKTOP_TEXT_COMMON_NONE);
	meshcore_form_add_option(
		app, MESHCORE_FORM_ROLE, DESKTOP_TEXT_MESHCORE_SETTINGS_ROLE,
		DESKTOP_TEXT_MESHCORE_ROLE_VALUES, DESKTOP_TEXT_MESHCORE_ROLE_COUNT,
		meshcore_role_to_index(app->editing.role));

	buf = meshcore_value_buf(app, &value_idx);
	meshcore_hex_short(app->editing.public_key.bytes, app->editing.public_key.size, buf,
			   MESHCORE_VALUE_BUF_SIZE, 4U);
	meshcore_form_add_value(app, MESHCORE_FORM_PUBLIC_KEY,
				DESKTOP_TEXT_MESHCORE_SETTINGS_PUBLIC_KEY, buf);

	buf = meshcore_value_buf(app, &value_idx);
	(void)snprintk(buf, MESHCORE_VALUE_BUF_SIZE, "%u", (unsigned int)app->editing.multi_acks);
	meshcore_form_add_value(app, MESHCORE_FORM_MULTI_ACKS,
				DESKTOP_TEXT_MESHCORE_SETTINGS_MULTI_ACKS, buf);

	meshcore_form_add_option(
		app, MESHCORE_FORM_ADVERT_POSITION, DESKTOP_TEXT_MESHCORE_SETTINGS_ADVERT_POSITION,
		DESKTOP_TEXT_COMMON_NO_YES_VALUES, 2U, app->editing.advert_position ? 1U : 0U);

	meshcore_form_add_option(
		app, MESHCORE_FORM_PATH_HASH_SIZE, DESKTOP_TEXT_MESHCORE_SETTINGS_PATH_HASH_SIZE,
		meshcore_path_hash_options, ARRAY_SIZE(meshcore_path_hash_options),
		meshcore_sanitize_path_hash_size(app->editing.path_hash_size) - 1U);

	meshcore_form_add_route(app, MESHCORE_FORM_CONTACT_ADD_POLICY,
				DESKTOP_TEXT_MESHCORE_SETTINGS_CONTACT_ADD_POLICY);
	meshcore_form_add_route(app, MESHCORE_FORM_FORWARDING,
				DESKTOP_TEXT_MESHCORE_SETTINGS_FORWARDING);
	meshcore_form_add_route(app, MESHCORE_FORM_TELEMETRY_MODE,
				DESKTOP_TEXT_MESHCORE_SETTINGS_TELEMETRY_MODE);

	if (meshcore_role_is_repeater(app->editing.role)) {
		buf = meshcore_value_buf(app, &value_idx);
		(void)snprintk(buf, MESHCORE_VALUE_BUF_SIZE,
			       DESKTOP_TEXT_POWER_VALUE_SECONDS_FORMAT,
			       (unsigned int)app->editing.advert_interval);
		meshcore_form_add_value(app, MESHCORE_FORM_ADVERT_INTERVAL,
					DESKTOP_TEXT_MESHCORE_SETTINGS_ADVERT_INTERVAL, buf);

		buf = meshcore_value_buf(app, &value_idx);
		(void)snprintk(buf, MESHCORE_VALUE_BUF_SIZE,
			       DESKTOP_TEXT_POWER_VALUE_SECONDS_FORMAT,
			       (unsigned int)app->editing.flood_advert_interval);
		meshcore_form_add_value(app, MESHCORE_FORM_FLOOD_ADVERT,
					DESKTOP_TEXT_MESHCORE_SETTINGS_FLOOD_ADVERT_INTERVAL, buf);
	}

	meshcore_form_add_value(app, MESHCORE_FORM_RESET, DESKTOP_TEXT_MESHCORE_ACTION_RESET, NULL);
	meshcore_form_add_value(app, MESHCORE_FORM_APPLY,
		DESKTOP_TEXT_COMMON_ACTION_APPLY, NULL);

	meshcore_update_settings_form(app, DESKTOP_TEXT_MESHCORE_TITLE);
}

static void meshcore_build_contact_add_policy_form(struct meshcore_app *app)
{
	size_t value_idx = 0U;
	char *buf;

	if (app == NULL) {
		return;
	}

	meshcore_settings_sanitize(&app->editing);
	app->editing.add_contact_config |= MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE;
	app->form_item_count = 0U;
	app->settings_page = MESHCORE_SETTINGS_PAGE_CONTACT_ADD_POLICY;

	meshcore_form_add_option(app, MESHCORE_FORM_ADD_CONTACT_OVERWRITE,
				 DESKTOP_TEXT_MESHCORE_SETTINGS_ADD_CONTACT_OVERWRITE,
				 DESKTOP_TEXT_COMMON_NO_YES_VALUES, 2U,
				 (app->editing.add_contact_config &
				  MESHBUS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST) != 0U);
	meshcore_form_add_option(
		app, MESHCORE_FORM_ADD_CONTACT_CHAT, DESKTOP_TEXT_MESHCORE_SETTINGS_ADD_CONTACT_CHAT,
		DESKTOP_TEXT_COMMON_NO_YES_VALUES, 2U,
		(app->editing.add_contact_config & MESHBUS_MESHCORE_CONTACT_ADD_FILTER_CHAT) != 0U);
	meshcore_form_add_option(
		app, MESHCORE_FORM_ADD_CONTACT_REPEATER,
		DESKTOP_TEXT_MESHCORE_SETTINGS_ADD_CONTACT_REPEATER, DESKTOP_TEXT_COMMON_NO_YES_VALUES,
		2U, (app->editing.add_contact_config & MESHBUS_MESHCORE_CONTACT_ADD_FILTER_REPEATER) != 0U);
	meshcore_form_add_option(
		app, MESHCORE_FORM_ADD_CONTACT_ROOM, DESKTOP_TEXT_MESHCORE_SETTINGS_ADD_CONTACT_ROOM,
		DESKTOP_TEXT_COMMON_NO_YES_VALUES, 2U,
		(app->editing.add_contact_config & MESHBUS_MESHCORE_CONTACT_ADD_FILTER_ROOM) != 0U);
	meshcore_form_add_option(
		app, MESHCORE_FORM_ADD_CONTACT_SENSOR, DESKTOP_TEXT_MESHCORE_SETTINGS_ADD_CONTACT_SENSOR,
		DESKTOP_TEXT_COMMON_NO_YES_VALUES, 2U,
		(app->editing.add_contact_config & MESHBUS_MESHCORE_CONTACT_ADD_FILTER_SENSOR) != 0U);

	buf = meshcore_value_buf(app, &value_idx);
	(void)snprintk(buf, MESHCORE_VALUE_BUF_SIZE, "%u",
		       (unsigned int)app->editing.add_contact_hops_limit);
	meshcore_form_add_value(app, MESHCORE_FORM_ADD_CONTACT_HOPS,
				DESKTOP_TEXT_MESHCORE_SETTINGS_ADD_CONTACT_HOPS_LIMIT, buf);

	meshcore_update_settings_form(app, DESKTOP_TEXT_MESHCORE_SETTINGS_CONTACT_ADD_POLICY_TITLE);
}

static void meshcore_build_forwarding_form(struct meshcore_app *app)
{
	size_t value_idx = 0U;
	char *buf;

	if (app == NULL) {
		return;
	}

	meshcore_settings_sanitize(&app->editing);
	app->form_item_count = 0U;
	app->settings_page = MESHCORE_SETTINGS_PAGE_FORWARDING;

	if (meshcore_role_is_repeater(app->editing.role)) {
		meshcore_form_add_option(
			app, MESHCORE_FORM_DISABLE_FWD, DESKTOP_TEXT_MESHCORE_SETTINGS_DISABLE_FWD,
			DESKTOP_TEXT_COMMON_NO_YES_VALUES, 2U, app->editing.disable_fwd ? 1U : 0U);
		meshcore_form_add_option(
			app, MESHCORE_FORM_LOOP_DETECT, DESKTOP_TEXT_MESHCORE_SETTINGS_LOOP_DETECT,
			DESKTOP_TEXT_MESHCORE_LOOP_DETECT_VALUES,
			DESKTOP_TEXT_MESHCORE_LOOP_DETECT_COUNT,
			meshcore_loop_detect_to_index(app->editing.loop_detect));

		buf = meshcore_value_buf(app, &value_idx);
		(void)snprintk(buf, MESHCORE_VALUE_BUF_SIZE, "%u",
			       (unsigned int)app->editing.flood_max);
		meshcore_form_add_value(app, MESHCORE_FORM_FLOOD_MAX,
					DESKTOP_TEXT_MESHCORE_SETTINGS_FLOOD_MAX, buf);

		buf = meshcore_value_buf(app, &value_idx);
		meshcore_format_delay_factor(buf, MESHCORE_VALUE_BUF_SIZE,
					     app->editing.tx_delay_factor);
		meshcore_form_add_value(app, MESHCORE_FORM_TX_DELAY_FACTOR,
					DESKTOP_TEXT_MESHCORE_SETTINGS_TX_DELAY_FACTOR, buf);

		buf = meshcore_value_buf(app, &value_idx);
		meshcore_format_delay_factor(buf, MESHCORE_VALUE_BUF_SIZE,
					     app->editing.direct_tx_delay_factor);
		meshcore_form_add_value(app, MESHCORE_FORM_DIRECT_TX_DELAY,
					DESKTOP_TEXT_MESHCORE_SETTINGS_DIRECT_TX_DELAY_FACTOR, buf);
	} else {
		meshcore_form_add_option(
			app, MESHCORE_FORM_CLIENT_REPEAT, DESKTOP_TEXT_MESHCORE_SETTINGS_CLIENT_REPEAT,
			DESKTOP_TEXT_COMMON_NO_YES_VALUES, 2U, app->editing.client_repeat ? 1U : 0U);
	}

	meshcore_update_settings_form(app, DESKTOP_TEXT_MESHCORE_SETTINGS_FORWARDING_TITLE);
}

static void meshcore_build_telemetry_mode_form(struct meshcore_app *app)
{
	if (app == NULL) {
		return;
	}

	meshcore_settings_sanitize(&app->editing);
	app->form_item_count = 0U;
	app->settings_page = MESHCORE_SETTINGS_PAGE_TELEMETRY_MODE;

	meshcore_form_add_option(
		app, MESHCORE_FORM_TELEMETRY_BASE,
		DESKTOP_TEXT_MESHCORE_SETTINGS_TELEMETRY_MODE_BASE, meshcore_telemetry_mode_text,
		ARRAY_SIZE(meshcore_telemetry_mode_text),
		meshcore_telemetry_mode_enabled(app->editing.telemetry_mode_base) ? 1U : 0U);
	meshcore_form_add_option(
		app, MESHCORE_FORM_TELEMETRY_LOCAT,
		DESKTOP_TEXT_MESHCORE_SETTINGS_TELEMETRY_MODE_LOCAT, meshcore_telemetry_mode_text,
		ARRAY_SIZE(meshcore_telemetry_mode_text),
		meshcore_telemetry_mode_enabled(app->editing.telemetry_mode_locat) ? 1U : 0U);
	meshcore_form_add_option(
		app, MESHCORE_FORM_TELEMETRY_ENV,
		DESKTOP_TEXT_MESHCORE_SETTINGS_TELEMETRY_MODE_ENVIRONMENT,
		meshcore_telemetry_mode_text, ARRAY_SIZE(meshcore_telemetry_mode_text),
		meshcore_telemetry_mode_enabled(app->editing.telemetry_mode_environment) ? 1U : 0U);

	meshcore_update_settings_form(app, DESKTOP_TEXT_MESHCORE_SETTINGS_TELEMETRY_MODE_TITLE);
}

void meshcore_build_settings_form(struct meshcore_app *app)
{
	if (app == NULL) {
		return;
	}

	switch (app->settings_page) {
	case MESHCORE_SETTINGS_PAGE_CONTACT_ADD_POLICY:
		meshcore_build_contact_add_policy_form(app);
		break;
	case MESHCORE_SETTINGS_PAGE_FORWARDING:
		meshcore_build_forwarding_form(app);
		break;
	case MESHCORE_SETTINGS_PAGE_TELEMETRY_MODE:
		meshcore_build_telemetry_mode_form(app);
		break;
	case MESHCORE_SETTINGS_PAGE_ROOT:
	default:
		meshcore_build_root_settings_form(app);
		break;
	}
}

static void meshcore_enter_settings_subpage(struct meshcore_app *app,
					    enum meshcore_settings_page page)
{
	if (app == NULL) {
		return;
	}

	app->settings_root_selected = zui_form_selected(app->settings_form);
	switch (page) {
	case MESHCORE_SETTINGS_PAGE_CONTACT_ADD_POLICY:
		meshcore_build_contact_add_policy_form(app);
		break;
	case MESHCORE_SETTINGS_PAGE_FORWARDING:
		meshcore_build_forwarding_form(app);
		break;
	case MESHCORE_SETTINGS_PAGE_TELEMETRY_MODE:
		meshcore_build_telemetry_mode_form(app);
		break;
	default:
		return;
	}
	meshcore_select_settings_form(app, 0U);
}

static void meshcore_update_bool_bit(uint8_t *field, uint8_t bit, size_t option)
{
	if (field == NULL) {
		return;
	}

	if (option != 0U) {
		*field |= bit;
	} else {
		*field &= ~bit;
	}
}

static void meshcore_sync_root_settings_from_form(struct meshcore_app *app)
{
	app->editing.role = zui_form_option(app->settings_form, MESHCORE_FORM_ROLE) + 1U;
	app->editing.advert_position =
		zui_form_option(app->settings_form, MESHCORE_FORM_ADVERT_POSITION) != 0U;
	app->editing.path_hash_size =
		(uint8_t)(zui_form_option(app->settings_form, MESHCORE_FORM_PATH_HASH_SIZE) + 1U);
}

static void meshcore_sync_contact_add_policy_from_form(struct meshcore_app *app)
{
	app->editing.add_contact_config |= MESHBUS_MESHCORE_CONTACT_ADD_FILTER_MANUAL_MODE;
	meshcore_update_bool_bit(
		&app->editing.add_contact_config, MESHBUS_MESHCORE_CONTACT_ADD_FILTER_OVERWRITE_OLDEST,
		zui_form_option(app->settings_form, MESHCORE_FORM_ADD_CONTACT_OVERWRITE));
	meshcore_update_bool_bit(&app->editing.add_contact_config, MESHBUS_MESHCORE_CONTACT_ADD_FILTER_CHAT,
				 zui_form_option(app->settings_form, MESHCORE_FORM_ADD_CONTACT_CHAT));
	meshcore_update_bool_bit(
		&app->editing.add_contact_config, MESHBUS_MESHCORE_CONTACT_ADD_FILTER_REPEATER,
		zui_form_option(app->settings_form, MESHCORE_FORM_ADD_CONTACT_REPEATER));
	meshcore_update_bool_bit(
		&app->editing.add_contact_config, MESHBUS_MESHCORE_CONTACT_ADD_FILTER_ROOM,
		zui_form_option(app->settings_form, MESHCORE_FORM_ADD_CONTACT_ROOM));
	meshcore_update_bool_bit(
		&app->editing.add_contact_config, MESHBUS_MESHCORE_CONTACT_ADD_FILTER_SENSOR,
		zui_form_option(app->settings_form, MESHCORE_FORM_ADD_CONTACT_SENSOR));
}

static void meshcore_sync_forwarding_from_form(struct meshcore_app *app)
{
	if (meshcore_role_is_repeater(app->editing.role)) {
		app->editing.disable_fwd =
			zui_form_option(app->settings_form, MESHCORE_FORM_DISABLE_FWD) != 0U;
		app->editing.loop_detect =
			meshcore_loop_detect_values[zui_form_option(app->settings_form,
								    MESHCORE_FORM_LOOP_DETECT) %
						    ARRAY_SIZE(meshcore_loop_detect_values)];
	} else {
		app->editing.client_repeat =
			zui_form_option(app->settings_form, MESHCORE_FORM_CLIENT_REPEAT) != 0U;
	}
}

static void meshcore_sync_telemetry_mode_from_form(struct meshcore_app *app)
{
	app->editing.telemetry_mode_base =
		meshcore_telemetry_mode_from_enabled(
			zui_form_option(app->settings_form, MESHCORE_FORM_TELEMETRY_BASE),
			app->editing.telemetry_mode_base, MESHBUS_MESHCORE_TELEMETRY_MODE_ALL);
	app->editing.telemetry_mode_locat =
		meshcore_telemetry_mode_from_enabled(
			zui_form_option(app->settings_form, MESHCORE_FORM_TELEMETRY_LOCAT),
			app->editing.telemetry_mode_locat, MESHBUS_MESHCORE_TELEMETRY_MODE_ALL);
	app->editing.telemetry_mode_environment =
		meshcore_telemetry_mode_from_enabled(
			zui_form_option(app->settings_form, MESHCORE_FORM_TELEMETRY_ENV),
			app->editing.telemetry_mode_environment,
			MESHBUS_MESHCORE_TELEMETRY_MODE_FLAGS);
}

void meshcore_sync_settings_from_form(struct meshcore_app *app)
{
	if (app == NULL) {
		return;
	}

	switch (app->settings_page) {
	case MESHCORE_SETTINGS_PAGE_CONTACT_ADD_POLICY:
		meshcore_sync_contact_add_policy_from_form(app);
		break;
	case MESHCORE_SETTINGS_PAGE_FORWARDING:
		meshcore_sync_forwarding_from_form(app);
		break;
	case MESHCORE_SETTINGS_PAGE_TELEMETRY_MODE:
		meshcore_sync_telemetry_mode_from_form(app);
		break;
	case MESHCORE_SETTINGS_PAGE_ROOT:
	default:
		meshcore_sync_root_settings_from_form(app);
		break;
	}
	meshcore_settings_sanitize(&app->editing);
}
void meshcore_settings_work(struct k_work *work)
{
	struct meshcore_app *app = CONTAINER_OF(work, struct meshcore_app, settings_work);
	bool reset = app->settings_reset;
	int rc = reset ? meshbus_meshcore_config_reset() :
			 meshbus_meshcore_config_set(&app->settings_candidate);

	meshcore_apply_toast(app, rc == 0, reset ? DESKTOP_TEXT_MESHCORE_RESET_DONE : NULL);
	atomic_clear(&app->settings_busy);
}

static void meshcore_submit_settings(struct meshcore_app *app, bool reset)
{
	if (app == NULL || !atomic_cas(&app->settings_busy, 0, 1)) {
		return;
	}
	meshcore_sync_settings_from_form(app);
	app->settings_candidate = app->editing;
	app->settings_reset = reset;
	if (k_work_submit(&app->settings_work) < 0) {
		atomic_clear(&app->settings_busy);
		meshcore_apply_toast(app, false, NULL);
		return;
	}
	meshcore_switch(app, MESHCORE_SCREEN_MENU);
}

void meshcore_apply_settings(struct meshcore_app *app)
{
	meshcore_submit_settings(app, false);
}

void meshcore_reset_settings(struct meshcore_app *app)
{
	meshcore_submit_settings(app, true);
}

void meshcore_settings_activated(struct zui_form *form, uint32_t id,
				 const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	ARG_UNUSED(form);

	if (app == NULL) {
		return;
	}

	meshcore_sync_settings_from_form(app);
	switch (id) {
	case MESHCORE_FORM_NAME:
		meshcore_open_name_editor(app, MESHCORE_TEXT_NODE_NAME,
					  DESKTOP_TEXT_MESHCORE_SETTINGS_NAME, app->editing.name,
					  MESHCORE_SCREEN_SETTINGS);
		break;
	case MESHCORE_FORM_PUBLIC_KEY:
		meshcore_show_detail(app, DESKTOP_TEXT_MESHCORE_PUBLIC_KEY_TITLE,
				     app->editing.public_key.bytes, app->editing.public_key.size);
		break;
	case MESHCORE_FORM_CONTACT_ADD_POLICY:
		meshcore_enter_settings_subpage(app, MESHCORE_SETTINGS_PAGE_CONTACT_ADD_POLICY);
		break;
	case MESHCORE_FORM_FORWARDING:
		meshcore_enter_settings_subpage(app, MESHCORE_SETTINGS_PAGE_FORWARDING);
		break;
	case MESHCORE_FORM_TELEMETRY_MODE:
		meshcore_enter_settings_subpage(app, MESHCORE_SETTINGS_PAGE_TELEMETRY_MODE);
		break;
	case MESHCORE_FORM_MULTI_ACKS:
		meshcore_open_number(app, MESHCORE_NUMBER_MULTI_ACKS,
				     DESKTOP_TEXT_MESHCORE_NUMBER_INPUT_MULTI_ACKS,
				     app->editing.multi_acks, 0, 255);
		break;
	case MESHCORE_FORM_ADD_CONTACT_HOPS:
		meshcore_open_number(app, MESHCORE_NUMBER_CONTACT_ADD_HOPS_LIMIT,
				     DESKTOP_TEXT_MESHCORE_NUMBER_INPUT_CONTACT_ADD_HOPS_LIMIT,
				     app->editing.add_contact_hops_limit, 0,
				     MESHCORE_CONTACT_ADD_HOPS_LIMIT_MAX);
		break;
	case MESHCORE_FORM_FLOOD_MAX:
		meshcore_open_number(app, MESHCORE_NUMBER_FLOOD_MAX,
				     DESKTOP_TEXT_MESHCORE_NUMBER_INPUT_FLOOD_MAX,
				     app->editing.flood_max, 0, 255);
		break;
	case MESHCORE_FORM_TX_DELAY_FACTOR:
		meshcore_open_number(app, MESHCORE_NUMBER_TX_DELAY_FACTOR_X100,
				     DESKTOP_TEXT_MESHCORE_NUMBER_INPUT_TX_DELAY_FACTOR,
				     meshcore_delay_factor_to_x100(app->editing.tx_delay_factor),
				     0, 200);
		break;
	case MESHCORE_FORM_DIRECT_TX_DELAY:
		meshcore_open_number(
			app, MESHCORE_NUMBER_DIRECT_TX_DELAY_FACTOR_X100,
			DESKTOP_TEXT_MESHCORE_NUMBER_INPUT_DIRECT_TX_DELAY_FACTOR,
			meshcore_delay_factor_to_x100(app->editing.direct_tx_delay_factor),
			0, 200);
		break;
	case MESHCORE_FORM_ADVERT_INTERVAL:
		meshcore_open_number(app, MESHCORE_NUMBER_ADVERT_INTERVAL,
				     DESKTOP_TEXT_MESHCORE_NUMBER_INPUT_ADVERT_INTERVAL,
				     app->editing.advert_interval, 0, 86400);
		break;
	case MESHCORE_FORM_FLOOD_ADVERT:
		meshcore_open_number(app, MESHCORE_NUMBER_FLOOD_ADVERT_INTERVAL,
				     DESKTOP_TEXT_MESHCORE_NUMBER_INPUT_FLOOD_ADVERT_INTERVAL,
				     app->editing.flood_advert_interval, 0, 86400);
		break;
	case MESHCORE_FORM_APPLY:
		meshcore_apply_settings(app);
		break;
	case MESHCORE_FORM_RESET:
		meshcore_show_modal(app, MESHCORE_MODAL_RESET,
				    DESKTOP_TEXT_MESHCORE_RESET_CONFIRM_TITLE,
				    DESKTOP_TEXT_MESHCORE_RESET_CONFIRM_TEXT,
				    DESKTOP_TEXT_COMMON_CANCEL, DESKTOP_TEXT_COMMON_OK);
		break;
	default:
		if (app->settings_page == MESHCORE_SETTINGS_PAGE_ROOT && meshcore_is_long(event)) {
			meshcore_apply_settings(app);
		}
		break;
	}
}
static void meshcore_form_draw(struct zui_draw_ctx *draw, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		(void)zui_screen_draw(zui_form_get_screen(app->settings_form), draw);
	}
}

static bool meshcore_form_input(const struct zui_input_event *event, void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app == NULL) {
		return false;
	}
	if (meshcore_should_consume_edge(event)) {
		return true;
	}
	if (meshcore_is_click(event) && event->code == ZUI_INPUT_CODE_BACK) {
		meshcore_sync_settings_from_form(app);
		if (app->settings_page == MESHCORE_SETTINGS_PAGE_ROOT) {
			app->editing = app->applied;
			meshcore_switch(app, MESHCORE_SCREEN_MENU);
		} else {
			app->settings_page = MESHCORE_SETTINGS_PAGE_ROOT;
			meshcore_build_settings_form(app);
			meshcore_restore_root_settings_selection(app);
			meshcore_request_redraw(app);
		}
		return true;
	}
	if (app->settings_page == MESHCORE_SETTINGS_PAGE_ROOT && meshcore_is_long(event) &&
	    event->code == ZUI_INPUT_CODE_SELECT) {
		meshcore_apply_settings(app);
		return true;
	}
	meshbus_meshcore_role previous_role = app->editing.role;

	if (meshcore_forward_input(zui_form_get_screen(app->settings_form), event)) {
		meshcore_sync_settings_from_form(app);
		if (app->settings_page == MESHCORE_SETTINGS_PAGE_ROOT &&
		    previous_role != app->editing.role) {
			app->settings_root_selected = zui_form_selected(app->settings_form);
			meshcore_build_settings_form(app);
			meshcore_restore_root_settings_selection(app);
			meshcore_request_redraw(app);
		}
		return true;
	}
	return false;
}

static void meshcore_form_enter(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_enter(zui_form_get_screen(app->settings_form));
	}
}

static void meshcore_form_exit(void *user_data)
{
	struct meshcore_app *app = user_data;

	if (app != NULL) {
		meshcore_forward_exit(zui_form_get_screen(app->settings_form));
	}
}

const struct zui_screen_ops meshcore_settings_ops = {
	.draw = meshcore_form_draw,
	.input = meshcore_form_input,
	.enter = meshcore_form_enter,
	.exit = meshcore_form_exit,
};
