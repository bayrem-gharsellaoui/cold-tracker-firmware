/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coldtracker_storage.hpp"

#include <cstddef>
#include <cstdint>

#include <zephyr/fs/fcb.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#endif

LOG_MODULE_REGISTER(storage, LOG_LEVEL_DBG);

namespace
{

#define STORAGE_PARTITION sensor_data_partition

constexpr uint32_t kFcbMagic = 0xDEADBEEF;
constexpr uint8_t kFcbVersion = 1;
constexpr uint8_t kFcbScratchCount = 1;

/*
 * Maximum number of flash sector descriptors ColdTracker can hold in RAM.
 *
 * Current targets:
 *   Nucleo U575   : 14 sectors
 *   native_sim    : 30 sectors
 *   XIAO ESP32-C3 : 44 sectors
 */
constexpr size_t kMaxSectors = 64;

struct StorageWalkContext {
	coldtracker::storage::Callback callback;
	void *user_data;
};

struct k_mutex storage_lock{};
bool storage_initialized{};

struct flash_sector storage_sectors[kMaxSectors]{};
struct fcb storage_fcb{};
struct fcb_entry read_cursor{};

/**
 * @brief Initialize persistent sample storage.
 *
 * @return 0 on success, otherwise a negative errno value.
 */
int storage_init()
{
	int ret = k_mutex_init(&storage_lock);

	if (ret < 0) {
		LOG_ERR("Failed to initialize storage mutex: %d", ret);
		return ret;
	}

	uint32_t sector_count = ARRAY_SIZE(storage_sectors);

	ret = flash_area_get_sectors(PARTITION_ID(STORAGE_PARTITION), &sector_count,
				     storage_sectors);

	if (ret < 0) {
		LOG_ERR("Failed to get storage sectors: %d", ret);
		return ret;
	}

	storage_fcb.f_magic = kFcbMagic;
	storage_fcb.f_version = kFcbVersion;
	storage_fcb.f_scratch_cnt = kFcbScratchCount;
	storage_fcb.f_sectors = storage_sectors;
	storage_fcb.f_sector_cnt = static_cast<uint16_t>(sector_count);

	ret = fcb_init(PARTITION_ID(STORAGE_PARTITION), &storage_fcb);

	if (ret < 0) {
		LOG_ERR("Failed to initialize FCB: %d", ret);
		return ret;
	}

	storage_initialized = true;

	LOG_INF("Storage initialized with %u sectors", sector_count);

	return 0;
}

/**
 * @brief FCB walk adapter.
 *
 * @param entry_ctx Current FCB entry.
 * @param arg Storage walk context.
 *
 * @return Callback result or a negative errno value.
 */
int storage_walk_callback(struct fcb_entry_ctx *entry_ctx, void *arg)
{
	auto *context = static_cast<StorageWalkContext *>(arg);

	struct ColdTrackerSample sample{};

	if (entry_ctx->loc.fe_data_len != sizeof(sample)) {
		LOG_WRN("Skipping invalid entry");
		return 0;
	}

	const int ret = flash_area_read(entry_ctx->fap, FCB_ENTRY_FA_DATA_OFF(entry_ctx->loc),
					&sample, sizeof(sample));

	if (ret < 0) {
		LOG_ERR("Failed to read sample: %d", ret);
		return ret;
	}

	return context->callback(sample, context->user_data);
}

#ifdef CONFIG_SHELL

struct HistoryContext {
	const struct shell *shell;
	size_t count;
};

/**
 * @brief Print a stored sample to the shell.
 *
 * @param sample Stored sample.
 * @param user_data History context.
 *
 * @return 0.
 */
int storage_history_callback(const struct ColdTrackerSample &sample, void *user_data)
{
	auto *context = static_cast<HistoryContext *>(user_data);

	shell_print(context->shell, "%zu: %.2f C @ %lld", ++context->count,
		    static_cast<double>(sample.temperature_mc) / 1000.0,
		    static_cast<long long>(sample.timestamp));

	return 0;
}

/**
 * @brief Shell command for displaying stored temperature history.
 *
 * @param shell Shell instance.
 * @param argc Argument count.
 * @param argv Argument vector.
 *
 * @return 0 on success, otherwise a negative errno value.
 */
int cmd_storage_history(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	HistoryContext context{
		.shell = shell,
		.count = 0,
	};

	const int ret = coldtracker::storage::foreach(storage_history_callback, &context);

	if (ret == -ENODEV) {
		shell_error(shell, "Storage is not initialized");
		return ret;
	}

	if (ret < 0) {
		shell_error(shell, "Failed to read storage history: %d", ret);

		return ret;
	}

	if (context.count == 0U) {
		shell_print(shell, "No samples stored");
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(storage_commands,
			       SHELL_CMD(history, NULL, "Show stored temperature history",
					 cmd_storage_history),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(storage, &storage_commands, "ColdTracker storage commands", NULL);

#endif /* CONFIG_SHELL */

} // namespace

int coldtracker::storage::foreach(Callback callback, void *user_data)
{
	if (callback == nullptr) {
		return -EINVAL;
	}

	if (!storage_initialized) {
		return -ENODEV;
	}

	StorageWalkContext context{
		.callback = callback,
		.user_data = user_data,
	};

	k_mutex_lock(&storage_lock, K_FOREVER);

	const int ret = fcb_walk(&storage_fcb, nullptr, storage_walk_callback, &context);

	k_mutex_unlock(&storage_lock);

	return ret;
}

int coldtracker::storage::append(const struct ColdTrackerSample &sample)
{
	if (!storage_initialized) {
		return -ENODEV;
	}

	struct fcb_entry entry{};

	k_mutex_lock(&storage_lock, K_FOREVER);

	int ret = fcb_append(&storage_fcb, sizeof(sample), &entry);

	if (ret < 0) {
		if (ret == -ENOSPC) {
			LOG_WRN("Storage is full");
		} else {
			LOG_ERR("Failed to append FCB entry: %d", ret);
		}

		goto out;
	}

	ret = flash_area_write(storage_fcb.fap, FCB_ENTRY_FA_DATA_OFF(entry), &sample,
			       sizeof(sample));

	if (ret < 0) {
		LOG_ERR("Failed to write sample: %d", ret);
		goto out;
	}

	ret = fcb_append_finish(&storage_fcb, &entry);

	if (ret < 0) {
		LOG_ERR("Failed to finish FCB entry: %d", ret);
		goto out;
	}

	LOG_DBG("Stored sample: %lld mC @ %lld", static_cast<long long>(sample.temperature_mc),
		static_cast<long long>(sample.timestamp));

out:
	k_mutex_unlock(&storage_lock);

	return ret;
}

int coldtracker::storage::peek(struct ColdTrackerSample &sample)
{
	if (!storage_initialized) {
		return -ENODEV;
	}

	k_mutex_lock(&storage_lock, K_FOREVER);

	int ret = 0;

	if (read_cursor.fe_sector == nullptr) {
		ret = fcb_getnext(&storage_fcb, &read_cursor);

		if (ret == -ENOTSUP) {
			read_cursor = {};
			ret = -ENOENT;
			goto out;
		}

		if (ret < 0) {
			goto out;
		}
	}

	if (read_cursor.fe_data_len != sizeof(sample)) {
		ret = -EINVAL;
		goto out;
	}

	ret = flash_area_read(storage_fcb.fap, FCB_ENTRY_FA_DATA_OFF(read_cursor), &sample,
			      sizeof(sample));

out:
	k_mutex_unlock(&storage_lock);

	return ret;
}

int coldtracker::storage::commit()
{
	if (!storage_initialized) {
		return -ENODEV;
	}

	struct fcb_entry next_entry{};

	k_mutex_lock(&storage_lock, K_FOREVER);

	int ret = 0;

	if (read_cursor.fe_sector == nullptr) {
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

		read_cursor = {};
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

SYS_INIT(storage_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
