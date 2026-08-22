#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/util.h>
#if defined(CONFIG_WIFI)
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#endif

#include "coldtracker_network.h"

#ifdef CONFIG_WIFI
// #define WIFI_SSID "F3D3 Hyperoptic 1Gb Fibre 2.4Ghz"
// #define WIFI_PASSWORD "6puYZJG5f63D"

#define WIFI_SSID     "Galaxy S21 Ultra 5G fc54"
#define WIFI_PASSWORD "zmga9847"

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

int network_connect(void)
{
	struct net_if *iface = net_if_get_default();

	if (!iface) {
		return -ENODEV;
	}

	IF_ENABLED(CONFIG_WIFI, (
		return wifi_connect(iface);
	))

	return 0;
}
