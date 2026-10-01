/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coldtracker_ota.hpp"

#include <cstddef>
#include <cstdint>
#include <strings.h>

#include <zephyr/dfu/flash_img.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/http/parser.h>
#include <zephyr/net/http/parser_url.h>
#include <zephyr/net/socket.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(ota, LOG_LEVEL_DBG);

namespace
{

constexpr int32_t kHttpTimeoutMs = 30000;
constexpr size_t kHttpRecvBufSize = 1024;
constexpr size_t kRedirectUrlMaxLen = 2048;

struct RedirectContext {
	char url[kRedirectUrlMaxLen]{};
	size_t len{};
	bool location_found{};
};

struct OtaContext {
	uint8_t recv_buf[kHttpRecvBufSize]{};
	struct flash_img_context flash_ctx{};
	RedirectContext redirect{};
	uint16_t http_status{};
};

struct UrlContext {
	char host[128]{};
	char port[6]{};
	const char *path{};
	bool tls{};
};

OtaContext ota_ctx{};

/**
 * @brief Parse an HTTP or HTTPS URL.
 *
 * @param url URL to parse.
 * @param context Destination URL context.
 *
 * @return 0 on success, otherwise a negative errno value.
 */
int parse_url(const char *url, UrlContext &context)
{
	struct http_parser_url parsed{};

	http_parser_url_init(&parsed);

	const int ret = http_parser_parse_url(url, std::strlen(url), false, &parsed);

	if (ret != 0) {
		LOG_ERR("Failed to parse URL");
		return -EINVAL;
	}

	if ((parsed.field_set & BIT(UF_SCHEMA)) == 0U || (parsed.field_set & BIT(UF_HOST)) == 0U) {
		LOG_ERR("URL is missing scheme or host");
		return -EINVAL;
	}

	const size_t host_len = parsed.field_data[UF_HOST].len;

	if (host_len >= sizeof(context.host)) {
		return -ENOMEM;
	}

	std::memcpy(context.host, &url[parsed.field_data[UF_HOST].off], host_len);

	context.host[host_len] = '\0';

	const char *scheme = &url[parsed.field_data[UF_SCHEMA].off];

	const size_t scheme_len = parsed.field_data[UF_SCHEMA].len;

	if ((scheme_len == std::strlen("https")) &&
	    (strncasecmp(scheme, "https", scheme_len) == 0)) {
#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
		context.tls = true;
		std::strcpy(context.port, "443");
#else
		LOG_ERR("HTTPS requested but TLS support is disabled");
		return -EPROTONOSUPPORT;
#endif
	} else if ((scheme_len == std::strlen("http")) &&
		   (strncasecmp(scheme, "http", scheme_len) == 0)) {
		context.tls = false;
		std::strcpy(context.port, "80");
	} else {
		LOG_ERR("Unsupported URL scheme");
		return -EPROTONOSUPPORT;
	}

	if ((parsed.field_set & BIT(UF_PORT)) != 0U) {
		const size_t port_len = parsed.field_data[UF_PORT].len;

		if (port_len >= sizeof(context.port)) {
			return -EINVAL;
		}

		std::memcpy(context.port, &url[parsed.field_data[UF_PORT].off], port_len);

		context.port[port_len] = '\0';
	}

	if ((parsed.field_set & BIT(UF_PATH)) != 0U) {
		context.path = &url[parsed.field_data[UF_PATH].off];
	} else {
		context.path = "/";
	}

	return 0;
}

/**
 * @brief Handle HTTP response header field fragments.
 *
 * @param parser HTTP parser.
 * @param data Header field data.
 * @param length Header field length.
 *
 * @return 0 on success.
 */
int on_header_field(struct http_parser *parser, const char *data, size_t length)
{
	ARG_UNUSED(parser);

	ota_ctx.redirect.location_found =
		(length == sizeof("Location") - 1U) && (strncasecmp(data, "Location", length) == 0);

	return 0;
}

/**
 * @brief Handle HTTP response header value fragments.
 *
 * @param parser HTTP parser.
 * @param data Header value data.
 * @param length Header value length.
 *
 * @return 0 on success, otherwise a negative errno value.
 */
int on_header_value(struct http_parser *parser, const char *data, size_t length)
{
	ARG_UNUSED(parser);

	if (!ota_ctx.redirect.location_found) {
		return 0;
	}

	if ((ota_ctx.redirect.len + length) >= sizeof(ota_ctx.redirect.url)) {
		return -ENOMEM;
	}

	std::memcpy(&ota_ctx.redirect.url[ota_ctx.redirect.len], data, length);

	ota_ctx.redirect.len += length;
	ota_ctx.redirect.url[ota_ctx.redirect.len] = '\0';

	return 0;
}

/**
 * @brief Perform an HTTP GET request.
 *
 * @param url Request URL.
 * @param response_cb HTTP response callback.
 * @param parser_settings Optional HTTP parser settings.
 * @param user_data User data passed to the response callback.
 *
 * @return 0 on success, otherwise a negative errno value.
 */
int http_get(const char *url, http_response_cb_t response_cb,
	     const struct http_parser_settings *parser_settings, void *user_data)
{
	struct zsock_addrinfo *address = nullptr;

	struct zsock_addrinfo hints{};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	UrlContext target{};

	int ret = parse_url(url, target);

	if (ret < 0) {
		return ret;
	}

	ret = zsock_getaddrinfo(target.host, target.port, &hints, &address);

	if (ret != 0) {
		LOG_ERR("DNS resolution failed: %d", ret);
		return -EHOSTUNREACH;
	}

	LOG_DBG("Resolved %s:%s", target.host, target.port);

	int socket = -1;

	if (target.tls) {
#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
		socket = zsock_socket(address->ai_family, address->ai_socktype, IPPROTO_TLS_1_2);
#else
		ret = -EPROTONOSUPPORT;
		goto cleanup;
#endif
	} else {
		socket = zsock_socket(address->ai_family, address->ai_socktype,
				      address->ai_protocol);
	}

	if (socket < 0) {
		ret = -errno;
		goto cleanup;
	}

	LOG_DBG("Created socket %d for %s", socket, target.host);

#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
	if (target.tls) {
		int verify = TLS_PEER_VERIFY_NONE;

		ret = zsock_setsockopt(socket, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));

		if (ret < 0) {
			ret = -errno;
			LOG_ERR("Failed to configure TLS: %d", ret);
			goto cleanup;
		}

		ret = zsock_setsockopt(socket, SOL_TLS, TLS_HOSTNAME, target.host,
				       std::strlen(target.host));

		if (ret < 0) {
			ret = -errno;
			LOG_ERR("Failed to set TLS hostname: %d", ret);
			goto cleanup;
		}
	}
#endif

