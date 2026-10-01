/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

LOG_MODULE_REGISTER(faults, LOG_LEVEL_DBG);

namespace
{

/**
 * @brief Log the reason for a fatal kernel error.
 *
 * @param reason Zephyr fatal error reason.
 */
void log_fatal_reason(unsigned int reason)
{
	switch (reason) {
	case K_ERR_CPU_EXCEPTION:
		LOG_ERR("Generic CPU exception");
		break;

	case K_ERR_SPURIOUS_IRQ:
		LOG_ERR("Unhandled hardware interrupt");
		break;

	case K_ERR_STACK_CHK_FAIL:
		LOG_ERR("Stack overflow");
		break;

	case K_ERR_KERNEL_OOPS:
		LOG_ERR("Kernel oops");
		break;

	case K_ERR_KERNEL_PANIC:
		LOG_ERR("Kernel panic");
		break;

	default:
		LOG_ERR("Unknown fatal error (%u)", reason);
		break;
	}
}

/**
 * @brief Log information about the thread that caused a fatal error.
 */
void log_faulting_thread()
{
	struct k_thread *thread = k_current_get();

	if (thread == nullptr) {
		return;
	}

	const char *name = k_thread_name_get(thread);

	LOG_ERR("Faulting thread: %s", name != nullptr ? name : "unknown");
	LOG_ERR("Thread ID: %p", thread);

#ifdef CONFIG_THREAD_STACK_INFO
	LOG_ERR("Stack start: %p, size: %zu", reinterpret_cast<void *>(thread->stack_info.start),
		thread->stack_info.size);
#endif
}

} // namespace

/**
 * @brief Handle a fatal Zephyr kernel error.
 *
 * Logs the fatal error reason and information about the faulting thread,
 * flushes the logging subsystem, and halts execution.
 *
 * @param reason Zephyr fatal error reason.
 * @param esf Architecture-specific exception stack frame.
 */
extern "C" void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	log_fatal_reason(reason);
	log_faulting_thread();

	LOG_PANIC();

	k_fatal_halt(reason);
}
