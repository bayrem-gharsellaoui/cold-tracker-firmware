#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sensing, LOG_LEVEL_DBG);

static void sensing_thread_entry(void *arg1, void *arg2, void *arg3);

K_THREAD_DEFINE(sensing, 1024, sensing_thread_entry, NULL, NULL, NULL, 8, 0, 0);

static void sensing_thread_entry(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	const struct device *const dev = DEVICE_DT_GET(DT_ALIAS(coldtracker_temp));

	if (!device_is_ready(dev)) {
		LOG_ERR("Device is not ready!");
		return;
	}

	while (1) {
		k_msleep(1000);
	}
}
