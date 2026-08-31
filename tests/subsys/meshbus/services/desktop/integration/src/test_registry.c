/* SPDX-License-Identifier: Apache-2.0 */

/* Desktop registry and lifecycle integration coverage. */

#include "desktop_private.h"
#include "registry/apps_registry_prvi.h"
#include "registry/dashboard_widgets_registry.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/meshbus/desktop.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(meshbus_desktop);

#define TEST_APP_STACK_SIZE 1024

static K_THREAD_STACK_DEFINE(shared_app_stack, 2048);
static K_SEM_DEFINE(exit_requested_sem, 0, 1);
static K_SEM_DEFINE(allow_thread_return_sem, 0, 1);

static struct zui_desktop test_desktop;
static meshbus_desktop_app_handle_t app_a_handle;
static meshbus_desktop_app_handle_t app_b_handle;
static meshbus_desktop_app_handle_t expected_active_handle;
static bool active_when_exit_requested;
static int external_cleanup_count;
static void *expected_user_data;

static void short_app_main(void *arg)
{
	struct meshbus_desktop_app_args *args = arg;
	const struct meshbus_desktop_app_desc *desc;

	zassert_ok(meshbus_desktop_app_registry_resolve_handle(expected_active_handle, &desc));
	zassert_not_null(args);
	zassert_equal(args->host, test_desktop.host);
	zassert_equal(args->user_data, expected_user_data);
	zassert_str_equal(args->app_id, desc->id);
	zassert_str_equal(args->display_name, desc->display_name);
	/* The mutable app arguments do not transfer cleanup-context ownership. */
	args->user_data = NULL;
}

MESHBUS_DESKTOP_APP_DEFINE("registry-test-a", "Registry test A", short_app_main,
			   TEST_APP_STACK_SIZE, NULL, 0);
MESHBUS_DESKTOP_APP_DEFINE("registry-test-b", "Registry test B", short_app_main,
			   TEST_APP_STACK_SIZE, NULL, 1);
MESHBUS_DESKTOP_APP_DEFINE("registry-test-a", "Duplicate registry test A", short_app_main,
			   TEST_APP_STACK_SIZE, NULL, 2);

static struct zui_screen *test_widget_screen_create(
	struct meshbus_desktop_dashboard_widget *ctx)
{
	return (struct zui_screen *)ctx;
}

static uint32_t test_widget_tick(struct meshbus_desktop_dashboard_widget *ctx)
{
	ARG_UNUSED(ctx);
	return 1000U;
}

static struct zui_screen *test_widget_nine_screen_create(
	struct meshbus_desktop_dashboard_widget *ctx);
static struct zui_screen *test_widget_two_screen_create(
	struct meshbus_desktop_dashboard_widget *ctx);
static struct zui_screen *test_widget_seven_screen_create(
	struct meshbus_desktop_dashboard_widget *ctx);
static struct zui_screen *test_widget_duplicate_screen_create(
	struct meshbus_desktop_dashboard_widget *ctx);

MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(9, "Nine", test_widget_nine_screen_create,
					  test_widget_tick, "registry-test-a");
MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(2, "Two", test_widget_two_screen_create,
					  test_widget_tick, NULL);
MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(7, "Seven", test_widget_seven_screen_create,
					  test_widget_tick, "registry-test-b");
MESHBUS_DESKTOP_DASHBOARD_WIDGETS_DEFINE(9, "Duplicate nine",
					  test_widget_duplicate_screen_create,
					  test_widget_tick, NULL);

static struct zui_screen *test_widget_nine_screen_create(
	struct meshbus_desktop_dashboard_widget *ctx)
{
	return test_widget_screen_create(ctx);
}

static struct zui_screen *test_widget_two_screen_create(
	struct meshbus_desktop_dashboard_widget *ctx)
{
	return test_widget_screen_create(ctx);
}

static struct zui_screen *test_widget_seven_screen_create(
	struct meshbus_desktop_dashboard_widget *ctx)
{
	return test_widget_screen_create(ctx);
}

static struct zui_screen *test_widget_duplicate_screen_create(
	struct meshbus_desktop_dashboard_widget *ctx)
{
	return test_widget_screen_create(ctx);
}

STRUCT_SECTION_ITERABLE(meshbus_desktop_dashboard_widget_desc,
			invalid_widget_desc) = {
	.index = 1,
	.title = NULL,
	.screen_create = NULL,
	.tick = NULL,
	.app_id = NULL,
};

