/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coldtracker_telemetry.hpp"

#include "coldtracker_storage.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(telemetry, LOG_LEVEL_DBG);

namespace
{

constexpr k_timeout_t kTelemetryPeriod = K_SECONDS(1);

} // namespace

TelemetryService::TelemetryService(const char *name, struct k_msgq &event_queue)
	: Service(name, event_queue, kSubscriptions)
{
}

void TelemetryService::on_start()
{
	k_timer_init(&m_timer, timer_handler, nullptr);
	k_timer_user_data_set(&m_timer, this);
}

void TelemetryService::on_event(servz::EventId event)
{
	switch (static_cast<Event>(event)) {
	case Event::NetworkOnline:
		LOG_INF("Network is online, processing pending telemetry");

		m_network_online = true;

		if (!m_processing) {
			process_pending();
		}

		break;

	case Event::NetworkOffline:
		m_network_online = false;
		m_processing = false;

		k_timer_stop(&m_timer);
		break;

	case Event::SampleStored:
		if (m_network_online && !m_processing) {
			process_pending();
		}

		break;

	case Event::TelemetryTick:
		process_pending();
		break;

	default:
		break;
	}
}

void TelemetryService::process_pending()
{
	if (!m_network_online) {
		m_processing = false;
		return;
	}

	struct ColdTrackerSample sample{};

	int ret = coldtracker::storage::peek(sample);

	if (ret == -ENOENT) {
		LOG_DBG("No pending telemetry");
		m_processing = false;
		return;
	}

	if (ret < 0) {
		LOG_ERR("Failed to read pending sample: %d", ret);
		m_processing = false;
		return;
	}

	ret = push_sample(sample);

	if (ret < 0) {
		LOG_ERR("Failed to push sample: %d", ret);
		m_processing = false;
		return;
	}

	ret = coldtracker::storage::commit();

	if (ret < 0) {
		LOG_ERR("Failed to commit sample: %d", ret);
		m_processing = false;
		return;
	}

	m_processing = true;

	k_timer_start(&m_timer, kTelemetryPeriod, K_NO_WAIT);
}

int TelemetryService::push_sample(const struct ColdTrackerSample &sample)
{
	LOG_DBG("Pushing data: %.2f C @ %lld", static_cast<double>(sample.temperature_mc) / 1000.0,
		static_cast<long long>(sample.timestamp));

	/*
	 * TODO: Send the sample to the ColdTracker backend.
	 */

	return 0;
}

void TelemetryService::timer_handler(struct k_timer *timer)
{
	auto *service = static_cast<TelemetryService *>(k_timer_user_data_get(timer));

	if (service == nullptr) {
		return;
	}

	(void)service->post_event(static_cast<servz::EventId>(Event::TelemetryTick));
}

SERVZ_SERVICE_DEFINE(telemetry_service, TelemetryService, 2048, 7, 4);
