#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/clock.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sensing, LOG_LEVEL_DBG);

#include "coldtracker_messages.h"

#define SENSING_PERIOD K_SECONDS(1)

ZBUS_SUBSCRIBER_DEFINE(sensing_subscriber, 4);
ZBUS_CHAN_ADD_OBS(time_status_chan, sensing_subscriber, 3);

static void sensing_take_sample(const struct device *dev)
{
	struct sensor_value temperature = {0};
	struct timespec timestamp = {0};
	int ret;

	ret = sensor_sample_fetch(dev);
	if (ret < 0) {
		LOG_ERR("Failed to fetch sensor sample: %d", ret);
		return;
	}

	ret = sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &temperature);
	if (ret < 0) {
		LOG_ERR("Failed to read temperature: %d", ret);
		return;
	}

	ret = sys_clock_gettime(SYS_CLOCK_REALTIME, &timestamp);
	if (ret < 0) {
		LOG_ERR("Failed to read system time: %d", ret);
		return;
	}

	LOG_DBG("Temperature: %.2f C @ %lld", sensor_value_to_double(&temperature),
		(long long)timestamp.tv_sec);
}

static void sensing_thread_entry(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	const struct device *const dev = DEVICE_DT_GET(DT_ALIAS(coldtracker_temp));
	const struct zbus_channel *chan = NULL;
	struct time_status_msg time_status = {0};
	int ret;

	if (!device_is_ready(dev)) {
		LOG_ERR("%s device is not ready", dev->name);
		return;
	}

	/* 1. Wait until the Time service provides a valid system time */
	while (1) {
		ret = zbus_sub_wait(&sensing_subscriber, &chan, K_FOREVER);
		if (ret < 0) {
			LOG_ERR("Failed waiting for time state: %d", ret);
			continue;
		}

		if (chan != &time_status_chan) {
			continue;
		}

		ret = zbus_chan_read(chan, &time_status, K_MSEC(100));
		if (ret < 0) {
			LOG_ERR("Failed to read time state: %d", ret);
			continue;
		}

		if (time_status.state == TIME_STATE_AVAILABLE) {
			break;
		}
	}

	LOG_INF("Time available (source: %d), starting sensing", time_status.source);

	/* 2. Periodically acquire and timestamp temperature samples */
	while (1) {
		sensing_take_sample(dev);
		k_sleep(SENSING_PERIOD);
	}
}

K_THREAD_DEFINE(sensing_thread, 1024, sensing_thread_entry, NULL, NULL, NULL, 8, 0, 0);
