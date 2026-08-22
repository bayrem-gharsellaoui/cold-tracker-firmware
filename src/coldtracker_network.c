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

#include "coldtracker_network.h"

#ifdef CONFIG_WIFI
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

	return 0;
}
