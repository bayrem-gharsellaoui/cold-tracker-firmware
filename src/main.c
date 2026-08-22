#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/app_version.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include "net_sample_common.h"
#include "coldtracker_network.h"

#define ANSI_COLOR_CYAN  "\033[96m"
#define ANSI_COLOR_GREEN "\033[92m"
#define ANSI_COLOR_RESET "\033[0m"

/* Generated from
 * https://patorjk.com/software/taag/#p=display&f=ANSI+Shadow&t=Type+Something+&x=none&v=4&h=4&w=80&we=false
 * and formatted using "clang-format --style=file:${ZEPHYR_BASE}/.clang-format -i src/main.c"
 */
#define COLDTRACKER_BOOT_BANNER                                                                    \
	"\r\n" ANSI_COLOR_CYAN " ██████╗ ██████╗ ██╗     ██████╗ " ANSI_COLOR_GREEN                \
	"████████╗██████╗  █████╗  ██████╗██╗  ██╗███████╗██████╗ " ANSI_COLOR_RESET               \
	"\r\n" ANSI_COLOR_CYAN "██╔════╝██╔═══██╗██║     ██╔══██╗" ANSI_COLOR_GREEN                \
	"╚══██╔══╝██╔══██╗██╔══██╗██╔════╝██║ ██╔╝██╔════╝██╔══██╗" ANSI_COLOR_RESET               \
	"\r\n" ANSI_COLOR_CYAN "██║     ██║   ██║██║     ██║  ██║" ANSI_COLOR_GREEN                \
	"   ██║   ██████╔╝███████║██║     █████╔╝ █████╗  ██████╔╝" ANSI_COLOR_RESET               \
	"\r\n" ANSI_COLOR_CYAN "██║     ██║   ██║██║     ██║  ██║" ANSI_COLOR_GREEN                \
	"   ██║   ██╔══██╗██╔══██║██║     ██╔═██╗ ██╔══╝  ██╔══██╗" ANSI_COLOR_RESET               \
	"\r\n" ANSI_COLOR_CYAN "╚██████╗╚██████╔╝███████╗██████╔╝" ANSI_COLOR_GREEN                \
	"   ██║   ██║  ██║██║  ██║╚██████╗██║  ██╗███████╗██║  ██║" ANSI_COLOR_RESET               \
	"\r\n" ANSI_COLOR_CYAN " ╚═════╝ ╚═════╝ ╚══════╝╚═════╝ " ANSI_COLOR_GREEN                \
	"   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝" ANSI_COLOR_RESET "\r\n"        \
	"\r\n" ANSI_COLOR_GREEN "                     v%s - (c) 2026 Techleef. All rights "        \
	"reserved.\r\n" ANSI_COLOR_RESET ANSI_COLOR_CYAN                                           \
	"                            Target: %s\r\n\r\n" ANSI_COLOR_RESET

#define HTTP_TIMEOUT_MS 5000

static uint8_t recv_buf[1024];

static int http_response_cb(struct http_response *rsp, enum http_final_call final_data,
			    void *user_data)
{
	if (rsp->body_found) {
		LOG_INF("HTTP status: %d", rsp->http_status_code);
		LOG_HEXDUMP_INF(rsp->body_frag_start, rsp->body_frag_len, "Body");
	}

	return 0;
}

static int http_get(const char *host, const char *port, const char *path)
{
	struct zsock_addrinfo *addr = NULL;
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};

	/* Resolve DNS hostname */
	int ret = zsock_getaddrinfo(host, port, &hints, &addr);
	if (ret) {
		LOG_ERR("DNS resolution failed: %d", ret);
		return -EHOSTUNREACH;
	}

	/* Create a network socket */
	int sock = zsock_socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
	if (sock < 0) {
		ret = -errno;
		zsock_freeaddrinfo(addr);
		return ret;
	}

	/* Connect socket to the resolved address */
	ret = zsock_connect(sock, addr->ai_addr, addr->ai_addrlen);
	zsock_freeaddrinfo(addr);

	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Connection to %s:%s failed: %d", host, port, ret);
		zsock_close(sock);
		return ret;
	}

	LOG_INF("Connected to %s:%s", host, port);

	struct http_request req = {
		.method = HTTP_GET,
		.url = path,
		.host = host,
		.protocol = "HTTP/1.1",
		.response = http_response_cb,
		.recv_buf = recv_buf,
		.recv_buf_len = sizeof(recv_buf),
	};

	/* Send HTTP GET request */
	ret = http_client_req(sock, &req, HTTP_TIMEOUT_MS, NULL);

	/* Close socket */
	zsock_close(sock);

	return ret;
}

int main(void)
{
	int ret;

	printk(COLDTRACKER_BOOT_BANNER, APP_VERSION_STRING, CONFIG_BOARD_TARGET);

	ret = network_connect();
	if (ret) {
		LOG_ERR("Failed to initiate network connection: %d", ret);
		return ret;
	}

	wait_for_network();

	LOG_INF("ColdTracker is online");

	/* HTTP / TLS / OTA from here */
	ret = http_get("example.com", "80", "/");
	// ret = http_get("10.42.0.1", "4242", "/");
	if (ret < 0) {
		LOG_ERR("Internet HTTP request failed: %d", ret);
	}

	while (1) {
		k_msleep(1000);
	}

	return 0;
}
