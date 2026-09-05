#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/sys/util.h>
#include <zephyr/zbus/zbus.h>
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

#include "coldtracker_messages.h"

#define NETWORK_EVENT_UP   BIT(0)
#define NETWORK_EVENT_DOWN BIT(1)

K_EVENT_DEFINE(network_events);

static void publish_network_state(enum network_state state)
{
	struct network_status_msg msg = {
		.state = state,
	};

	int ret = zbus_chan_pub(&network_status_chan, &msg, K_MSEC(100));
	if (ret < 0) {
		LOG_ERR("Failed to publish network state: %d", ret);
	}
}

static void net_up_event_handler(uint64_t event, struct net_if *iface, void *info,
				 size_t info_length, void *user_data)
{
	ARG_UNUSED(event);
	ARG_UNUSED(iface);
	ARG_UNUSED(info);
	ARG_UNUSED(info_length);
	ARG_UNUSED(user_data);

	k_event_post(&network_events, NETWORK_EVENT_UP);
}

NET_MGMT_REGISTER_EVENT_HANDLER(coldtracker_net_up_handler, NET_EVENT_L4_CONNECTED,
				net_up_event_handler, NULL);

static void net_down_event_handler(uint64_t event, struct net_if *iface, void *info,
				   size_t info_length, void *user_data)
{
	ARG_UNUSED(event);
	ARG_UNUSED(iface);
	ARG_UNUSED(info);
	ARG_UNUSED(info_length);
	ARG_UNUSED(user_data);

	k_event_post(&network_events, NETWORK_EVENT_DOWN);
}

NET_MGMT_REGISTER_EVENT_HANDLER(coldtracker_net_down_handler, NET_EVENT_L4_DISCONNECTED,
				net_down_event_handler, NULL);

#ifdef CONFIG_WIFI
static struct net_mgmt_event_callback wifi_mgmt_cb;

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *status = (const struct wifi_status *)cb->info;

		if (status->status == 0) {
			LOG_INF("Wi-Fi connected");
		} else {
			LOG_ERR("Wi-Fi connect failed, status: %d", status->status);
			k_event_post(&network_events, NETWORK_EVENT_DOWN);
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

static int network_connect(void)
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

	return -ENOTSUP;
}

static void network_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	publish_network_state(NETWORK_STATE_CONNECTING);

	int ret = network_connect();
	if (ret < 0) {
		LOG_ERR("Failed to initiate network connection: %d", ret);
		publish_network_state(NETWORK_STATE_OFFLINE);
	}

	while (1) {
		uint32_t events = k_event_wait(
			&network_events, NETWORK_EVENT_UP | NETWORK_EVENT_DOWN, true, K_FOREVER);

		if (events & NETWORK_EVENT_UP) {
			LOG_INF("Network is online");
			publish_network_state(NETWORK_STATE_ONLINE);
		}

		if (events & NETWORK_EVENT_DOWN) {
			LOG_WRN("Network is offline");
			publish_network_state(NETWORK_STATE_OFFLINE);
		}
	}
}

K_THREAD_DEFINE(network_thread_id, 2048, network_thread, NULL, NULL, NULL, 5, 0, 0);
