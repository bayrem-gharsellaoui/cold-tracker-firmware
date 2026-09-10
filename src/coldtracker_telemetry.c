#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(telemetry, LOG_LEVEL_DBG);

#include "coldtracker_messages.h"
#include "coldtracker_sample.h"
#include "coldtracker_storage.h"

#define TELEMETRY_PERIOD K_SECONDS(1)

ZBUS_SUBSCRIBER_DEFINE(telemetry_subscriber, 4);
ZBUS_CHAN_ADD_OBS(network_status_chan, telemetry_subscriber, 3);

static int push_sample(const struct coldtracker_sample *sample)
{
	if (sample == NULL) {
		LOG_ERR("No valid sample to push");
		return -EINVAL;
	}

	LOG_DBG("Pushing data: %.2f C @ %lld", (double)sample->temperature_mc / 1000.0,
		(long long)sample->timestamp);

	return 0;
}

static int process_sample(const struct coldtracker_sample *sample, void *user_data)
{
	ARG_UNUSED(user_data);

	return push_sample(sample);
}

static void telemetry_thread_entry(void *arg1, void *arg2, void *arg3)
{
	const struct zbus_channel *chan = NULL;
	struct network_status_msg network_status = {0};
	int ret;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		ret = zbus_sub_wait(&telemetry_subscriber, &chan, K_FOREVER);
		if (ret < 0) {
			LOG_ERR("Failed waiting for network state: %d", ret);
			continue;
		}

		if (chan != &network_status_chan) {
			continue;
		}

		ret = zbus_chan_read(chan, &network_status, K_MSEC(100));
		if (ret < 0) {
			LOG_ERR("Failed to read network state: %d", ret);
			continue;
		}

		if (network_status.state == NETWORK_STATE_ONLINE) {
			break;
		}
	}

	LOG_INF("Network is online, processing pending telemetry");

	while (1) {
		struct coldtracker_sample sample = {0};

		ret = storage_peek(&sample);
		if (ret == -ENOENT) {
			LOG_DBG("No pending data to push");
			break;
		}

		if (ret < 0) {
			LOG_ERR("Failed to read pending sample: %d", ret);
			break;
		}

		ret = push_sample(&sample);
		if (ret < 0) {
			LOG_ERR("Failed to push sample: %d", ret);
			break;
		}

		ret = storage_commit();
		if (ret < 0) {
			LOG_ERR("Failed to commit sample: %d", ret);
			break;
		}

		k_sleep(TELEMETRY_PERIOD);
	}
}

K_THREAD_DEFINE(telemetry_thread, 2048, telemetry_thread_entry, NULL, NULL, NULL, 7, 0, 0);
