/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "coldtracker_events.hpp"

#include <servz/service.hpp>

/**
 * @brief Service responsible for OTA update management.
 *
 * The service waits for network connectivity and checks whether a firmware
 * update is available.
 */
class OtaService final: public servz::Service
{
public:
	/**
	 * @brief Construct the OTA service.
	 *
	 * @param name Service name.
	 * @param event_queue Service event queue.
	 */
	OtaService(const char *name, struct k_msgq &event_queue);

protected:
	/**
	 * @brief Handle events delivered to the OTA service.
	 *
	 * @param event Event identifier.
	 */
	void on_event(servz::EventId event) override;

private:
	/**
	 * @brief Check whether a firmware update is available.
	 */
	void check_for_update();

	static constexpr auto kSubscriptions = servz::EventMask::of(Event::NetworkOnline);
};