	LOG_DBG("Connecting socket %d to %s:%s", socket, target.host, target.port);

	ret = zsock_connect(socket, address->ai_addr, address->ai_addrlen);

	if (ret < 0) {
		ret = -errno;

		LOG_ERR("Connection to %s:%s failed: %d", target.host, target.port, ret);

		goto cleanup;
	}

	LOG_INF("Connected to %s:%s", target.host, target.port);

	struct http_request request{};

	request.method = HTTP_GET;
	request.url = target.path;
	request.host = target.host;
	request.protocol = "HTTP/1.1";
	request.response = response_cb;
	request.http_cb = parser_settings;
	request.recv_buf = ota_ctx.recv_buf;
	request.recv_buf_len = sizeof(ota_ctx.recv_buf);

	LOG_INF("GET %s", target.path);

	ret = http_client_req(socket, &request, kHttpTimeoutMs, user_data);

cleanup:

	if (socket >= 0) {
		LOG_DBG("Closing socket %d", socket);
		zsock_close(socket);
	}

	if (address != nullptr) {
		zsock_freeaddrinfo(address);
	}

	return ret;
}

/**
 * @brief Handle HTTP firmware response data.
 *
 * Firmware data from successful HTTP responses is written directly to the
 * secondary image slot.
 *
 * @param response HTTP response.
 * @param final_data Indicates whether this is the final response fragment.
 * @param user_data OTA context.
 *
 * @return 0 on success, otherwise a negative errno value.
 */
