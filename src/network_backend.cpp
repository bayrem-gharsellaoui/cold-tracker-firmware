/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include "network_backend.hpp"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

#ifdef CONFIG_WIFI
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#endif

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <zephyr/net/dhcpv4.h>
#include <zephyr/usb/usbd.h>

#include "sample_usbd.h"
#endif

LOG_MODULE_REGISTER(network_backend, LOG_LEVEL_DBG);

NetworkBackend::NetworkBackend(ConnectionFailureHandler failure_handler)
	: m_failure_handler(failure_handler)
{
}

int NetworkBackend::connect()
{
	struct net_if *iface = net_if_get_default();

	if (iface == nullptr) {
		return -ENODEV;
	}

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
	return usb_connect(iface);
#elif defined(CONFIG_WIFI)
	return wifi_connect(iface);
#elif defined(CONFIG_NET_PPP)
	return ppp_connect(iface);
#elif defined(CONFIG_NET_NATIVE_OFFLOADED_SOCKETS)
	return 0;
#elif defined(CONFIG_NET_L2_ETHERNET)
	return 0;
#else
	return -ENOTSUP;
#endif
}

#ifdef CONFIG_WIFI

int NetworkBackend::wifi_connect(struct net_if *iface)
{
	static struct wifi_connect_req_params params = {
		.ssid = reinterpret_cast<const uint8_t *>(CONFIG_WIFI_CREDENTIALS_STATIC_SSID),
		.ssid_length = sizeof(CONFIG_WIFI_CREDENTIALS_STATIC_SSID) - 1,
		.psk = reinterpret_cast<const uint8_t *>(CONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD),
		.psk_length = sizeof(CONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD) - 1,
		.security = WIFI_SECURITY_TYPE_PSK,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
	};

	net_mgmt_init_event_callback(&m_wifi_mgmt_cb, wifi_event_handler,
				     NET_EVENT_WIFI_CONNECT_RESULT);

	net_mgmt_add_event_callback(&m_wifi_mgmt_cb);

	LOG_INF("Connecting to SSID: %s", CONFIG_WIFI_CREDENTIALS_STATIC_SSID);

	return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
}

void NetworkBackend::wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
					struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (event != NET_EVENT_WIFI_CONNECT_RESULT) {
		return;
	}

	const auto *status = static_cast<const struct wifi_status *>(cb->info);

	if (status->status == 0) {
		LOG_INF("Wi-Fi connected");
		return;
	}

	LOG_ERR("Wi-Fi connection failed: %d", status->status);

	auto *backend = CONTAINER_OF(cb, NetworkBackend, m_wifi_mgmt_cb);

	if (backend->m_failure_handler != nullptr) {
		backend->m_failure_handler();
	}
}

#endif /* CONFIG_WIFI */

#ifdef CONFIG_USB_DEVICE_STACK_NEXT

int NetworkBackend::usb_connect(struct net_if *iface)
{
	struct usbd_context *ctx = sample_usbd_init_device(nullptr);

	if (ctx == nullptr) {
		return -ENODEV;
	}

	const int ret = usbd_enable(ctx);

	if (ret < 0) {
		return ret;
	}

	net_dhcpv4_start(iface);

	return 0;
}

#endif /* CONFIG_USB_DEVICE_STACK_NEXT */

#ifdef CONFIG_NET_PPP

int NetworkBackend::ppp_connect(struct net_if *iface)
{
	ARG_UNUSED(iface);

	/* PPP starts automatically. */
	return 0;
}

#endif /* CONFIG_NET_PPP */
