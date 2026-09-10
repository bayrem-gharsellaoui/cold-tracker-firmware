#include <errno.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(storage, LOG_LEVEL_DBG);

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#endif /* CONFIG_SHELL */

#include "coldtracker_storage.h"

#define STORAGE_PARTITION       sensor_data_partition
#define STORAGE_FCB_MAGIC       0xDEADBEEF
#define STORAGE_FCB_VERSION     1
#define STORAGE_FCB_SCRATCH_CNT 1

/*
 * Maximum number of flash sector descriptors ColdTracker can hold in RAM.
 *
 * Current targets:
 *   Nucleo U575     : 14 sectors
 *   native_sim      : 30 sectors
 *   XIAO ESP32-C3   : 44 sectors
 */
#define STORAGE_MAX_SECTORS 64

struct storage_walk_context {
	storage_callback_t callback;
	void *user_data;
};

K_MUTEX_DEFINE(storage_lock);

static bool storage_initialized = false;

static struct flash_sector storage_sectors[STORAGE_MAX_SECTORS] = {0};

static struct fcb storage_fcb = {
	.f_magic = STORAGE_FCB_MAGIC,
	.f_version = STORAGE_FCB_VERSION,
	.f_scratch_cnt = STORAGE_FCB_SCRATCH_CNT,
	.f_sectors = storage_sectors,
};

static struct fcb_entry read_cursor = {0};

static int storage_init(void)
{
	int ret;
	uint32_t sector_count = ARRAY_SIZE(storage_sectors);

	/* How many sectors are there in sensor_data_partition: sector_count
	 * Also put the information about these sectors in this array: storage_sectors
	 */
	ret = flash_area_get_sectors(PARTITION_ID(STORAGE_PARTITION), &sector_count,
				     storage_sectors);
	if (ret < 0) {
		LOG_ERR("Failed to get storage sectors: %d", ret);
		return ret;
	}

	storage_fcb.f_sector_cnt = (uint16_t)sector_count;

	ret = fcb_init(PARTITION_ID(STORAGE_PARTITION), &storage_fcb);
	if (ret < 0) {
		LOG_ERR("Failed to initialize FCB: %d", ret);
		return ret;
	}

	storage_initialized = true;

	LOG_INF("Storage initialized with %u sectors", sector_count);

	return 0;
}

static int storage_walk_cb(struct fcb_entry_ctx *entry_ctx, void *arg)
{
	struct storage_walk_context *ctx = arg;
	struct coldtracker_sample sample = {0};
	int ret;

	if (entry_ctx->loc.fe_data_len != sizeof(sample)) {
		LOG_WRN("Skipping invalid entry");
		return 0;
	}

	ret = flash_area_read(entry_ctx->fap, FCB_ENTRY_FA_DATA_OFF(entry_ctx->loc), &sample,
			      sizeof(sample));
	if (ret < 0) {
		LOG_ERR("Failed to read sample: %d", ret);
		return ret;
	}

	return ctx->callback(&sample, ctx->user_data);
}

int storage_foreach(storage_callback_t callback, void *user_data)
{
	struct storage_walk_context ctx = {
		.callback = callback,
		.user_data = user_data,
	};
	int ret;

	if (callback == NULL) {
		return -EINVAL;
	}

	if (!storage_initialized) {
		return -ENODEV;
	}

	k_mutex_lock(&storage_lock, K_FOREVER);

	ret = fcb_walk(&storage_fcb, NULL, storage_walk_cb, &ctx);

	k_mutex_unlock(&storage_lock);

	return ret;
}

