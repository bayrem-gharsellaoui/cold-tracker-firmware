/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "coldtracker_events.hpp"
#include "coldtracker_sample.hpp"

#include <servz/service.hpp>

#include <zephyr/kernel.h>

/**
 * @brief Service responsible for sending stored telemetry data.
 *
 * Pending samples are sent while network connectivity is available. Samples
 * are committed from storage only after they are successfully transmitted.
 */
class TelemetryService final: public servz::Service
{
public:
	/**
	 * @brief Construct the telemetry service.
	 *
	 * @param name Service name.
	 * @param event_queue Service event queue.
	 */
	TelemetryService(const char *name, struct k_msgq &event_queue);

protected:
	/**
	 * @brief Initialize telemetry processing.
	 */
	void on_start() override;

	/**
	 * @brief Handle network, storage, and telemetry events.
	 *
	 * @param event Event identifier.
	 */
	void on_event(servz::EventId event) override;

private:
	/**
	 * @brief Process the next pending telemetry sample.
	 */
	void process_pending();

	/**
	 * @brief Push one sample to the remote telemetry endpoint.
	 *
	 * @param sample Sample to send.
	 *
	 * @return 0 on success, otherwise a negative errno value.
	 */
	int push_sample(const struct ColdTrackerSample &sample);

	/**
	 * @brief Handle expiration of the telemetry timer.
	 *
	 * @param timer Telemetry timer.
	 */
	static void timer_handler(struct k_timer *timer);

	static constexpr auto kSubscriptions = servz::EventMask::of(
		Event::NetworkOnline, Event::NetworkOffline, Event::SampleStored);

	struct k_timer m_timer{};
	bool m_network_online{};
	bool m_processing{};
};
