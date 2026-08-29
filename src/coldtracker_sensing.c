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

#if defined(CONFIG_SHELL)
static int cmd_temp_handler_cb(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int ret;
	struct sensor_value temp = {0};
	const struct device *const dev = DEVICE_DT_GET(DT_ALIAS(coldtracker_temp));

	ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_DIE_TEMP);
	if (ret < 0) {
		shell_error(sh, "Failed to fetch temperature: %d", ret);
		return ret;
	}

	ret = sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &temp);
	if (ret < 0) {
		shell_error(sh, "Failed to get temperature: %d", ret);
		return ret;
	}

	shell_print(sh, "%d.%02d", temp.val1, temp.val2);

	return 0;
}

SHELL_CMD_ARG_REGISTER(get_temp, NULL, "Read coldtracker temperature in celsius",
		       cmd_temp_handler_cb, 1, 0);
#endif /* CONFIG_SHELL */