int storage_append(const struct coldtracker_sample *sample)
{
	int ret;
	struct fcb_entry entry = {0};

	if (sample == NULL) {
		return -EINVAL;
	}

	if (!storage_initialized) {
		return -ENODEV;
	}

	k_mutex_lock(&storage_lock, K_FOREVER);

	/* Ask FCB to reserve space for the new sample */
	ret = fcb_append(&storage_fcb, sizeof(*sample), &entry);
	if (ret < 0) {
		if (ret == -ENOSPC) {
			LOG_WRN("Storage is full");
		} else {
			LOG_ERR("Failed to append FCB entry: %d", ret);
		}
		goto out;
	}

	/* Write the sample into the space allocated by FCB */
	ret = flash_area_write(storage_fcb.fap, FCB_ENTRY_FA_DATA_OFF(entry), sample,
			       sizeof(*sample));
	if (ret < 0) {
		LOG_ERR("Failed to write sample: %d", ret);
		goto out;
	}

	/* Finish the entry so FCB considers it complete */
	ret = fcb_append_finish(&storage_fcb, &entry);
	if (ret < 0) {
		LOG_ERR("Failed to finish FCB entry: %d", ret);
		goto out;
	}

	LOG_DBG("Stored sample: %lld mC @ %lld", (long long)sample->temperature_mc,
		(long long)sample->timestamp);

out:
	k_mutex_unlock(&storage_lock);

	return ret;
}

int storage_peek(struct coldtracker_sample *sample)
{
	int ret;

	if (sample == NULL) {
		return -EINVAL;
	}

	if (!storage_initialized) {
		return -ENODEV;
	}

	k_mutex_lock(&storage_lock, K_FOREVER);

	if (read_cursor.fe_sector == NULL) {
		ret = fcb_getnext(&storage_fcb, &read_cursor);
		if (ret == -ENOTSUP) {
			read_cursor = (struct fcb_entry){0};
			ret = -ENOENT;
			goto out;
		}

		if (ret < 0) {
			goto out;
		}
	}

	if (read_cursor.fe_data_len != sizeof(*sample)) {
		ret = -EINVAL;
		goto out;
	}

	ret = flash_area_read(storage_fcb.fap, FCB_ENTRY_FA_DATA_OFF(read_cursor), sample,
			      sizeof(*sample));

out:
	k_mutex_unlock(&storage_lock);

	return ret;
}

int storage_commit(void)
{
	struct fcb_entry next_entry;
	int ret;

	if (!storage_initialized) {
		return -ENODEV;
	}

	k_mutex_lock(&storage_lock, K_FOREVER);

	if (read_cursor.fe_sector == NULL) {
		ret = -ENOENT;
		goto out;
	}

	next_entry = read_cursor;

	ret = fcb_getnext(&storage_fcb, &next_entry);
	if (ret == -ENOTSUP) {
		ret = fcb_rotate(&storage_fcb);
		if (ret < 0) {
			goto out;
		}

		read_cursor = (struct fcb_entry){0};
		goto out;
	}

	if (ret < 0) {
		goto out;
	}

	if (next_entry.fe_sector != read_cursor.fe_sector) {
		ret = fcb_rotate(&storage_fcb);
		if (ret < 0) {
			goto out;
		}
	}

	read_cursor = next_entry;
	ret = 0;

out:
	k_mutex_unlock(&storage_lock);

	return ret;
}

#ifdef CONFIG_SHELL

struct history_context {
	const struct shell *sh;
	size_t count;
};

static int storage_history_cb(const struct coldtracker_sample *sample, void *user_data)
{
	struct history_context *ctx = user_data;

	shell_print(ctx->sh, "%zu: %.2f C @ %lld", ++ctx->count,
		    (double)sample->temperature_mc / 1000.0, (long long)sample->timestamp);

	return 0;
}

static int cmd_storage_history(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int ret;
	struct history_context ctx = {
		.sh = sh,
	};

	if (!storage_initialized) {
		shell_error(sh, "Storage is not initialized");
		return -ENODEV;
	}

	ret = storage_foreach(storage_history_cb, &ctx);
	if (ret < 0) {
		shell_error(sh, "Failed to read storage history: %d", ret);
		return ret;
	}

	if (ctx.count == 0) {
		shell_print(sh, "No samples stored");
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(storage_commands,
			       SHELL_CMD(history, NULL, "Show stored temperature history",
					 cmd_storage_history),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(storage, &storage_commands, "ColdTracker storage commands", NULL);

#endif /* CONFIG_SHELL */

SYS_INIT(storage_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
