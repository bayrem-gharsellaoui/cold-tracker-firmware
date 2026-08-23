#include <string.h>
#include <strings.h>

#include <zephyr/net/socket.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/http/parser.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ota, LOG_LEVEL_DBG);

#include "coldtracker_ota.h"

#define HTTP_TIMEOUT_MS      5000
#define HTTP_RECV_BUF_SIZE   1024
#define REDIRECT_URL_MAX_LEN 2048

struct redirect_context {
	char url[REDIRECT_URL_MAX_LEN];
	size_t len;
	bool url_found;
};

struct ota_context {
	uint8_t recv_buf[HTTP_RECV_BUF_SIZE];
	struct flash_img_context flash_ctx;
	struct redirect_context redirect;
	uint16_t http_status;
};

static struct ota_context ota_ctx;

static int on_header_field(struct http_parser *parser, const char *at, size_t length)
{
	ARG_UNUSED(parser);

	/* Check if this is the Location header. */
	ota_ctx.redirect.url_found =
		(length == sizeof("Location") - 1) && (strncasecmp(at, "Location", length) == 0);

	return 0;
}

static int on_header_value(struct http_parser *parser, const char *at, size_t length)
{
	ARG_UNUSED(parser);

	if (!ota_ctx.redirect.url_found) {
		return 0;
	}

	/* Make sure the redirect URL fits in the buffer. */
	if ((ota_ctx.redirect.len + length) >= sizeof(ota_ctx.redirect.url)) {
		return -ENOMEM;
	}

	/* Append the received URL fragment. */
	memcpy(&ota_ctx.redirect.url[ota_ctx.redirect.len], at, length);

	ota_ctx.redirect.len += length;
	ota_ctx.redirect.url[ota_ctx.redirect.len] = '\0';

	return 0;
}

static const struct http_parser_settings redirect_parser = {
	.on_header_field = on_header_field,
	.on_header_value = on_header_value,
};

static int http_get(const char *host, const char *port, const char *path,
		    http_response_cb_t response_cb,
		    const struct http_parser_settings *parser_settings, void *user_data)
{
	struct zsock_addrinfo *addr = NULL;
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};

	/* Resolve DNS hostname. */
	int ret = zsock_getaddrinfo(host, port, &hints, &addr);
	if (ret) {
		LOG_ERR("DNS resolution failed: %d", ret);
		return -EHOSTUNREACH;
	}

	/* Create a network socket. */
#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
	int sock = zsock_socket(addr->ai_family, addr->ai_socktype, IPPROTO_TLS_1_2);
#else
	int sock = zsock_socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
#endif /* CONFIG_NET_SOCKETS_SOCKOPT_TLS */
	if (sock < 0) {
		ret = -errno;
		zsock_freeaddrinfo(addr);
		return ret;
	}

#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
	/* Skip peer verification while bringing up TLS. */
	int verify = TLS_PEER_VERIFY_NONE;
	ret = zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Failed to configure TLS: %d", ret);
		goto cleanup;
	}

	/* Set TLS hostname. */
	ret = zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, host, strlen(host));
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Failed to set TLS hostname: %d", ret);
		goto cleanup;
	}
#endif /* CONFIG_NET_SOCKETS_SOCKOPT_TLS */

	/* Connect socket to the resolved address. */
	ret = zsock_connect(sock, addr->ai_addr, addr->ai_addrlen);
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Connection to %s:%s failed: %d", host, port, ret);
		goto cleanup;
	}

	LOG_INF("Connected to %s:%s", host, port);

	struct http_request req = {
		.method = HTTP_GET,
		.url = path,
		.host = host,
		.protocol = "HTTP/1.1",
		.response = response_cb,
		.http_cb = parser_settings,
		.recv_buf = ota_ctx.recv_buf,
		.recv_buf_len = sizeof(ota_ctx.recv_buf),
	};

	/* Send HTTP GET request. */
	LOG_INF("GET %s", path);
	ret = http_client_req(sock, &req, HTTP_TIMEOUT_MS, user_data);

cleanup:
	/* Close socket and release DNS result. */
	zsock_close(sock);
	zsock_freeaddrinfo(addr);

	return ret;
}

static int ota_get_redirect_link_response_cb(struct http_response *rsp,
					     enum http_final_call final_data, void *user_data)
{
	struct ota_context *ctx = user_data;

	if (final_data == HTTP_DATA_FINAL) {
		ctx->http_status = rsp->http_status_code;
	}

	return 0;
}

static int ota_get_redirect_link(const char *host, const char *port, const char *path)
{
	/* Reset redirect state before starting a new request. */
	ota_ctx.redirect.len = 0;
	ota_ctx.redirect.url[0] = '\0';
	ota_ctx.redirect.url_found = false;
	ota_ctx.http_status = 0;

	/* Request the GitHub release URL and capture its Location header. */
	int ret = http_get(host, port, path, ota_get_redirect_link_response_cb, &redirect_parser,
			   &ota_ctx);
	if (ret < 0) {
		return ret;
	}

	/* GitHub should redirect us to the actual release asset. */
	if (ota_ctx.http_status != 302) {
		LOG_ERR("Expected HTTP 302, got %u", ota_ctx.http_status);
		return -EBADMSG;
	}

	if (ota_ctx.redirect.len == 0) {
		LOG_ERR("Firmware download URL not found");
		return -ENOENT;
	}

	LOG_INF("Firmware download URL received:\r\n%s", ota_ctx.redirect.url);

	return 0;
}

static int ota_download_image_response_cb(struct http_response *rsp,
					  enum http_final_call final_data, void *user_data)
{
	struct ota_context *ctx = user_data;

	/* Only write the body of a successful firmware response. */
	if ((rsp->http_status_code == 200) && (rsp->body_found) && (rsp->body_frag_len > 0)) {
		int ret = flash_img_buffered_write(&ctx->flash_ctx, rsp->body_frag_start,
						   rsp->body_frag_len,
						   (final_data == HTTP_DATA_FINAL));
		if (ret) {
			LOG_ERR("Failed to write firmware: %d", ret);
			return ret;
		}
	}

	if (final_data == HTTP_DATA_FINAL) {
		ctx->http_status = rsp->http_status_code;
	}

	return 0;
}

static int ota_download_image(const char *host, const char *port, const char *path)
{
	/* Initialize the context used to write the firmware into slot 1. */
	int ret = flash_img_init(&ota_ctx.flash_ctx);
	if (ret) {
		LOG_ERR("Failed to initialize firmware slot: %d", ret);
		return ret;
	}

	ota_ctx.http_status = 0;

	LOG_INF("Downloading firmware...");

	/* Download the firmware and stream it directly into slot 1. */
	ret = http_get(host, port, path, ota_download_image_response_cb, NULL, &ota_ctx);
	if (ret < 0) {
		return ret;
	}

	if (ota_ctx.http_status != 200) {
		LOG_ERR("Firmware request failed: HTTP %u", ota_ctx.http_status);
		return -EIO;
	}

	LOG_INF("Firmware downloaded: %zu bytes", flash_img_bytes_written(&ota_ctx.flash_ctx));

	return 0;
}

int ota_update(const char *host, const char *port, const char *path)
{
#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
	int ret = ota_get_redirect_link(host, port, path);
	if (ret) {
		return ret;
	}

	/* TODO: parse redirect URL and call ota_download_image(). */
	return 0;
#else
	return ota_download_image(host, port, path);
#endif
}
