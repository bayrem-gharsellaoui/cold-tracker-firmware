#include <string.h>
#include <strings.h>

#include <zephyr/net/socket.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/http/parser.h>
#include <zephyr/net/http/parser_url.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ota, LOG_LEVEL_DBG);

#include "coldtracker_ota.h"

#define HTTP_TIMEOUT_MS      30000
#define HTTP_RECV_BUF_SIZE   1024
#define REDIRECT_URL_MAX_LEN 2048

struct redirect_context {
	char url[REDIRECT_URL_MAX_LEN];
	size_t len;
	bool location_found;
};

struct ota_context {
	uint8_t recv_buf[HTTP_RECV_BUF_SIZE];
	struct flash_img_context flash_ctx;
	struct redirect_context redirect;
	uint16_t http_status;
};

struct url_context {
	char host[128];
	char port[6];
	const char *path;
	bool tls;
};

static struct ota_context ota_ctx;

static int parse_url(const char *url, struct url_context *ctx)
{
	struct http_parser_url parsed = {0};

	/* Initialize parser context URL members */
	memset(ctx, 0, sizeof(*ctx));
	http_parser_url_init(&parsed);

	/* Parse URL into its individual fields. */
	int ret = http_parser_parse_url(url, strlen(url), false, &parsed);
	if (ret) {
		LOG_ERR("Failed to parse URL");
		return -EINVAL;
	}

	/* Host and scheme are required. */
	if (!(parsed.field_set & BIT(UF_SCHEMA)) || !(parsed.field_set & BIT(UF_HOST))) {
		LOG_ERR("URL is missing scheme or host");
		return -EINVAL;
	}

	/* Copy hostname. */
	size_t host_len = parsed.field_data[UF_HOST].len;

	if (host_len >= sizeof(ctx->host)) {
		return -ENOMEM;
	}

	memcpy(ctx->host, &url[parsed.field_data[UF_HOST].off], host_len);
	ctx->host[host_len] = '\0';

	/* Select transport and default port from URL scheme. */
	const char *scheme = &url[parsed.field_data[UF_SCHEMA].off];
	size_t scheme_len = parsed.field_data[UF_SCHEMA].len;

	if ((scheme_len == strlen("https")) && (strncasecmp(scheme, "https", scheme_len) == 0)) {
#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
		ctx->tls = true;
		strcpy(ctx->port, "443");
#else
		LOG_ERR("HTTPS requested but TLS support is disabled");
		return -EPROTONOSUPPORT;
#endif
	} else if ((scheme_len == strlen("http")) &&
		   (strncasecmp(scheme, "http", scheme_len) == 0)) {
		ctx->tls = false;
		strcpy(ctx->port, "80");
	} else {
		LOG_ERR("Unsupported URL scheme");
		return -EPROTONOSUPPORT;
	}

	/* Override the default port when one is explicitly provided. */
	if (parsed.field_set & BIT(UF_PORT)) {
		size_t port_len = parsed.field_data[UF_PORT].len;

		if (port_len >= sizeof(ctx->port)) {
			return -EINVAL;
		}

		memcpy(ctx->port, &url[parsed.field_data[UF_PORT].off], port_len);
		ctx->port[port_len] = '\0';
	}

	/* Point directly into the original URL so the query string is preserved. */
	if (parsed.field_set & BIT(UF_PATH)) {
		ctx->path = &url[parsed.field_data[UF_PATH].off];
	} else {
		ctx->path = "/";
	}

	return 0;
}

static int on_header_field(struct http_parser *parser, const char *at, size_t length)
{
	ARG_UNUSED(parser);

	/* Check if this is the Location header. */
	ota_ctx.redirect.location_found =
		(length == sizeof("Location") - 1) && (strncasecmp(at, "Location", length) == 0);

	return 0;
}

