#include <zephyr/dfu/flash_img.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ota, LOG_LEVEL_DBG);

#include "coldtracker_ota.h"

#define HTTP_TIMEOUT_MS 5000

static uint8_t recv_buf[1024];
static struct flash_img_context flash_ctx;

static int http_response_cb(struct http_response *rsp, enum http_final_call final_data,
			    void *user_data)
{
	int ret;

	if (rsp->body_found && rsp->body_frag_len > 0) {
		LOG_INF("Writing %zu bytes to slot 1", rsp->body_frag_len);
		ret = flash_img_buffered_write(&flash_ctx, rsp->body_frag_start, rsp->body_frag_len,
					       final_data == HTTP_DATA_FINAL);
		if (ret) {
			LOG_ERR("Failed to write firmware: %d", ret);
			return ret;
		}
	}

	if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("Firmware downloaded: %zu bytes", flash_img_bytes_written(&flash_ctx));
	}

	return 0;
}

int ota_download(const char *host, const char *port, const char *path)
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
		LOG_ERR("Connection failed: %d", ret);
		zsock_close(sock);
		return ret;
	}

	/* Initialize context needed for writing the image to the flash. */
	ret = flash_img_init(&flash_ctx);
	if (ret) {
		LOG_ERR("Failed to initialize firmware slot: %d", ret);
		zsock_close(sock);
		return ret;
	}

	struct http_request req = {
		.method = HTTP_GET,
		.url = path,
		.host = host,
		.protocol = "HTTP/1.1",
		.response = http_response_cb,
		.recv_buf = recv_buf,
		.recv_buf_len = sizeof(recv_buf),
	};

	LOG_INF("Downloading firmware...");

	/* Send HTTP GET request */
	ret = http_client_req(sock, &req, HTTP_TIMEOUT_MS, NULL);

	/* Close socket */
	zsock_close(sock);

	LOG_INF("Firmware written to slot 1: %zu bytes", flash_img_bytes_written(&flash_ctx));

	return ret;
}
