#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/app_version.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include "net_sample_common.h"
#include "coldtracker_network.h"
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include "coldtracker_ota.h"
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
	int ret;

	printk(COLDTRACKER_BOOT_BANNER, APP_VERSION_STRING, CONFIG_BOARD_TARGET);

	ret = network_connect();
	if (ret) {
		LOG_ERR("Failed to initiate network connection: %d", ret);
		return ret;
	}

	wait_for_network();

	LOG_INF("ColdTracker is online");

	while (1) {
		k_msleep(1000);
	}

	return 0;
}

#ifdef CONFIG_BOOTLOADER_MCUBOOT

#define OTA_SERVER "github.com"
#define OTA_PORT   "443"
#define OTA_FIRMWARE_PATH                                                                          \
	"/bytefull/pn532/releases/download/0.6.0/" CONFIG_KERNEL_BIN_NAME ".signed.bin"

static int cmd_update(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int ret = ota_update(OTA_SERVER, OTA_PORT, OTA_FIRMWARE_PATH);
	if (ret < 0) {
		shell_error(sh, "Firmware download failed: %d", ret);
		return ret;
	}

	shell_print(sh, "Firmware download completed");

	return 0;
}

SHELL_CMD_REGISTER(update, NULL, "Download firmware update", cmd_update);
#endif /* CONFIG_BOOTLOADER_MCUBOOT */
