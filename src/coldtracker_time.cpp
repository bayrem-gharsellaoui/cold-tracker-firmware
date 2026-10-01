/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#define _POSIX_C_SOURCE 200809L

#include "coldtracker_time.hpp"

#include <errno.h>
#include <time.h>

#include <servz/event_manager.hpp>

#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>
#include <zephyr/sys/clock.h>

#ifdef CONFIG_RTC
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/timeutil.h>
#endif

LOG_MODULE_REGISTER(time, LOG_LEVEL_DBG);

namespace
{

constexpr char kSntpServer[] = "pool.ntp.org";
constexpr int32_t kSntpTimeoutMs = 4000;
constexpr k_timeout_t kSntpRetryDelay = K_SECONDS(1);

int get_time_from_sntp(struct timespec &time)
{
	struct sntp_time sntp_time{};

	const int ret = sntp_simple(kSntpServer, kSntpTimeoutMs, &sntp_time);

	if (ret < 0) {
		return ret;
	}

	time.tv_sec = sntp_time.seconds;
	time.tv_nsec = (static_cast<uint64_t>(sntp_time.fraction) * NSEC_PER_SEC) >> 32;

	return 0;
}

int format_time(const struct timespec &time, char *buffer, size_t size)
{
	struct tm tm{};

	if (gmtime_r(&time.tv_sec, &tm) == nullptr) {
		return -EINVAL;
	}

	if (strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &tm) == 0U) {
		return -ENOMEM;
	}

	return 0;
}

#ifdef CONFIG_RTC

int get_time_from_rtc(const struct device *device, struct timespec &time)
{
	struct rtc_time rtc_time{};

	const int ret = rtc_get_time(device, &rtc_time);

	if (ret < 0) {
		return ret;
	}

	const time_t timestamp = timeutil_timegm(rtc_time_to_tm(&rtc_time));

	if (timestamp == static_cast<time_t>(-1)) {
		return -EINVAL;
	}

	time.tv_sec = timestamp;
	time.tv_nsec = rtc_time.tm_nsec;

	return 0;
}

int save_time_to_rtc(const struct device *device, const struct timespec &time)
{
	struct rtc_time rtc_time{};

	if (gmtime_r(&time.tv_sec, rtc_time_to_tm(&rtc_time)) == nullptr) {
		return -EINVAL;
	}

	rtc_time.tm_nsec = time.tv_nsec;

	return rtc_set_time(device, &rtc_time);
}

#endif /* CONFIG_RTC */

} // namespace

TimeService::TimeService(const char *name, struct k_msgq &event_queue)
	: Service(name, event_queue, kSubscriptions)
{
}

void TimeService::on_start()
{
	k_timer_init(&m_retry_timer, retry_timer_handler, nullptr);
	k_timer_user_data_set(&m_retry_timer, this);

#ifdef CONFIG_RTC
	const struct device *rtc_device = DEVICE_DT_GET(DT_CHOSEN(zephyr_rtc));

	if (!device_is_ready(rtc_device)) {
		LOG_WRN("RTC device is not ready");
		return;
	}

	struct timespec time{};

	int ret = get_time_from_rtc(rtc_device, time);

	if (ret < 0) {
		LOG_INF("RTC does not contain valid time");
		return;
	}

	ret = sys_clock_settime(SYS_CLOCK_REALTIME, &time);

	if (ret < 0) {
		LOG_ERR("Failed to set system clock: %d", ret);
		return;
	}

	char time_string[32]{};

	ret = format_time(time, time_string, sizeof(time_string));

	if (ret == 0) {
		LOG_INF("Time restored from RTC: %s UTC (%lld)", time_string,
			static_cast<long long>(time.tv_sec));
	}

	(void)servz::EventManager::publish(static_cast<servz::EventId>(Event::TimeAvailableRtc));
#endif
}

void TimeService::on_event(servz::EventId event)
{
	switch (static_cast<Event>(event)) {
	case Event::NetworkOnline:
		synchronize();
		break;

	case Event::NetworkOffline:
		k_timer_stop(&m_retry_timer);
		break;

	case Event::TimeSyncRetry:
		synchronize();
		break;

	default:
		break;
	}
}

void TimeService::synchronize()
{
	LOG_INF("Synchronizing time with %s...", kSntpServer);

	struct timespec time{};

	int ret = get_time_from_sntp(time);

	if (ret < 0) {
		LOG_ERR("Failed to get time from SNTP server: %d", ret);
		schedule_retry();
		return;
	}

	ret = sys_clock_settime(SYS_CLOCK_REALTIME, &time);

	if (ret < 0) {
		LOG_ERR("Failed to set system clock: %d", ret);
		schedule_retry();
		return;
	}

#ifdef CONFIG_RTC
	const struct device *rtc_device = DEVICE_DT_GET(DT_CHOSEN(zephyr_rtc));

	if (device_is_ready(rtc_device)) {
		ret = save_time_to_rtc(rtc_device, time);

		if (ret < 0) {
			LOG_WRN("Failed to save time to RTC: %d", ret);
		}
	}
#endif

	char time_string[32]{};

	ret = format_time(time, time_string, sizeof(time_string));

	if (ret < 0) {
		LOG_WRN("Failed to format synchronized time: %d", ret);
	} else {
		LOG_INF("Time synchronized: %s UTC (%lld)", time_string,
			static_cast<long long>(time.tv_sec));
	}

	(void)servz::EventManager::publish(static_cast<servz::EventId>(Event::TimeAvailableSntp));
}

void TimeService::schedule_retry()
{
	LOG_WRN("Retrying time synchronization in 1 second");

	k_timer_start(&m_retry_timer, kSntpRetryDelay, K_NO_WAIT);
}

void TimeService::retry_timer_handler(struct k_timer *timer)
{
	auto *service = static_cast<TimeService *>(k_timer_user_data_get(timer));

	if (service == nullptr) {
		return;
	}

	(void)service->post_event(static_cast<servz::EventId>(Event::TimeSyncRetry));
}

SERVZ_SERVICE_DEFINE(time_service, TimeService, 2048, 5, 4);
