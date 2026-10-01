/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coldtracker_network.hpp"

#include <servz/event_manager.hpp>

#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>

LOG_MODULE_REGISTER(network, LOG_LEVEL_DBG);

NetworkService::NetworkService(const char *name, struct k_msgq &event_queue)
	: Service(name, event_queue, kSubscriptions), m_backend(backend_connection_failed)
{
}

void NetworkService::on_start()
{
	net_mgmt_init_event_callback(&m_l4_mgmt_cb, l4_event_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);

	net_mgmt_add_event_callback(&m_l4_mgmt_cb);

	conn_mgr_mon_resend_status();

	(void)servz::EventManager::publish(static_cast<servz::EventId>(Event::NetworkConnecting));

	const int ret = m_backend.connect();

	if (ret < 0) {
		LOG_ERR("Failed to initiate network connection: %d", ret);
		(void)servz::EventManager::publish(
			static_cast<servz::EventId>(Event::NetworkOffline));
	}
}

void NetworkService::on_event(servz::EventId event)
{
	switch (static_cast<Event>(event)) {
	case Event::NetworkL4Connected:
		LOG_INF("Network is online");
		(void)servz::EventManager::publish(
			static_cast<servz::EventId>(Event::NetworkOnline));
		break;

	case Event::NetworkL4Disconnected:
		LOG_WRN("Network is offline");
		(void)servz::EventManager::publish(
			static_cast<servz::EventId>(Event::NetworkOffline));
		break;

	default:
		break;
	}
}

void NetworkService::l4_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
				      struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	LOG_INF("L4 event received: 0x%llx", static_cast<unsigned long long>(event));

	if (event == NET_EVENT_L4_CONNECTED) {
		(void)servz::EventManager::publish(
			static_cast<servz::EventId>(Event::NetworkL4Connected));
	} else if (event == NET_EVENT_L4_DISCONNECTED) {
		(void)servz::EventManager::publish(
			static_cast<servz::EventId>(Event::NetworkL4Disconnected));
	}
}

void NetworkService::backend_connection_failed()
{
	(void)servz::EventManager::publish(
		static_cast<servz::EventId>(Event::NetworkL4Disconnected));
}

SERVZ_SERVICE_DEFINE(network_service, NetworkService, 2048, 5, 4);
