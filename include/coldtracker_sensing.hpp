/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "coldtracker_events.hpp"
#include "coldtracker_sample.hpp"

#include <servz/service.hpp>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

/**
 * @brief Service responsible for acquiring temperature samples.
 *
 * Sampling starts once valid system time becomes available. Samples are
 * periodically timestamped and written to storage.
 */
class SensingService final: public servz::Service
{
public:
	/**
	 * @brief Construct the sensing service.
	 *
	 * @param name Service name.
	 * @param event_queue Service event queue.
	 */
	SensingService(const char *name, struct k_msgq &event_queue);

protected:
	/**
	 * @brief Initialize the temperature sensor and sampling timer.
	 */
	void on_start() override;

	/**
	 * @brief Handle time availability and sampling events.
	 *
	 * @param event Event identifier.
	 */
	void on_event(servz::EventId event) override;

private:
	/**
	 * @brief Acquire and timestamp one temperature sample.
	 *
	 * @param sample Destination sample.
	 *
	 * @return 0 on success, otherwise a negative errno value.
	 */
	int take_sample(struct ColdTrackerSample &sample);

	/**
	 * @brief Start periodic sensing.
	 */
	void start_sensing();

	/**
	 * @brief Handle expiration of the sensing timer.
	 *
	 * @param timer Sensing timer.
	 */
	static void timer_handler(struct k_timer *timer);

	static constexpr auto kSubscriptions =
		servz::EventMask::of(Event::TimeAvailableRtc, Event::TimeAvailableSntp);

	const struct device *m_sensor;
	struct k_timer m_timer{};
	bool m_started{};
	int m_remaining_samples{3};
};
