/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstddef>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(heap_trap, LOG_LEVEL_INF);

namespace
{

/**
 * @brief Trap an attempted dynamic memory operation.
 *
 * @param operation Name of the attempted allocation or deallocation operation.
 */
[[noreturn]]
void heap_trap(const char *operation)
{
	LOG_ERR("Dynamic allocation attempted: %s", operation);

	__ASSERT(false, "Dynamic allocation attempted: %s", operation);

	/*
	 * __ASSERT may be compiled out, so this guarantees that
	 * dynamic allocation still fails in non-assert builds.
	 */
	k_panic();

	CODE_UNREACHABLE;
}

} // namespace

extern "C" {

/**
 * @brief Trap use of malloc().
 *
 * @param size Requested allocation size in bytes.
 *
 * @return This function never returns.
 */
void *malloc([[maybe_unused]] std::size_t size)
{
	heap_trap("malloc");
}

/**
 * @brief Trap use of calloc().
 *
 * @param num Number of elements to allocate.
 * @param size Size of each element in bytes.
 *
 * @return This function never returns.
 */
void *calloc([[maybe_unused]] std::size_t num, [[maybe_unused]] std::size_t size)
{
	heap_trap("calloc");
}

/**
 * @brief Trap use of realloc().
 *
 * @param ptr Pointer to the existing allocation.
 * @param size Requested new allocation size in bytes.
 *
 * @return This function never returns.
 */
void *realloc([[maybe_unused]] void *ptr, [[maybe_unused]] std::size_t size)
{
	heap_trap("realloc");
}

/**
 * @brief Trap use of aligned_alloc().
 *
 * @param alignment Requested allocation alignment.
 * @param size Requested allocation size in bytes.
 *
 * @return This function never returns.
 */
void *aligned_alloc([[maybe_unused]] std::size_t alignment, [[maybe_unused]] std::size_t size)
{
	heap_trap("aligned_alloc");
}

/**
 * @brief Trap use of free().
 *
 * @param ptr Pointer to the memory being released.
 */
void free(void *ptr)
{
	if (ptr != nullptr) {
		heap_trap("free");
	}
}

} // extern "C"
