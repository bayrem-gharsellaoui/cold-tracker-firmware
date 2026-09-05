#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/net/sntp.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(time, LOG_LEVEL_DBG);

#include "coldtracker_messages.h"

#define SNTP_SERVER      "pool.ntp.org"
#define SNTP_TIMEOUT_MS  4000
#define SNTP_RETRY_DELAY K_SECONDS(1)

static void time_thread(void *p1, void *p2, void *p3);
static void time_sync_work_handler(struct k_work *work);

static int get_time_from_rtc(const struct device *rtc_dev, struct timespec *time);
static int get_time_from_sntp(struct timespec *time);
static int save_time_to_rtc(const struct device *rtc_dev, const struct timespec *time);
static int format_time(const struct timespec *time, char *buf, size_t buf_size);

K_THREAD_DEFINE(time_thread_id, 2048, time_thread, NULL, NULL, NULL, 5, 0, 0);

K_WORK_DELAYABLE_DEFINE(time_sync_work, time_sync_work_handler);

ZBUS_SUBSCRIBER_DEFINE(time_subscriber, 4);
ZBUS_CHAN_ADD_OBS(network_status_chan, time_subscriber, 3);

static int get_time_from_rtc(const struct device *rtc_dev, struct timespec *time)
{
	struct rtc_time rtc_time = {0};
	time_t timestamp;
	int ret;

	ret = rtc_get_time(rtc_dev, &rtc_time);
	if (ret < 0) {
		return ret;
	}

	timestamp = timeutil_timegm(rtc_time_to_tm(&rtc_time));
	if (timestamp == (time_t)-1) {
		return -EINVAL;
	}

	time->tv_sec = timestamp;
	time->tv_nsec = rtc_time.tm_nsec;

	return 0;
}

static int get_time_from_sntp(struct timespec *time)
{
	struct sntp_time sntp_time = {0};
	int ret;

	ret = sntp_simple(SNTP_SERVER, SNTP_TIMEOUT_MS, &sntp_time);
	if (ret < 0) {
		return ret;
	}

	time->tv_sec = sntp_time.seconds;
	time->tv_nsec = ((uint64_t)sntp_time.fraction * NSEC_PER_SEC) >> 32;

	return 0;
}

static int save_time_to_rtc(const struct device *rtc_dev, const struct timespec *time)
{
	struct rtc_time rtc_time = {0};

	if (gmtime_r(&time->tv_sec, rtc_time_to_tm(&rtc_time)) == NULL) {
		return -EINVAL;
	}

	rtc_time.tm_nsec = time->tv_nsec;

	return rtc_set_time(rtc_dev, &rtc_time);
}

static int format_time(const struct timespec *time, char *buf, size_t buf_size)
{
	struct tm tm = {0};

	if (gmtime_r(&time->tv_sec, &tm) == NULL) {
		return -EINVAL;
	}

	if (strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &tm) == 0) {
		return -ENOMEM;
	}

	return 0;
}

static void time_sync_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	const struct device *rtc_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_rtc));
	struct timespec time = {0};
	char datetime[32] = {0};
	struct time_status_msg time_status = {
		.state = TIME_STATE_AVAILABLE,
		.source = TIME_SOURCE_SNTP,
	};
	int ret;

	LOG_INF("Synchronizing time with %s...", SNTP_SERVER);

	ret = get_time_from_sntp(&time);
	if (ret < 0) {
		LOG_ERR("Failed to get time from SNTP server: %d", ret);
		LOG_WRN("Retrying time synchronization in 1 second...");
		k_work_reschedule(&time_sync_work, SNTP_RETRY_DELAY);
		return;
	}

	ret = sys_clock_settime(SYS_CLOCK_REALTIME, &time);
	if (ret < 0) {
		LOG_ERR("Failed to set system clock: %d", ret);
		LOG_WRN("Retrying time synchronization in 1 second...");
		k_work_reschedule(&time_sync_work, SNTP_RETRY_DELAY);
		return;
	}

	ret = save_time_to_rtc(rtc_dev, &time);
	if (ret < 0) {
		LOG_WRN("Failed to save synchronized time to RTC: %d", ret);
	}

	ret = format_time(&time, datetime, sizeof(datetime));
	if (ret < 0) {
		LOG_ERR("Failed to format time: %d", ret);
	} else {
		LOG_INF("Time synchronized: %s UTC (%lld)", datetime, (long long)time.tv_sec);
	}

	ret = zbus_chan_pub(&time_status_chan, &time_status, K_MSEC(100));
	if (ret < 0) {
		LOG_ERR("Failed to publish time state: %d", ret);
	}
}

static void time_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct device *rtc_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_rtc));
	const struct zbus_channel *chan = NULL;
	struct network_status_msg network_status = {0};
	struct time_status_msg time_status = {
		.state = TIME_STATE_AVAILABLE,
		.source = TIME_SOURCE_RTC,
	};
	struct timespec time = {0};
	char datetime[32] = {0};
	int ret;

	if (!device_is_ready(rtc_dev)) {
		LOG_ERR("RTC device is not ready");
		return;
	}

	/* Try to restore the system clock from the RTC at startup */
	ret = get_time_from_rtc(rtc_dev, &time);
	if (ret < 0) {
		if (ret == -ENODATA) {
			LOG_INF("RTC does not contain valid time");
		} else {
			LOG_ERR("Failed to get time from RTC: %d", ret);
		}
	} else {
		ret = sys_clock_settime(SYS_CLOCK_REALTIME, &time);
		if (ret < 0) {
			LOG_ERR("Failed to restore system clock: %d", ret);
		} else {
			ret = format_time(&time, datetime, sizeof(datetime));
			if (ret < 0) {
				LOG_ERR("Failed to format time: %d", ret);
			} else {
				LOG_INF("Time restored from RTC: %s UTC (%lld)", datetime,
					(long long)time.tv_sec);
			}

			ret = zbus_chan_pub(&time_status_chan, &time_status, K_MSEC(100));
			if (ret < 0) {
				LOG_ERR("Failed to publish time state: %d", ret);
			}
		}
	}

	/* Wait for network state changes */
	while (1) {
		ret = zbus_sub_wait(&time_subscriber, &chan, K_FOREVER);
		if (ret < 0) {
			LOG_ERR("Failed waiting for network state: %d", ret);
			continue;
		}

		if (chan != &network_status_chan) {
			LOG_WRN("Not interested in this channel: %s", chan->name);
			continue;
		}

		ret = zbus_chan_read(chan, &network_status, K_MSEC(100));
		if (ret < 0) {
			LOG_ERR("Failed to read network state: %d", ret);
			continue;
		}

		if (network_status.state == NETWORK_STATE_OFFLINE) {
			LOG_WRN("Network is offline, stopping SNTP retries");
			k_work_cancel_delayable(&time_sync_work);
			continue;
		}

		if (network_status.state != NETWORK_STATE_ONLINE) {
			continue;
		}

		k_work_reschedule(&time_sync_work, K_NO_WAIT);
	}
}
