#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(time, LOG_LEVEL_DBG);

K_SEM_DEFINE(network_ready, 0, 1);

static int read_rtc(const struct device *rtc_dev, struct timespec *tspec)
{
	struct rtc_time rtctime = {0};
	time_t time;
	int ret;

	ret = rtc_get_time(rtc_dev, &rtctime);
	if (ret < 0) {
		return ret;
	}

	LOG_INF("RTC: %04d-%02d-%02d %02d:%02d:%02d", rtctime.tm_year + 1900, rtctime.tm_mon + 1,
		rtctime.tm_mday, rtctime.tm_hour, rtctime.tm_min, rtctime.tm_sec);

	time = timeutil_timegm(rtc_time_to_tm(&rtctime));
	if (time == (time_t)-1) {
		return -EINVAL;
	}

	tspec->tv_sec = time;
	tspec->tv_nsec = rtctime.tm_nsec;

	return 0;
}

static int update_rtc(const struct device *rtc_dev, const struct timespec *tspec)
{
	struct rtc_time rtctime = {0};
	int ret;

	if (gmtime_r(&tspec->tv_sec, rtc_time_to_tm(&rtctime)) == NULL) {
		return -EINVAL;
	}

	rtctime.tm_nsec = tspec->tv_nsec;

	ret = rtc_set_time(rtc_dev, &rtctime);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static void network_event_handler(uint64_t mgmt_event, struct net_if *iface, void *info,
				  size_t info_length, void *user_data)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(info);
	ARG_UNUSED(info_length);
	ARG_UNUSED(user_data);

	switch (mgmt_event) {
	case NET_EVENT_L4_CONNECTED:
		k_sem_give(&network_ready);
		break;
	default:
		break;
	}
}

NET_MGMT_REGISTER_EVENT_HANDLER(time_network_handler, NET_EVENT_L4_CONNECTED, network_event_handler,
				NULL);

static void time_thread(void *p1, void *p2, void *p3)
{
	const struct device *rtc_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_rtc));
	struct sntp_time sntp_time = {0};
	struct timespec tspec = {0};
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!device_is_ready(rtc_dev)) {
		LOG_ERR("RTC device is not ready");
		return;
	}

	/* Restore system time from RTC */
	ret = read_rtc(rtc_dev, &tspec);
	if (ret == 0) {
		ret = sys_clock_settime(SYS_CLOCK_REALTIME, &tspec);
		if (ret < 0) {
			LOG_ERR("Failed to set system clock: %d", ret);
		}
	} else if (ret != -ENODATA) {
		LOG_ERR("Failed to read from rtc device: %d", ret);
	}

	k_sem_take(&network_ready, K_FOREVER);

	IF_ENABLED(CONFIG_NET_PPP, (
		/* Allow PPP host networking to settle */
		k_msleep(5);
	))

	/* Synchronize system time from SNTP */
	ret = sntp_simple("pool.ntp.org", 4000, &sntp_time);
	if (ret < 0) {
		LOG_ERR("SNTP request failed: %d", ret);
		return;
	}

	tspec.tv_sec = sntp_time.seconds;
	tspec.tv_nsec = ((uint64_t)sntp_time.fraction * NSEC_PER_SEC) >> 32;

	ret = sys_clock_settime(SYS_CLOCK_REALTIME, &tspec);
	if (ret < 0) {
		LOG_ERR("Failed to set system clock: %d", ret);
		return;
	}

	LOG_INF("System clock synchronized via SNTP");

	ret = update_rtc(rtc_dev, &tspec);
	if (ret < 0) {
		LOG_ERR("Failed to update rtc: %d", ret);
		return;
	}

	while (1) {
		k_msleep(1000);
	}
}

K_THREAD_DEFINE(time_thread_id, 2048, time_thread, NULL, NULL, NULL, 5, 0, 0);
