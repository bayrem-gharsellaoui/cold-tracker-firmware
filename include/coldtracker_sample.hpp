/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

/**
 * @brief ColdTracker temperature sample.
 */
struct ColdTrackerSample {
	std::int64_t timestamp{};
	std::int32_t temperature_mc{};
};
