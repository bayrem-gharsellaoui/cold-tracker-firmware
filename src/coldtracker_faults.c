#include <zephyr/kernel.h>
#include <zephyr/fatal.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(faults, LOG_LEVEL_DBG);

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	struct k_thread *faulting_thread = k_current_get();

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

	if (faulting_thread != NULL) {
		const char *name = k_thread_name_get(faulting_thread);

		LOG_ERR("Faulting thread: %s", name != NULL ? name : "unknown");
		LOG_ERR("Thread ID: %p", faulting_thread);

#ifdef CONFIG_THREAD_STACK_INFO
		LOG_ERR("Stack start: %p, size: %zu", (void *)faulting_thread->stack_info.start,
			faulting_thread->stack_info.size);
#endif /* CONFIG_THREAD_STACK_INFO */
	}

	LOG_PANIC();

	k_fatal_halt(reason);
}