void zui_desktop_request_app_exit(struct zui_desktop *desktop)
{
	zassert_equal(desktop, &test_desktop, "unexpected desktop instance");
	active_when_exit_requested =
		desktop_app_registry_is_running(expected_active_handle);
	k_sem_give(&exit_requested_sem);
	zassert_ok(k_sem_take(&allow_thread_return_sem, K_SECONDS(1)),
		   "test did not release app thread");
}

static void *desktop_registry_setup(void)
{
	app_a_handle = meshbus_desktop_app_registry_handle_from_id("registry-test-a");
	app_b_handle = meshbus_desktop_app_registry_handle_from_id("registry-test-b");
	zassert_true(meshbus_desktop_app_handle_is_valid(app_a_handle), "app A not registered");
	zassert_true(meshbus_desktop_app_handle_is_valid(app_b_handle), "app B not registered");

	test_desktop.host = (struct zui_host *)&test_desktop;
	test_desktop.app_shared_stack = shared_app_stack;
	test_desktop.app_shared_stack_size = K_THREAD_STACK_SIZEOF(shared_app_stack);

	return NULL;
}

static void desktop_registry_before(void *fixture)
{
	ARG_UNUSED(fixture);

	active_when_exit_requested = false;
	expected_user_data = NULL;
	k_sem_reset(&exit_requested_sem);
	k_sem_reset(&allow_thread_return_sem);
}

static void start_and_complete(meshbus_desktop_app_handle_t handle)
{
	active_when_exit_requested = false;
	k_sem_reset(&exit_requested_sem);
	k_sem_reset(&allow_thread_return_sem);
	expected_active_handle = handle;

	zassert_ok(desktop_app_registry_start(&test_desktop, handle),
		   "failed to start app handle 0x%08x", (unsigned int)handle);
	zassert_ok(k_sem_take(&exit_requested_sem, K_SECONDS(1)),
		   "app did not reach exit request");
	zassert_true(active_when_exit_requested,
		     "app became idle before its thread was joined");
	k_sem_give(&allow_thread_return_sem);
	zassert_ok(desktop_app_registry_complete_exit(handle, K_SECONDS(1)),
		   "failed to join app handle 0x%08x", (unsigned int)handle);
}

static void external_cleanup(void *user_data)
{
	int *cleanup_count = user_data;

	(*cleanup_count)++;
	zassert_true(desktop_app_registry_is_running(expected_active_handle));
	zassert_equal(desktop_app_registry_complete_exit(expected_active_handle, K_NO_WAIT),
		      -EBUSY, "a second reaper entered cleanup");
	zassert_equal(desktop_app_registry_start(&test_desktop, app_b_handle), -EBUSY,
		      "another app started before cleanup finished");
	zassert_equal(desktop_app_registry_external_release(expected_active_handle), -EBUSY,
		      "external descriptor was freed during cleanup");
}

ZTEST(desktop_registry_lifecycle, test_public_registry_and_handle_boundaries)
{
	const struct meshbus_desktop_app_desc *desc = (void *)UINTPTR_MAX;
	meshbus_desktop_app_handle_t handle;
	meshbus_desktop_app_handle_t wrong_epoch;

	zassert_equal(meshbus_desktop_app_registry_count(), 2U,
		      "duplicate static app ID was retained");
	zassert_is_null(meshbus_desktop_app_registry_get(2U),
			"out-of-range app lookup succeeded");
	zassert_equal(meshbus_desktop_app_registry_get_handle(2U),
		      MESHBUS_DESKTOP_APP_HANDLE_INVALID,
		      "out-of-range handle lookup succeeded");
	zassert_equal(meshbus_desktop_app_registry_handle_from_id(NULL),
		      MESHBUS_DESKTOP_APP_HANDLE_INVALID,
		      "NULL app ID resolved");
	zassert_equal(meshbus_desktop_app_registry_handle_from_id("missing"),
		      MESHBUS_DESKTOP_APP_HANDLE_INVALID,
		      "unknown app ID resolved");
	zassert_is_null(meshbus_desktop_app_registry_get_by_id(NULL),
			"NULL app ID returned a descriptor");

	handle = meshbus_desktop_app_registry_get_handle(0U);
	zassert_true(meshbus_desktop_app_handle_is_valid(handle), "valid app handle rejected");
	zassert_ok(meshbus_desktop_app_registry_resolve_handle(handle, &desc));
	zassert_not_null(desc, "valid handle returned NULL descriptor");
	zassert_equal(meshbus_desktop_app_registry_resolve_handle(handle, NULL), -EINVAL,
		      "NULL output was accepted");

	desc = (void *)UINTPTR_MAX;
	zassert_equal(meshbus_desktop_app_registry_resolve_handle(0U, &desc), -EINVAL,
		      "zero handle was accepted");
	zassert_is_null(desc, "failed resolve did not clear output");
	wrong_epoch = MESHBUS_DESKTOP_APP_HANDLE_MAKE(
		(uint8_t)(MESHBUS_DESKTOP_APP_HANDLE_GET_EPOCH(handle) + 1U),
		MESHBUS_DESKTOP_APP_HANDLE_GET_INDEX_PLUS1(handle));
	zassert_equal(meshbus_desktop_app_registry_resolve_handle(wrong_epoch, &desc), -ENOENT,
		      "stale-epoch handle was accepted");
	zassert_false(desktop_app_registry_is_running(wrong_epoch),
		      "stale-epoch handle reported running");
}