static int on_header_value(struct http_parser *parser, const char *at, size_t length)
{
	ARG_UNUSED(parser);

	if (!ota_ctx.redirect.location_found) {
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

static int http_get(const char *url, http_response_cb_t response_cb,
		    const struct http_parser_settings *parser_settings, void *user_data)
{
	int sock = 0;
	struct zsock_addrinfo *addr = NULL;
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};
	struct url_context target;

	/* Extract url information such as host, path... */
	int ret = parse_url(url, &target);
	if (ret) {
		return ret;
	}

	/* Resolve DNS hostname. */
	ret = zsock_getaddrinfo(target.host, target.port, &hints, &addr);
	if (ret) {
		LOG_ERR("DNS resolution failed: %d", ret);
		return -EHOSTUNREACH;
	}
	LOG_DBG("Resolved %s:%s", target.host, target.port);

	/* Create a network socket. */
	if (target.tls) {
#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
		sock = zsock_socket(addr->ai_family, addr->ai_socktype, IPPROTO_TLS_1_2);
#else
		ret = -EPROTONOSUPPORT;
		goto cleanup;
#endif
	} else {
		sock = zsock_socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
	}

	if (sock < 0) {
		ret = -errno;
		zsock_freeaddrinfo(addr);
		return ret;
	}
	LOG_DBG("Created socket %d for %s", sock, target.host);

#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
	if (target.tls) {
		/* Skip peer verification while bringing up TLS. */
		int verify = TLS_PEER_VERIFY_NONE;
		ret = zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
		if (ret < 0) {
			ret = -errno;
			LOG_ERR("Failed to configure TLS: %d", ret);
			goto cleanup;
		}

		/* Set TLS hostname. */
		ret = zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, target.host,
				       strlen(target.host));
		if (ret < 0) {
			ret = -errno;
			LOG_ERR("Failed to set TLS hostname: %d", ret);
			goto cleanup;
		}
	}
#endif /* CONFIG_NET_SOCKETS_SOCKOPT_TLS */

	/* Connect socket to the resolved address. */
	LOG_DBG("Connecting socket %d to %s:%s", sock, target.host, target.port);
	ret = zsock_connect(sock, addr->ai_addr, addr->ai_addrlen);
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Connection to %s:%s failed: %d", target.host, target.port, ret);
		goto cleanup;
	}

	LOG_INF("Connected to %s:%s", target.host, target.port);

	struct http_request req = {
		.method = HTTP_GET,
		.url = target.path,
		.host = target.host,
		.protocol = "HTTP/1.1",
		.response = response_cb,
		.http_cb = parser_settings,
		.recv_buf = ota_ctx.recv_buf,
		.recv_buf_len = sizeof(ota_ctx.recv_buf),
	};

	/* Send HTTP GET request. */
	LOG_INF("GET %s", target.path);
	ret = http_client_req(sock, &req, HTTP_TIMEOUT_MS, user_data);

cleanup:
	/* Close socket and release DNS result. */
	LOG_DBG("Closing socket %d", sock);
	zsock_close(sock);
	zsock_freeaddrinfo(addr);

	return ret;
}

static int ota_response_cb(struct http_response *rsp, enum http_final_call final_data,
			   void *user_data)
{
	struct ota_context *ctx = user_data;

	/* Write firmware data only when the server returns HTTP 200. */
	if ((rsp->http_status_code == 200) && rsp->body_found && (rsp->body_frag_len > 0)) {
		int ret = flash_img_buffered_write(&ctx->flash_ctx, rsp->body_frag_start,
						   rsp->body_frag_len,
						   (final_data == HTTP_DATA_FINAL));
		if (ret) {
			LOG_ERR("Failed to write firmware: %d", ret);
			return ret;
		}
	}

	/* Save the final HTTP status code. */
	if (final_data == HTTP_DATA_FINAL) {
		ctx->http_status = rsp->http_status_code;
	}

	return 0;
}

int ota_update(const char *url)
{
	/* Initialize the context used to write firmware into slot 1. */
	int ret = flash_img_init(&ota_ctx.flash_ctx);
	if (ret) {
		LOG_ERR("Failed to initialize firmware slot: %d", ret);
		return ret;
	}

	/* Reset request state. */
	ota_ctx.redirect.len = 0;
	ota_ctx.redirect.url[0] = '\0';
	ota_ctx.redirect.location_found = false;
	ota_ctx.http_status = 0;

	LOG_INF("Downloading firmware...");

	/* Request firmware from the supplied URL. */
	ret = http_get(url, ota_response_cb, &redirect_parser, &ota_ctx);
	if (ret < 0) {
		return ret;
	}

	/* Firmware was served directly. */
	if (ota_ctx.http_status == 200) {
		LOG_INF("Firmware downloaded: %zu bytes",
			flash_img_bytes_written(&ota_ctx.flash_ctx));
		return 0;
	}

	/* Follow an HTTP redirect if one was received. */
	if ((ota_ctx.http_status == 302) && (ota_ctx.redirect.len > 0)) {
		LOG_INF("Following redirect:\r\n%s", ota_ctx.redirect.url);

		ota_ctx.http_status = 0;

		ret = http_get(ota_ctx.redirect.url, ota_response_cb, NULL, &ota_ctx);
		if (ret < 0) {
			return ret;
		}

		if (ota_ctx.http_status != 200) {
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
static int cmd_update(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	int ret = ota_update(argv[1]);
	if (ret < 0) {
		shell_error(sh, "Firmware download failed: %d", ret);
		return ret;
	}

	return 0;
}

SHELL_CMD_ARG_REGISTER(update, NULL, "Download firmware update: update <url>", cmd_update, 2, 0);
#endif /* CONFIG_SHELL */