int ota_response_callback(struct http_response *response, enum http_final_call final_data,
			  void *user_data)
{
	auto *context = static_cast<OtaContext *>(user_data);

	if ((response->http_status_code == 200) && response->body_found &&
	    (response->body_frag_len > 0U)) {
		const int ret = flash_img_buffered_write(
			&context->flash_ctx, response->body_frag_start, response->body_frag_len,
			final_data == HTTP_DATA_FINAL);

		if (ret != 0) {
			LOG_ERR("Failed to write firmware: %d", ret);
			return ret;
		}
	}

	if (final_data == HTTP_DATA_FINAL) {
		context->http_status = response->http_status_code;
	}

	return 0;
}

/**
 * @brief Download a firmware image from a URL.
 *
 * Supports direct HTTP responses and one HTTP 302 redirect.
 *
 * @param url Firmware image URL.
 *
 * @return 0 on success, otherwise a negative errno value.
 */
int ota_update(const char *url)
{
	int ret = flash_img_init(&ota_ctx.flash_ctx);

	if (ret != 0) {
		LOG_ERR("Failed to initialize firmware slot: %d", ret);
		return ret;
	}

	ota_ctx.redirect = {};
	ota_ctx.http_status = 0U;

	struct http_parser_settings redirect_parser{};
	redirect_parser.on_header_field = on_header_field;
	redirect_parser.on_header_value = on_header_value;

	LOG_INF("Downloading firmware...");

	ret = http_get(url, ota_response_callback, &redirect_parser, &ota_ctx);

	if (ret < 0) {
		return ret;
	}

	if (ota_ctx.http_status == 200U) {
		LOG_INF("Firmware downloaded: %zu bytes",
			flash_img_bytes_written(&ota_ctx.flash_ctx));

		return 0;
	}

	if ((ota_ctx.http_status == 302U) && (ota_ctx.redirect.len > 0U)) {
		LOG_INF("Following redirect:\r\n%s", ota_ctx.redirect.url);

		ota_ctx.http_status = 0U;

		ret = http_get(ota_ctx.redirect.url, ota_response_callback, nullptr, &ota_ctx);

		if (ret < 0) {
			return ret;
		}

		if (ota_ctx.http_status != 200U) {
			LOG_ERR("Firmware request failed: HTTP %u", ota_ctx.http_status);

			return -EIO;
		}

		LOG_INF("Firmware downloaded: %zu bytes",
			flash_img_bytes_written(&ota_ctx.flash_ctx));

		return 0;
	}

	LOG_ERR("Firmware request failed: HTTP %u", ota_ctx.http_status);

	return -EIO;
}

#ifdef CONFIG_SHELL

/**
 * @brief Shell command for manually downloading a firmware update.
 *
 * @param shell Shell instance.
 * @param argc Argument count.
 * @param argv Argument vector.
 *
 * @return 0 on success, otherwise a negative errno value.
 */
int cmd_update(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	const int ret = ota_update(argv[1]);

	if (ret < 0) {
		shell_error(shell, "Firmware download failed: %d", ret);

		return ret;
	}

	return 0;
}

SHELL_CMD_ARG_REGISTER(update, nullptr, "Download firmware update: update <url>", cmd_update, 2, 0);

#endif /* CONFIG_SHELL */

} // namespace

OtaService::OtaService(const char *name, struct k_msgq &event_queue)
	: Service(name, event_queue, kSubscriptions)
{
}

void OtaService::on_event(servz::EventId event)
{
	switch (static_cast<Event>(event)) {
	case Event::NetworkOnline:
		check_for_update();
		break;

	default:
		break;
	}
}

void OtaService::check_for_update()
{
	/*
	 * TODO: Query the firmware update service and determine whether a newer
	 * firmware version is available.
	 */
}

SERVZ_SERVICE_DEFINE(ota_service, OtaService, 2048, 8, 4);