ZTEST(desktop_registry_lifecycle, test_external_descriptor_boundaries_and_recovery)
{
	meshbus_desktop_app_handle_t handle = UINT32_MAX;
	meshbus_desktop_app_handle_t prepared_handle;
	struct meshbus_desktop_external_app_desc desc = {
		.id = "registry-test-external-boundary",
		.display_name = "External boundary",
		.app_main = short_app_main,
		.stack_size = TEST_APP_STACK_SIZE,
	};

	zassert_equal(desktop_app_registry_external_prepare(NULL, &handle), -EINVAL,
		      "NULL external descriptor was accepted");
	desc.id = "registry-test-a";
	zassert_equal(desktop_app_registry_external_prepare(&desc, &handle), -EEXIST,
		      "static app ID collision was accepted");
	desc.id = "registry-test-external-boundary";
	desc.stack_size = CONFIG_MESHBUS_DESKTOP_APP_SHARED_STACK_SIZE + 1U;
	zassert_equal(desktop_app_registry_external_prepare(&desc, &handle), -EINVAL,
		      "oversized app stack was accepted");
	desc.stack_size = TEST_APP_STACK_SIZE;
	zassert_equal(desktop_app_registry_external_prepare(&desc, NULL), -EINVAL,
		      "NULL handle output was accepted");

	zassert_ok(desktop_app_registry_external_prepare(&desc, &handle));
	prepared_handle = handle;
	zassert_true(desktop_app_registry_is_external_handle(handle),
		     "external handle was not identified");
	zassert_equal(desktop_app_registry_external_prepare(&desc, &handle), -EEXIST,
		      "duplicate external ID was accepted");
	desc.id = "another-external";
	zassert_equal(desktop_app_registry_external_prepare(&desc, &handle), -EBUSY,
		      "second external slot was accepted");
	zassert_ok(desktop_app_registry_external_release(prepared_handle));

	desc.id = "registry-test-external-boundary";
	zassert_ok(desktop_app_registry_external_prepare(&desc, &handle),
		   "registry did not recover after release");
	zassert_ok(desktop_app_registry_external_release(handle));
}

ZTEST(desktop_registry_lifecycle, test_widget_registry_filters_duplicates_and_sorts)
{
	static const uint16_t expected_indices[] = {2U, 7U, 9U};

	zassert_equal(meshbus_desktop_dashboard_widgets_count(), ARRAY_SIZE(expected_indices),
		      "invalid or duplicate widget was retained");
	for (size_t i = 0U; i < ARRAY_SIZE(expected_indices); i++) {
		struct meshbus_desktop_dashboard_widget *widget =
			meshbus_desktop_dashboard_widgets_get(i);

		zassert_not_null(widget, "missing widget %u", (unsigned int)i);
		zassert_not_null(widget->desc, "missing descriptor %u", (unsigned int)i);
		zassert_equal(widget->desc->index, expected_indices[i],
			      "widget registry is not sorted/unique");
		zassert_not_null(widget->desc->title, "widget title missing");
		zassert_not_null(widget->desc->screen_create, "widget factory missing");
	}
	zassert_is_null(meshbus_desktop_dashboard_widgets_get(ARRAY_SIZE(expected_indices)),
			"out-of-range widget lookup succeeded");
}

