/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "coldtracker_sample.hpp"

namespace coldtracker::storage
{

/**
 * @brief Callback invoked for each stored sample.
 *
 * @param sample Stored sample.
 * @param user_data User-provided callback context.
 *
 * @return 0 to continue iteration, otherwise a non-zero value to stop.
 */
using Callback = int (*)(const struct ColdTrackerSample &sample, void *user_data);

/**
 * @brief Iterate over all stored samples.
 *
 * @param callback Callback invoked for each sample.
 * @param user_data User-provided callback context.
 *
 * @return 0 on success, otherwise a negative errno value.
 */
int foreach(Callback callback, void *user_data = nullptr);

/**
 * @brief Append a sample to persistent storage.
 *
 * @param sample Sample to store.
 *
 * @return 0 on success, otherwise a negative errno value.
 */
int append(const struct ColdTrackerSample &sample);

/**
 * @brief Read the current uncommitted sample.
 *
 * Calling this function does not remove or advance the stored sample.
 *
 * @param sample Destination sample.
 *
 * @return 0 on success, otherwise a negative errno value.
 */
int peek(struct ColdTrackerSample &sample);

/**
 * @brief Commit the current sample and advance the read cursor.
 *
 * @return 0 on success, otherwise a negative errno value.
 */
int commit();

} // namespace coldtracker::storage
