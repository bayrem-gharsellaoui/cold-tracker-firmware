/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "coldtracker_events.hpp"
#include "network_backend.hpp"

#include <servz/service.hpp>

#include <zephyr/net/net_mgmt.h>

/**
 * @brief Service responsible for managing ColdTracker network connectivity.
 *
 * The service starts the configured network backend and translates Zephyr
 * network management events into application-level servz events.
 */
class NetworkService final: public servz::Service
{
public:
	/**
	 * @brief Construct the network service.
	 *
	 * @param name Service name.
	 * @param event_queue Service event queue.
	 */
	NetworkService(const char *name, struct k_msgq &event_queue);

protected:
	/**
	 * @brief Initialize network monitoring and start the network backend.
	 */
	void on_start() override;

	/**
	 * @brief Handle network events delivered to the service.
	 *
	 * @param event Event identifier.
	 */
	void on_event(servz::EventId event) override;

private:
	/**
	 * @brief Handle Zephyr L4 network state changes.
	 *
	 * @param cb Network management callback.
	 * @param event Network management event.
	 * @param iface Network interface associated with the event.
	 */
	static void l4_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
				     struct net_if *iface);

	/**
	 * @brief Handle an asynchronous backend connection failure.
	 */
	static void backend_connection_failed();

	static constexpr auto kSubscriptions =
		servz::EventMask::of(Event::NetworkL4Connected, Event::NetworkL4Disconnected);

	struct net_mgmt_event_callback m_l4_mgmt_cb{};
	NetworkBackend m_backend;
};
