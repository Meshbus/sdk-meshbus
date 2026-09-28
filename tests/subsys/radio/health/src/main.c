/* SPDX-FileCopyrightText: 2026 FoBE Studio */
/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <zephyr/drivers/lora.h>
#include <radio/radio.h>

static int send_error;
static int receive_error;
static lora_recv_cb receive_cb;
static void *receive_data;
static int fake_config(const struct device *dev, const struct lora_modem_config *cfg)
{
	ARG_UNUSED(dev); ARG_UNUSED(cfg); return 0;
}
static uint32_t fake_airtime(const struct device *dev, uint32_t len)
{
	ARG_UNUSED(dev); ARG_UNUSED(len); return 1;
}
static int fake_send(const struct device *dev, uint8_t *data, uint32_t len,
		     struct k_poll_signal *signal)
{
	ARG_UNUSED(dev); ARG_UNUSED(data); ARG_UNUSED(len);
	if (!send_error) {
		k_poll_signal_raise(signal, 0);
	}
	return send_error;
}
static int fake_receive(const struct device *dev, lora_recv_cb cb, void *data)
{
	ARG_UNUSED(dev);
	if (cb) {
		receive_cb = cb;
		receive_data = data;
		return receive_error;
	}
	return 0;
}
static DEVICE_API(lora, fake_api) = {
	.config = fake_config, .airtime = fake_airtime,
	.send_async = fake_send, .recv_async = fake_receive,
};
DEVICE_DT_DEFINE(DT_NODELABEL(test_radio), NULL, NULL, NULL, NULL,
	POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &fake_api);
K_SEM_DEFINE(done, 0, 1);
static void tx_done(const struct zbus_channel *chan)
{
	ARG_UNUSED(chan); k_sem_give(&done);
}
ZBUS_LISTENER_DEFINE(test_done, tx_done);
ZBUS_CHAN_ADD_OBS(mbs_radio_tx_done_chan, test_done, 1);
static void transmit(int status)
{
	send_error = status;
	struct mbs_radio_publish_event event = {.data = {1}, .len = 1};
	k_sem_reset(&done);
	zassert_ok(zbus_chan_pub(&mbs_radio_publish_chan, &event, K_MSEC(50)));
	zassert_ok(k_sem_take(&done, K_MSEC(500)), "no TX completion");
}
static struct mbs_radio_health_event health(void)
{
	struct mbs_radio_health_event event;
	zassert_ok(mbs_radio_health_get(&event));
	return event;
}
static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	send_error = 0;
	receive_error = 0;
	zassert_ok(mbs_radio_config_reset());
}
ZTEST(radio_health, test_driver_failure_threshold_and_policy_rejections)
{
	zassert_true(health().ready);
	transmit(-EBUSY);
	transmit(-ECANCELED);
	transmit(-EINVAL);
	zassert_false(health().tx_failed);
	transmit(-EIO);
	transmit(-EIO);
	zassert_false(health().tx_failed);
	transmit(-EIO);
	zassert_true(health().tx_failed);
	transmit(0);
	zassert_false(health().tx_failed);
}
ZTEST(radio_health, test_tx_success_does_not_hide_failed_receiver)
{
	receive_error = -EIO;
	transmit(0);
	zassert_true(health().rx_failed);
	zassert_false(health().tx_failed);
	receive_error = -EBUSY;
	transmit(0);
	zassert_true(health().rx_failed);
	uint8_t packet[] = {1};
	receive_cb(DEVICE_DT_GET(DT_NODELABEL(test_radio)), packet, 1, -80, 4, receive_data);
	zassert_false(health().rx_failed);
}
ZTEST_SUITE(radio_health, NULL, NULL, before, NULL, NULL);
