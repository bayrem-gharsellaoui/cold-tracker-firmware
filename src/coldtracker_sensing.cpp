/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coldtracker_sensing.hpp"

#include "coldtracker_storage.hpp"

#include <time.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>

#include <servz/event_manager.hpp>

LOG_MODULE_REGISTER(sensing, LOG_LEVEL_DBG);

namespace
{

constexpr k_timeout_t kSensingPeriod = K_SECONDS(1);

} // namespace

SensingService::SensingService(const char *name, struct k_msgq &event_queue)
	: Service(name, event_queue, kSubscriptions),
	  m_sensor(DEVICE_DT_GET(DT_ALIAS(coldtracker_temp)))
{
}

void SensingService::on_start()
{
	if (!device_is_ready(m_sensor)) {
		LOG_ERR("%s device is not ready", m_sensor->name);
		return;
	}

	k_timer_init(&m_timer, timer_handler, nullptr);
	k_timer_user_data_set(&m_timer, this);
}

void SensingService::on_event(servz::EventId event)
{
	switch (static_cast<Event>(event)) {
	case Event::TimeAvailableRtc:
	case Event::TimeAvailableSntp:
		if (!m_started) {
			start_sensing();
		}
		break;

	case Event::SensingTick: {
		if (m_remaining_samples <= 0) {
			k_timer_stop(&m_timer);
			break;
		}

		struct ColdTrackerSample sample{};

		const int ret = take_sample(sample);

		if (ret == 0) {
			const int ret = coldtracker::storage::append(sample);

			if (ret < 0) {
				LOG_ERR("Failed to store sample: %d", ret);
			} else {
				(void)servz::EventManager::publish(
					static_cast<servz::EventId>(Event::SampleStored));
			}
		}

		--m_remaining_samples;

		if (m_remaining_samples == 0) {
			k_timer_stop(&m_timer);
		}

		break;
	}

	default:
		break;
	}
}

int SensingService::take_sample(struct ColdTrackerSample &sample)
{
	struct sensor_value temperature{};
	struct timespec timestamp{};

	int ret = sensor_sample_fetch(m_sensor);

	if (ret < 0) {
		LOG_ERR("Failed to fetch sensor sample: %d", ret);
		return ret;
	}

	ret = sensor_channel_get(m_sensor, SENSOR_CHAN_DIE_TEMP, &temperature);

	if (ret < 0) {
		LOG_ERR("Failed to read temperature: %d", ret);
		return ret;
	}

	ret = sys_clock_gettime(SYS_CLOCK_REALTIME, &timestamp);

	if (ret < 0) {
		LOG_ERR("Failed to read system time: %d", ret);
		return ret;
	}

	sample.temperature_mc = sensor_value_to_milli(&temperature);

	sample.timestamp = timestamp.tv_sec;

	LOG_DBG("Temperature: %.2f C @ %lld", sensor_value_to_double(&temperature),
		static_cast<long long>(sample.timestamp));

	return 0;
}

void SensingService::start_sensing()
{
	LOG_INF("Time available, starting sensing");

	m_started = true;

	/*
	 * Trigger the first sample immediately, then continue periodically.
	 */
	k_timer_start(&m_timer, K_NO_WAIT, kSensingPeriod);
}

void SensingService::timer_handler(struct k_timer *timer)
{
	auto *service = static_cast<SensingService *>(k_timer_user_data_get(timer));

	if (service == nullptr) {
		return;
	}

	(void)service->post_event(static_cast<servz::EventId>(Event::SensingTick));
}

SERVZ_SERVICE_DEFINE(sensing_service, SensingService, 1024, 8, 4);