ZTEST(desktop_registry_lifecycle, test_app_remains_active_until_thread_is_joined)
{
	int join_ret;
	int second_start_ret = -ECANCELED;

	expected_active_handle = app_a_handle;
	zassert_ok(desktop_app_registry_start(&test_desktop, app_a_handle),
		   "failed to start app A");
	zassert_ok(k_sem_take(&exit_requested_sem, K_SECONDS(1)),
		   "app A did not reach exit request");

	join_ret = desktop_app_registry_complete_exit(app_a_handle, K_NO_WAIT);
	if (active_when_exit_requested && join_ret != 0) {
		second_start_ret = desktop_app_registry_start(&test_desktop, app_b_handle);
	}
	zassert_equal(desktop_app_registry_start(&test_desktop, app_a_handle), -EALREADY);
	zassert_false(desktop_app_registry_is_running(app_b_handle));
	zassert_ok(desktop_app_registry_complete_exit(app_b_handle, K_NO_WAIT));
	k_sem_give(&allow_thread_return_sem);

	zassert_true(active_when_exit_requested,
		     "app became idle while its thread still owned the shared stack");
	zassert_not_equal(join_ret, 0,
			  "join completed before the app thread returned");
	zassert_equal(second_start_ret, -EBUSY,
		      "app B reused the stack before app A was joined");
	zassert_equal(desktop_app_registry_detach_desktop_all(&test_desktop), -EBUSY,
		      "Desktop detached while app A still owned the shared stack");
	zassert_ok(desktop_app_registry_complete_exit(app_a_handle, K_SECONDS(1)),
		   "failed to join app A");
	zassert_false(desktop_app_registry_is_running(app_a_handle),
		      "app A remained active after join");
	zassert_ok(desktop_app_registry_complete_exit(app_a_handle, K_NO_WAIT),
		   "completing an idle app did not remain idempotent");

	for (int i = 0; i < 8; i++) {
		start_and_complete((i & 1) == 0 ? app_b_handle : app_a_handle);
	}
}

ZTEST(desktop_registry_lifecycle, test_start_failure_keeps_session_available)
{
	k_thread_stack_t *stack = test_desktop.app_shared_stack;
	size_t stack_size = test_desktop.app_shared_stack_size;

	test_desktop.app_shared_stack = NULL;
	zassert_equal(desktop_app_registry_start(&test_desktop, app_a_handle), -ENOMEM);
	zassert_false(desktop_app_registry_is_running(app_a_handle));
	test_desktop.app_shared_stack = stack;
	test_desktop.app_shared_stack_size = TEST_APP_STACK_SIZE - 1U;
	zassert_equal(desktop_app_registry_start(&test_desktop, app_a_handle), -EINVAL);
	zassert_false(desktop_app_registry_is_running(app_a_handle));
	test_desktop.app_shared_stack_size = stack_size;
	start_and_complete(app_a_handle);
}

ZTEST(desktop_registry_lifecycle, test_external_cleanup_and_release_follow_join)
{
	meshbus_desktop_app_handle_t handle = MESHBUS_DESKTOP_APP_HANDLE_INVALID;
	const struct meshbus_desktop_external_app_desc desc = {
		.id = "registry-test-external",
		.display_name = "Registry test external",
		.app_main = short_app_main,
		.stack_size = TEST_APP_STACK_SIZE,
		.user_data = &external_cleanup_count,
		.cleanup = external_cleanup,
	};

	external_cleanup_count = 0;
	start_and_complete(app_a_handle);
	zassert_ok(desktop_app_registry_external_prepare(&desc, &handle),
		   "failed to prepare external app");

	expected_active_handle = handle;
	expected_user_data = &external_cleanup_count;
	zassert_ok(desktop_app_registry_start(&test_desktop, handle),
		   "failed to start external app");
	zassert_ok(k_sem_take(&exit_requested_sem, K_SECONDS(1)),
		   "external app did not reach exit request");
	zassert_equal(desktop_app_registry_external_release(handle), -EBUSY,
		      "external app was released before join");
	zassert_equal(external_cleanup_count, 0,
		      "external cleanup ran before join");

	k_sem_give(&allow_thread_return_sem);
	zassert_ok(desktop_app_registry_complete_exit(handle, K_SECONDS(1)),
		   "failed to join external app");
	zassert_equal(external_cleanup_count, 1,
		      "external cleanup did not run exactly once after join");
	zassert_ok(desktop_app_registry_external_release(handle),
		   "failed to release joined external app");
	expected_user_data = NULL;
	start_and_complete(app_b_handle);
}

ZTEST_SUITE(desktop_registry_lifecycle, NULL, desktop_registry_setup,
	    desktop_registry_before, NULL, NULL);
