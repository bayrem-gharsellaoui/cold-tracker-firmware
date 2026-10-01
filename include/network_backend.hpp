/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/net/net_if.h>

#ifdef CONFIG_WIFI
#include <zephyr/net/net_mgmt.h>
#endif

/**
 * @brief Network transport backend.
 *
 * Provides the platform-specific network connection implementation used by
 * NetworkService. The active transport is selected at build time.
 */
class NetworkBackend
{
public:
	/**
	 * @brief Connection failure callback type.
	 */
	using ConnectionFailureHandler = void (*)();

	/**
	 * @brief Construct the network backend.
	 *
	 * @param failure_handler Callback invoked when an asynchronous connection
	 * failure occurs.
	 */
	explicit NetworkBackend(ConnectionFailureHandler failure_handler);

	/**
	 * @brief Start the configured network connection.
	 *
	 * @return 0 on success, otherwise a negative errno value.
	 */
	int connect();

private:
#ifdef CONFIG_WIFI
	/**
	 * @brief Start a Wi-Fi connection.
	 *
	 * @param iface Network interface.
	 *
	 * @return 0 on success, otherwise a negative errno value.
	 */
	int wifi_connect(struct net_if *iface);

	/**
	 * @brief Handle Wi-Fi management events.
	 *
	 * @param cb Network management callback.
	 * @param event Wi-Fi management event.
	 * @param iface Network interface associated with the event.
	 */
	static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
				       struct net_if *iface);

	struct net_mgmt_event_callback m_wifi_mgmt_cb{};
#endif

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
	/**
	 * @brief Start the USB network connection.
	 *
	 * @param iface Network interface.
	 *
	 * @return 0 on success, otherwise a negative errno value.
	 */
	static int usb_connect(struct net_if *iface);
#endif

#ifdef CONFIG_NET_PPP
	/**
	 * @brief Start the PPP network connection.
	 *
	 * @param iface Network interface.
	 *
	 * @return 0 on success, otherwise a negative errno value.
	 */
	static int ppp_connect(struct net_if *iface);
#endif

	ConnectionFailureHandler m_failure_handler;
};
