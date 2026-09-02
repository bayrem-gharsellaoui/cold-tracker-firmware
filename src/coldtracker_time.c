#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/sntp.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(time, LOG_LEVEL_DBG);

#include "coldtracker_network.h"

void time_restore_from_rtc(void)
{
	const struct device *rtc_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_rtc));
	struct rtc_time rtctime;
	struct timespec tspec;
	int ret;

	if (!device_is_ready(rtc_dev)) {
		LOG_ERR("RTC device %s is not ready", rtc_dev->name);
		return;
	}

	ret = rtc_get_time(rtc_dev, &rtctime);
	if (ret < 0) {
		LOG_WRN("Cannot read date time from RTC (%d)", ret);
		return;
	}

	tspec.tv_sec = timeutil_timegm(rtc_time_to_tm(&rtctime));
	tspec.tv_nsec = rtctime.tm_nsec;

	ret = sys_clock_settime(SYS_CLOCK_REALTIME, &tspec);
	if (ret < 0) {
		LOG_ERR("Failed to set system clock: %d", ret);
	} else {
		LOG_INF("System clock successfully restored from RTC");
	}

	LOG_INF("RTC date and time: %04d-%02d-%02d %02d:%02d:%02d", rtctime.tm_year + 1900,
		rtctime.tm_mon + 1, rtctime.tm_mday, rtctime.tm_hour, rtctime.tm_min,
		rtctime.tm_sec);
}

static void sntp_set_rtc(const struct timespec *tspec)
{
	const struct device *rtc_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_rtc));
	struct rtc_time rtctime;
	int ret;

	if (!device_is_ready(rtc_dev)) {
		return;
	}

	if (gmtime_r(&tspec->tv_sec, rtc_time_to_tm(&rtctime)) == NULL) {
		LOG_ERR("gmtime_r failed");
		return;
	}
	rtctime.tm_nsec = tspec->tv_nsec;

	ret = rtc_set_time(rtc_dev, &rtctime);
	if (ret != 0) {
		LOG_ERR("rtc_set_time failed: %d", ret);
	} else {
		LOG_INF("RTC updated from SNTP");
	}
}

static void sntp_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	struct sntp_time ts = {0};
	struct timespec tspec = {0};
	int ret;

	ret = sntp_simple("pool.ntp.org", 4000, &ts);
	if (ret < 0) {
		LOG_ERR("SNTP request failed: %d", ret);
		return;
	}

	tspec.tv_sec = ts.seconds;
	tspec.tv_nsec = ((uint64_t)ts.fraction * NSEC_PER_SEC) >> 32;

	ret = sys_clock_settime(SYS_CLOCK_REALTIME, &tspec);
	if (ret < 0) {
		LOG_ERR("sys_clock_settime failed: %d", ret);
		return;
	}

	LOG_INF("System clock synced via SNTP (epoch %" PRIu64 ")", ts.seconds);

	sntp_set_rtc(&tspec);
}

static K_WORK_DELAYABLE_DEFINE(sntp_work, sntp_work_handler);

static bool l4_connected;
static bool dns_ready;

static void maybe_sync_sntp(void)
{
	if (l4_connected && dns_ready) {
		LOG_INF("Network and DNS ready - scheduling SNTP sync");
		k_work_reschedule(&sntp_work, K_MSEC(500));
	}
}

static void l4_event_handler(uint64_t mgmt_event, struct net_if *iface, void *info,
			     size_t info_length, void *user_data)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(info);
	ARG_UNUSED(info_length);
	ARG_UNUSED(user_data);

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		LOG_DBG("NET_EVENT_L4_CONNECTED");
		l4_connected = true;
		maybe_sync_sntp();
	} else if (mgmt_event == NET_EVENT_DNS_SERVERS_RECONFIGURED) {
		LOG_DBG("NET_EVENT_DNS_SERVERS_RECONFIGURED");
		dns_ready = true;
		maybe_sync_sntp();
	} else if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		LOG_DBG("NET_EVENT_L4_DISCONNECTED");
		l4_connected = false;
		dns_ready = false;
		k_work_cancel_delayable(&sntp_work);
	}
}

NET_MGMT_REGISTER_EVENT_HANDLER(time_l4_handler,
				NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED |
					NET_EVENT_DNS_SERVERS_RECONFIGURED,
				l4_event_handler, NULL);
