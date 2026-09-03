#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/app_version.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#ifdef CONFIG_NETWORKING
#include "coldtracker_network.h"
#endif

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

int main(void)
{
	printk(COLDTRACKER_BOOT_BANNER, APP_VERSION_STRING, CONFIG_BOARD_TARGET);

	IF_ENABLED(CONFIG_NETWORKING, (
		int ret = network_connect();
		if (ret < 0) {
			LOG_ERR("Failed to initiate network connection: %d", ret);
			return ret;
		}
		network_wait_ready();
		LOG_INF("ColdTracker is online");
	))

	while (1) {
		k_msleep(1000);
	}

	return 0;
}
