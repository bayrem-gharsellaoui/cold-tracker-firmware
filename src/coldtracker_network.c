#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(network, LOG_LEVEL_DBG);

#ifdef CONFIG_WIFI
#include <zephyr/net/wifi_mgmt.h>
#endif /* CONFIG_WIFI */

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <zephyr/net/dhcpv4.h>
#include <zephyr/usb/usbd.h>
#include "sample_usbd.h"
#endif /* CONFIG_USB_DEVICE_STACK_NEXT */

#include "coldtracker_network.h"

static K_SEM_DEFINE(network_ready_sem, 0, 1);

#ifdef CONFIG_NET_PPP
#define NETWORK_READY_EVENT NET_EVENT_DNS_SERVERS_RECONFIGURED
#else
#define NETWORK_READY_EVENT NET_EVENT_L4_CONNECTED
#endif

static void net_event_handler(uint64_t event, struct net_if *iface, void *info, size_t info_length,
			      void *user_data)
{
	if (event == NETWORK_READY_EVENT) {
		k_sem_give(&network_ready_sem);
	}
}

NET_MGMT_REGISTER_EVENT_HANDLER(coldtracker_net_event_handler, NETWORK_READY_EVENT,
				net_event_handler, NULL);

#ifdef CONFIG_WIFI
static struct net_mgmt_event_callback wifi_mgmt_cb;

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *status = (const struct wifi_status *)cb->info;

		if (status->status == 0) {
			LOG_INF("Wi-Fi connected");
		} else {
			LOG_ERR("Wi-Fi connect failed, status: %d", status->status);
		}
	}
}

static int wifi_connect(struct net_if *iface)
{
	net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_event_handler,
				     NET_EVENT_WIFI_CONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_mgmt_cb);

	static struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)CONFIG_WIFI_CREDENTIALS_STATIC_SSID,
		.ssid_length = sizeof(CONFIG_WIFI_CREDENTIALS_STATIC_SSID) - 1,
		.psk = (const uint8_t *)CONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD,
		.psk_length = sizeof(CONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD) - 1,
		.security = WIFI_SECURITY_TYPE_PSK,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
	};

	LOG_INF("Connecting to SSID: %s", CONFIG_WIFI_CREDENTIALS_STATIC_SSID);

	return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
}
#endif /* CONFIG_WIFI */

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
static int usb_connect(struct net_if *iface)
{
	struct usbd_context *ctx;
	int ret;

	ctx = sample_usbd_init_device(NULL);
	if (ctx == NULL) {
		return -ENODEV;
	}

	ret = usbd_enable(ctx);
	if (ret) {
		return ret;
	}

	net_dhcpv4_start(iface);

	return 0;
}
#endif /* CONFIG_USB_DEVICE_STACK_NEXT */

#ifdef CONFIG_NET_PPP
static int ppp_connect(struct net_if *iface)
{
	ARG_UNUSED(iface);

	/* PPP starts automatically. */
	return 0;
}
#endif /* CONFIG_NET_PPP */

int network_connect(void)
{
	struct net_if *iface = net_if_get_default();

	if (!iface) {
		return -ENODEV;
	}

	IF_ENABLED(CONFIG_USB_DEVICE_STACK_NEXT, (
		return usb_connect(iface);
	))

	IF_ENABLED(CONFIG_WIFI, (
		return wifi_connect(iface);
	))

	IF_ENABLED(CONFIG_NET_PPP, (
		return ppp_connect(iface);
	))

	return 0;
}

void network_wait_ready(void)
{
	LOG_INF("Waiting for network...");

	k_sem_take(&network_ready_sem, K_FOREVER);

	LOG_INF("Network is ready");
}
