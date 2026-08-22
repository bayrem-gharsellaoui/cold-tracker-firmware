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
		LOG_INF("Body: %.*s", rsp->body_frag_len, rsp->body_frag_start);
	}

	return 0;
}

static int http_get(const char *host, const char *port, const char *path)
{
	struct zsock_addrinfo *addr;
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = IPPROTO_TCP,
	};

	int ret = zsock_getaddrinfo(host, port, &hints, &addr);
	if (ret) {
		LOG_ERR("DNS resolution failed: %d", ret);
		return -EHOSTUNREACH;
	}

	int sock = zsock_socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
	if (sock < 0) {
		ret = -errno;
		zsock_freeaddrinfo(addr);
		return ret;
	}

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

	ret = http_client_req(sock, &req, HTTP_TIMEOUT_MS, NULL);

	zsock_close(sock);

	return ret;
}

int main(void)
{
	printk(COLDTRACKER_BOOT_BANNER, APP_VERSION_STRING, CONFIG_BOARD_TARGET);

	int ret = network_connect();
	if (ret) {
		LOG_ERR("Failed to initiate network connection: %d", ret);
		return ret;
	}

	wait_for_network();

	LOG_INF("ColdTracker is online");

	/* HTTP / TLS / OTA from here */
	ret = http_get("192.168.1.104", "4242", "/");
	if (ret < 0) {
		LOG_ERR("Local HTTP request failed: %d", ret);
	}

	ret = http_get("example.com", "80", "/");
	if (ret < 0) {
		LOG_ERR("Internet HTTP request failed: %d", ret);
	}

	while (1) {
		k_msleep(1000);
	}

	return 0;
}

/**
 * @brief Zephyr fatal error handler.
 *
 * This function is called by the kernel when a fatal error occurs,
 * such as CPU exceptions, stack overflows, or kernel panics.
 * It logs the error details, including the faulting thread if available,
 * and halts the system.
 *
 * @param reason The reason code for the fatal error (see k_sys_fatal_error_handler documentation).
 * @param esf Pointer to the architecture-specific exception stack frame (may be NULL).
 */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);
	struct k_thread *faulting_thread = NULL;

	switch (reason) {
	case K_ERR_CPU_EXCEPTION: {
		LOG_ERR("Generic CPU exception, not covered by other codes");
		break;
	}
	case K_ERR_SPURIOUS_IRQ: {
		LOG_ERR("Unhandled hardware interrupt");
		break;
	}
	case K_ERR_STACK_CHK_FAIL: {
		LOG_ERR("Faulting context overflowed its stack buffer");
		/* Get the current thread that caused the fault */
		faulting_thread = k_current_get();
		if (faulting_thread) {
			LOG_ERR("Fault occurred in thread: %s", k_thread_name_get(faulting_thread));
			LOG_ERR("Thread ID: %p", (void *)faulting_thread);
			LOG_ERR("Stack start: %p, size: %zu",
				(void *)faulting_thread->stack_info.start,
				faulting_thread->stack_info.size);
		} else {
			LOG_ERR("Could not determine faulting thread");
		}
		break;
	}
	case K_ERR_KERNEL_OOPS: {
		LOG_ERR("Moderate severity software error");
		break;
	}
	case K_ERR_KERNEL_PANIC: {
		LOG_ERR("High severity software error");
		break;
	}
	case K_ERR_ARCH_START: {
		LOG_ERR("Arch specific fatal errors");
		break;
	}
	default: {
		LOG_ERR("Unknow reason for fatal error (%d)", reason);
		break;
	}
	}

	/* Disable interrupts and halt the system */
	arch_irq_lock();
	for (;;) { /* Spin endlessly */
	}
}
