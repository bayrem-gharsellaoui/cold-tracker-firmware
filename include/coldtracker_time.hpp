/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "coldtracker_events.hpp"

#include <servz/service.hpp>

#include <zephyr/kernel.h>

/**
 * @brief Service responsible for maintaining the system time.
 *
 * Restores time from the RTC when available and synchronizes the system clock
 * with an SNTP server when network connectivity becomes available.
 */
class TimeService final: public servz::Service
{
public:
	/**
	 * @brief Construct the time service.
	 *
	 * @param name Service name.
	 * @param event_queue Service event queue.
	 */
	TimeService(const char *name, struct k_msgq &event_queue);

protected:
	/**
	 * @brief Initialize time keeping and restore time from the RTC.
	 */
	void on_start() override;

	/**
	 * @brief Handle network and time synchronization events.
	 *
	 * @param event Event identifier.
	 */
	void on_event(servz::EventId event) override;

private:
	/**
	 * @brief Synchronize the system clock using SNTP.
	 */
	void synchronize();

	/**
	 * @brief Schedule another synchronization attempt.
	 */
	void schedule_retry();

	/**
	 * @brief Handle expiration of the synchronization retry timer.
	 *
	 * @param timer Retry timer.
	 */
	static void retry_timer_handler(struct k_timer *timer);

	static constexpr auto kSubscriptions =
		servz::EventMask::of(Event::NetworkOnline, Event::NetworkOffline);

	struct k_timer m_retry_timer{};
};
