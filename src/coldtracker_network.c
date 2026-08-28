#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>

#ifdef CONFIG_WIFI
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#endif /* CONFIG_WIFI */

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <zephyr/net/dhcpv4.h>
#include <zephyr/usb/usbd.h>
#include "sample_usbd.h"
#endif /* CONFIG_USB_DEVICE_STACK_NEXT */

#include "net_sample_common.h"
#include "coldtracker_network.h"

#ifdef CONFIG_WIFI

#define WIFI_SSID     "F3D3 Hyperoptic 1Gb Fibre 2.4Ghz"
#define WIFI_PASSWORD "6puYZJG5f63D"

static int wifi_connect(struct net_if *iface)
{
	static struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)WIFI_SSID,
		.ssid_length = sizeof(WIFI_SSID) - 1,
		.psk = (const uint8_t *)WIFI_PASSWORD,
		.psk_length = sizeof(WIFI_PASSWORD) - 1,
		.security = WIFI_SECURITY_TYPE_PSK,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
	};

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

#define PPP_LINK_SETTLE_TIME_MS 5

static int ppp_connect(struct net_if *iface)
{
	ARG_UNUSED(iface);

	/* PPP starts automatically. */
	return 0;
}

static void ppp_network_ready(void)
{
	k_msleep(PPP_LINK_SETTLE_TIME_MS);
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
	wait_for_network();

	IF_ENABLED(CONFIG_NET_PPP, (
		ppp_network_ready();
	))
}
