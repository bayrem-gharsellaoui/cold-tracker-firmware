#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/app_version.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#ifdef CONFIG_NETWORKING
#include <time.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/timeutil.h>
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

static void restore_time_from_rtc(void)
{
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_rtc), okay)
	const struct device *rtc_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_rtc));
	struct rtc_time rtctime;
	struct timespec tspec;
	int ret;

	if (!device_is_ready(rtc_dev)) {
		LOG_ERR("RTC device %s is not ready", rtc_dev->name);
		return;
	}

	ret = rtc_get_time(rtc_dev, &rtctime);
	if (ret < 0) {
		LOG_WRN("Cannot read date time from RTC (%d)", ret);
		return;
	}

	/* Display the retrieved date and time from the RTC */
	LOG_INF("RTC date and time: %04d-%02d-%02d %02d:%02d:%02d", rtctime.tm_year + 1900,
		rtctime.tm_mon + 1, rtctime.tm_mday, rtctime.tm_hour, rtctime.tm_min,
		rtctime.tm_sec);

	/* Set the kernel real-time system clock */
	tspec.tv_sec = timeutil_timegm(rtc_time_to_tm(&rtctime));
	tspec.tv_nsec = rtctime.tm_nsec;

	ret = sys_clock_settime(SYS_CLOCK_REALTIME, &tspec);
	if (ret < 0) {
		LOG_ERR("Failed to set system clock: %d", ret);
	} else {
		LOG_INF("System clock successfully restored from RTC");
	}
#else
	LOG_WRN("zephyr,rtc chosen node not found or disabled");
#endif
}

int main(void)
{
	printk(COLDTRACKER_BOOT_BANNER, APP_VERSION_STRING, CONFIG_BOARD_TARGET);

	restore_time_from_rtc();

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
		LOG_DBG("Running in main...");
		k_msleep(1000);
	}

	return 0;
}
