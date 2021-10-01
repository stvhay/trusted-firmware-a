/*
 * Copyright (c) 2013-2020, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>

#include <arch_features.h>
#include <arch_helpers.h>
#include <bl32/tsp/tsp.h>
#include <common/bl_common.h>
#include <common/debug.h>
#include <lib/spinlock.h>
#include <plat/common/platform.h>
#include <platform_def.h>
#include <platform_tsp.h>
#if SPMC_AT_EL3
#include <services/ffa_svc.h>
#include <lib/psci/psci.h>

#include "ffa_helpers.h"
#include <lib/xlat_tables/xlat_tables_defs.h>
#include <lib/xlat_tables/xlat_tables_v2.h>

#endif

#include "tsp_private.h"


/*******************************************************************************
 * Lock to control access to the console
 ******************************************************************************/
spinlock_t console_lock;

/*******************************************************************************
 * Per cpu data structure to populate parameters for an SMC in C code and use
 * a pointer to this structure in assembler code to populate x0-x7
 ******************************************************************************/
static tsp_args_t tsp_smc_args[PLATFORM_CORE_COUNT];

/*******************************************************************************
 * Per cpu data structure to keep track of TSP activity
 ******************************************************************************/
work_statistics_t tsp_stats[PLATFORM_CORE_COUNT];

/*******************************************************************************
 * The TSP memory footprint starts at address BL32_BASE and ends with the
 * linker symbol __BL32_END__. Use these addresses to compute the TSP image
 * size.
 ******************************************************************************/
#define BL32_TOTAL_LIMIT BL32_END
#define BL32_TOTAL_SIZE (BL32_TOTAL_LIMIT - (unsigned long) BL32_BASE)

#if SPMC_AT_EL3
static unsigned int spmc_id;
static unsigned int partition_id;

/* Partition Mailbox */
static uint8_t send_page[PAGE_SIZE] __aligned(PAGE_SIZE);
static uint8_t recv_page[PAGE_SIZE] __aligned(PAGE_SIZE);

struct mailbox {
	void* send;
	const void* recv;
};
struct mailbox mailbox;

#endif

tsp_args_t tsp_smc(uint32_t func, uint64_t arg0,
			  uint64_t arg1, uint64_t arg2,
			  uint64_t arg3, uint64_t arg4,
			  uint64_t arg5, uint64_t arg6)
{
	tsp_args_t ret_args = {0};
	register uint64_t r0 __asm__("x0") = func;
	register uint64_t r1 __asm__("x1") = arg0;
	register uint64_t r2 __asm__("x2") = arg1;
	register uint64_t r3 __asm__("x3") = arg2;
	register uint64_t r4 __asm__("x4") = arg3;
	register uint64_t r5 __asm__("x5") = arg4;
	register uint64_t r6 __asm__("x6") = arg5;
	register uint64_t r7 __asm__("x7") = arg6;

	__asm__ volatile(
		                "smc #0"
				: /* Output registers, also used as inputs ('+' constraint). */
				  "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3), "+r"(r4), "+r"(r5),
				  "+r"(r6), "+r"(r7));

	ret_args._regs[0] = r0;
	ret_args._regs[1] = r1;
	ret_args._regs[2] = r2;
	ret_args._regs[3] = r3;
	ret_args._regs[4] = r4;
	ret_args._regs[5] = r5;
	ret_args._regs[6] = r6;
	ret_args._regs[7] = r7;

	return ret_args;
}

static tsp_args_t *set_smc_args(uint64_t arg0,
				uint64_t arg1,
				uint64_t arg2,
				uint64_t arg3,
				uint64_t arg4,
				uint64_t arg5,
				uint64_t arg6,
				uint64_t arg7)
{
	uint32_t linear_id;
	tsp_args_t *pcpu_smc_args;

	/*
	 * Return to Secure Monitor by raising an SMC. The results of the
	 * service are passed as an arguments to the SMC
	 */
	linear_id = plat_my_core_pos();
	pcpu_smc_args = &tsp_smc_args[linear_id];
	write_sp_arg(pcpu_smc_args, TSP_ARG0, arg0);
	write_sp_arg(pcpu_smc_args, TSP_ARG1, arg1);
	write_sp_arg(pcpu_smc_args, TSP_ARG2, arg2);
	write_sp_arg(pcpu_smc_args, TSP_ARG3, arg3);
	write_sp_arg(pcpu_smc_args, TSP_ARG4, arg4);
	write_sp_arg(pcpu_smc_args, TSP_ARG5, arg5);
	write_sp_arg(pcpu_smc_args, TSP_ARG6, arg6);
	write_sp_arg(pcpu_smc_args, TSP_ARG7, arg7);

	return pcpu_smc_args;
}

/*******************************************************************************
 * Setup function for TSP.
 ******************************************************************************/
void tsp_setup(void)
{
	/* Perform early platform-specific setup */
	tsp_early_platform_setup();

	/* Perform late platform-specific setup */
	tsp_plat_arch_setup();

#if ENABLE_PAUTH
	/*
	 * Assert that the ARMv8.3-PAuth registers are present or an access
	 * fault will be triggered when they are being saved or restored.
	 */
	assert(is_armv8_3_pauth_present());
#endif /* ENABLE_PAUTH */
}

/*******************************************************************************
 * TSP main entry point where it gets the opportunity to initialize its secure
 * state/applications. Once the state is initialized, it must return to the
 * SPD with a pointer to the 'tsp_vector_table' jump table.
 ******************************************************************************/
#if SPMC_AT_EL3
tsp_args_t *tsp_main(uintptr_t secondary_ep)
#else
uint64_t tsp_main(void)
#endif
{
	NOTICE("TSP: %s\n", version_string);
	NOTICE("TSP: %s\n", build_message);
	INFO("TSP: Total memory base : 0x%lx\n", (unsigned long) BL32_BASE);
	INFO("TSP: Total memory size : 0x%lx bytes\n", BL32_TOTAL_SIZE);

	uint32_t linear_id = plat_my_core_pos();

	/* Initialize the platform */
	tsp_platform_setup();

	/* Initialize secure/applications state here */
	tsp_generic_timer_start();

#if SPMC_AT_EL3
	{
		tsp_args_t smc_args = {0};

		/* Register secondary entrypoint with the SPMC. */
		smc_args = tsp_smc(FFA_SECONDARY_EP_REGISTER_SMC64,
				   (uint64_t) secondary_ep,
				   0, 0, 0, 0, 0, 0);
		if (smc_args._regs[TSP_ARG0] != FFA_SUCCESS_SMC32)
			ERROR("TSP could not register secondary ep (0x%llx)\n",
			      smc_args._regs[2]);

		/* Get TSP's endpoint id */
		smc_args = tsp_smc(FFA_ID_GET, 0, 0, 0, 0, 0, 0, 0);
		if (smc_args._regs[TSP_ARG0] != FFA_SUCCESS_SMC32) {
			ERROR("TSP could not get own ID (0x%llx) on core%d\n",
			      smc_args._regs[2], linear_id);
			panic();
		}

		INFO("TSP FF-A endpoint id = 0x%llx \n", smc_args._regs[2]);
		partition_id =  smc_args._regs[2];

		/* Get the SPMC ID */
		smc_args = tsp_smc(FFA_SPM_ID_GET, 0, 0, 0, 0, 0, 0, 0);
		if (smc_args._regs[TSP_ARG0] != FFA_SUCCESS_SMC32) {
			ERROR("TSP could not get SPMC ID (0x%llx) on core%d\n",
			      smc_args._regs[2], linear_id);
			panic();
		}

		spmc_id = smc_args._regs[2];

		/* Call RXTX_MAP to map a 4k RX and TX buffer. */
		if (ffa_rxtx_map((uintptr_t) send_page, (uintptr_t) recv_page, 1)) {
			ERROR("TSP could not map it's RX/TX Buffers\n");
			panic();
		}

		mailbox.send = send_page;
		mailbox.recv = recv_page;
	}
#endif
	/* Update this cpu's statistics */
	tsp_stats[linear_id].smc_count++;
	tsp_stats[linear_id].eret_count++;
	tsp_stats[linear_id].cpu_on_count++;

#if LOG_LEVEL >= LOG_LEVEL_INFO
	spin_lock(&console_lock);
	INFO("TSP: cpu 0x%lx: %d smcs, %d erets %d cpu on requests\n",
	     read_mpidr(),
	     tsp_stats[linear_id].smc_count,
	     tsp_stats[linear_id].eret_count,
	     tsp_stats[linear_id].cpu_on_count);
	spin_unlock(&console_lock);
#endif
#if SPMC_AT_EL3
	return set_smc_args(FFA_MSG_WAIT, 0, 0, 0, 0, 0, 0, 0);
#else
	return (uint64_t) &tsp_vector_table;
#endif
}

/*******************************************************************************
 * This function performs any remaining book keeping in the test secure payload
 * after this cpu's architectural state has been setup in response to an earlier
 * psci cpu_on request.
 ******************************************************************************/
tsp_args_t *tsp_cpu_on_main(void)
{
	uint32_t linear_id = plat_my_core_pos();

	/* Initialize secure/applications state here */
	tsp_generic_timer_start();

	/* Update this cpu's statistics */
	tsp_stats[linear_id].smc_count++;
	tsp_stats[linear_id].eret_count++;
	tsp_stats[linear_id].cpu_on_count++;

#if LOG_LEVEL >= LOG_LEVEL_INFO
	spin_lock(&console_lock);
	INFO("TSP: cpu 0x%lx turned on\n", read_mpidr());
	INFO("TSP: cpu 0x%lx: %d smcs, %d erets %d cpu on requests\n",
		read_mpidr(),
		tsp_stats[linear_id].smc_count,
		tsp_stats[linear_id].eret_count,
		tsp_stats[linear_id].cpu_on_count);
	spin_unlock(&console_lock);
#endif

#if SPMC_AT_EL3
	return set_smc_args(FFA_MSG_WAIT, 0, 0, 0, 0, 0, 0, 0);
#else
	/* Indicate to the SPD that we have completed turned ourselves on */
	return set_smc_args(TSP_ON_DONE, 0, 0, 0, 0, 0, 0, 0);
#endif
}

/*******************************************************************************
 * This function performs any remaining book keeping in the test secure payload
 * before this cpu is turned off in response to a psci cpu_off request.
 ******************************************************************************/
tsp_args_t *tsp_cpu_off_main(uint64_t arg0,
			   uint64_t arg1,
			   uint64_t arg2,
			   uint64_t arg3,
			   uint64_t arg4,
			   uint64_t arg5,
			   uint64_t arg6,
			   uint64_t arg7)
{
	uint32_t linear_id = plat_my_core_pos();

	/*
	 * This cpu is being turned off, so disable the timer to prevent the
	 * secure timer interrupt from interfering with power down. A pending
	 * interrupt will be lost but we do not care as we are turning off.
	 */
	tsp_generic_timer_stop();

	/* Update this cpu's statistics */
	tsp_stats[linear_id].smc_count++;
	tsp_stats[linear_id].eret_count++;
	tsp_stats[linear_id].cpu_off_count++;

#if LOG_LEVEL >= LOG_LEVEL_INFO
	spin_lock(&console_lock);
	INFO("TSP: cpu 0x%lx off request\n", read_mpidr());
	INFO("TSP: cpu 0x%lx: %d smcs, %d erets %d cpu off requests\n",
		read_mpidr(),
		tsp_stats[linear_id].smc_count,
		tsp_stats[linear_id].eret_count,
		tsp_stats[linear_id].cpu_off_count);
	spin_unlock(&console_lock);
#endif

#if SPMC_AT_EL3
	{
		unsigned int tsp_id;
		tsp_args_t smc_args = {0};

		/* Get the TSP ID */
		smc_args = tsp_smc(FFA_ID_GET, 0, 0, 0, 0, 0, 0, 0);
		if (smc_args._regs[TSP_ARG0] != FFA_SUCCESS_SMC32) {
			ERROR("TSP could not get own ID (0x%llx) on core%d\n",
			      smc_args._regs[2], linear_id);
			panic();
		}

		tsp_id = smc_args._regs[2];

		return set_smc_args(FFA_MSG_SEND_DIRECT_RESP_SMC32,
				    tsp_id << FFA_DIRECT_MSG_SOURCE_SHIFT |
				    spmc_id,
				    FFA_DIRECT_FRAMEWORK_MSG_MASK |
				    (FFA_PM_MSG_PM_RESP & FFA_PM_MSG_MASK),
				    0, 0, 0, 0, 0);
	}
#else
	/* Indicate to the SPD that we have completed this request */
	return set_smc_args(TSP_OFF_DONE, 0, 0, 0, 0, 0, 0, 0);
#endif
}

/*******************************************************************************
 * This function performs any book keeping in the test secure payload before
 * this cpu's architectural state is saved in response to an earlier psci
 * cpu_suspend request.
 ******************************************************************************/
tsp_args_t *tsp_cpu_suspend_main(uint64_t arg0,
			       uint64_t arg1,
			       uint64_t arg2,
			       uint64_t arg3,
			       uint64_t arg4,
			       uint64_t arg5,
			       uint64_t arg6,
			       uint64_t arg7)
{
	uint32_t linear_id = plat_my_core_pos();

	/*
	 * Save the time context and disable it to prevent the secure timer
	 * interrupt from interfering with wakeup from the suspend state.
	 */
	tsp_generic_timer_save();
	tsp_generic_timer_stop();

	/* Update this cpu's statistics */
	tsp_stats[linear_id].smc_count++;
	tsp_stats[linear_id].eret_count++;
	tsp_stats[linear_id].cpu_suspend_count++;

#if LOG_LEVEL >= LOG_LEVEL_INFO
	spin_lock(&console_lock);
	INFO("TSP: cpu 0x%lx: %d smcs, %d erets %d cpu suspend requests\n",
		read_mpidr(),
		tsp_stats[linear_id].smc_count,
		tsp_stats[linear_id].eret_count,
		tsp_stats[linear_id].cpu_suspend_count);
	spin_unlock(&console_lock);
#endif

	/* Indicate to the SPD that we have completed this request */
	return set_smc_args(TSP_SUSPEND_DONE, 0, 0, 0, 0, 0, 0, 0);
}

/*******************************************************************************
 * This function performs any book keeping in the test secure payload after this
 * cpu's architectural state has been restored after wakeup from an earlier psci
 * cpu_suspend request.
 ******************************************************************************/
tsp_args_t *tsp_cpu_resume_main(uint64_t max_off_pwrlvl,
			      uint64_t arg1,
			      uint64_t arg2,
			      uint64_t arg3,
			      uint64_t arg4,
			      uint64_t arg5,
			      uint64_t arg6,
			      uint64_t arg7)
{
	uint32_t linear_id = plat_my_core_pos();

	/* Restore the generic timer context */
	tsp_generic_timer_restore();

	/* Update this cpu's statistics */
	tsp_stats[linear_id].smc_count++;
	tsp_stats[linear_id].eret_count++;
	tsp_stats[linear_id].cpu_resume_count++;

#if LOG_LEVEL >= LOG_LEVEL_INFO
	spin_lock(&console_lock);
	INFO("TSP: cpu 0x%lx resumed. maximum off power level %lld\n",
	     read_mpidr(), max_off_pwrlvl);
	INFO("TSP: cpu 0x%lx: %d smcs, %d erets %d cpu resume requests\n",
		read_mpidr(),
		tsp_stats[linear_id].smc_count,
		tsp_stats[linear_id].eret_count,
		tsp_stats[linear_id].cpu_resume_count);
	spin_unlock(&console_lock);
#endif
	/* Indicate to the SPD that we have completed this request */
	return set_smc_args(TSP_RESUME_DONE, 0, 0, 0, 0, 0, 0, 0);
}

/*******************************************************************************
 * This function performs any remaining bookkeeping in the test secure payload
 * before the system is switched off (in response to a psci SYSTEM_OFF request)
 ******************************************************************************/
tsp_args_t *tsp_system_off_main(uint64_t arg0,
				uint64_t arg1,
				uint64_t arg2,
				uint64_t arg3,
				uint64_t arg4,
				uint64_t arg5,
				uint64_t arg6,
				uint64_t arg7)
{
	uint32_t linear_id = plat_my_core_pos();

	/* Update this cpu's statistics */
	tsp_stats[linear_id].smc_count++;
	tsp_stats[linear_id].eret_count++;

#if LOG_LEVEL >= LOG_LEVEL_INFO
	spin_lock(&console_lock);
	INFO("TSP: cpu 0x%lx SYSTEM_OFF request\n", read_mpidr());
	INFO("TSP: cpu 0x%lx: %d smcs, %d erets requests\n", read_mpidr(),
	     tsp_stats[linear_id].smc_count,
	     tsp_stats[linear_id].eret_count);
	spin_unlock(&console_lock);
#endif

	/* Indicate to the SPD that we have completed this request */
	return set_smc_args(TSP_SYSTEM_OFF_DONE, 0, 0, 0, 0, 0, 0, 0);
}

/*******************************************************************************
 * This function performs any remaining bookkeeping in the test secure payload
 * before the system is reset (in response to a psci SYSTEM_RESET request)
 ******************************************************************************/
tsp_args_t *tsp_system_reset_main(uint64_t arg0,
				uint64_t arg1,
				uint64_t arg2,
				uint64_t arg3,
				uint64_t arg4,
				uint64_t arg5,
				uint64_t arg6,
				uint64_t arg7)
{
	uint32_t linear_id = plat_my_core_pos();

	/* Update this cpu's statistics */
	tsp_stats[linear_id].smc_count++;
	tsp_stats[linear_id].eret_count++;

#if LOG_LEVEL >= LOG_LEVEL_INFO
	spin_lock(&console_lock);
	INFO("TSP: cpu 0x%lx SYSTEM_RESET request\n", read_mpidr());
	INFO("TSP: cpu 0x%lx: %d smcs, %d erets requests\n", read_mpidr(),
	     tsp_stats[linear_id].smc_count,
	     tsp_stats[linear_id].eret_count);
	spin_unlock(&console_lock);
#endif

	/* Indicate to the SPD that we have completed this request */
	return set_smc_args(TSP_SYSTEM_RESET_DONE, 0, 0, 0, 0, 0, 0, 0);
}

/*******************************************************************************
 * TSP fast smc handler. The secure monitor jumps to this function by
 * doing the ERET after populating X0-X7 registers. The arguments are received
 * in the function arguments in order. Once the service is rendered, this
 * function returns to Secure Monitor by raising SMC.
 ******************************************************************************/
tsp_args_t *tsp_smc_handler(uint64_t func,
			       uint64_t arg1,
			       uint64_t arg2,
			       uint64_t arg3,
			       uint64_t arg4,
			       uint64_t arg5,
			       uint64_t arg6,
			       uint64_t arg7)
{
	uint128_t service_args;
	uint64_t service_arg0;
	uint64_t service_arg1;
	uint64_t results[2];
	uint32_t linear_id = plat_my_core_pos();

	/* Update this cpu's statistics */
	tsp_stats[linear_id].smc_count++;
	tsp_stats[linear_id].eret_count++;

#if LOG_LEVEL >= LOG_LEVEL_INFO
	spin_lock(&console_lock);
	INFO("TSP: cpu 0x%lx received %s smc 0x%llx\n", read_mpidr(),
		((func >> 31) & 1) == 1 ? "fast" : "yielding",
		func);
	INFO("TSP: cpu 0x%lx: %d smcs, %d erets\n", read_mpidr(),
		tsp_stats[linear_id].smc_count,
		tsp_stats[linear_id].eret_count);
	spin_unlock(&console_lock);
#endif

	/* Render secure services and obtain results here */
	results[0] = arg1;
	results[1] = arg2;

	/*
	 * Request a service back from dispatcher/secure monitor.
	 * This call returns and thereafter resumes execution.
	 */
	service_args = tsp_get_magic();
	service_arg0 = (uint64_t)service_args;
	service_arg1 = (uint64_t)(service_args >> 64U);

#if CTX_INCLUDE_MTE_REGS
	/*
	 * Write a dummy value to an MTE register, to simulate usage in the
	 * secure world
	 */
	write_gcr_el1(0x99);
#endif

	/* Determine the function to perform based on the function ID */
	switch (TSP_BARE_FID(func)) {
	case TSP_ADD:
		results[0] += service_arg0;
		results[1] += service_arg1;
		break;
	case TSP_SUB:
		results[0] -= service_arg0;
		results[1] -= service_arg1;
		break;
	case TSP_MUL:
		results[0] *= service_arg0;
		results[1] *= service_arg1;
		break;
	case TSP_DIV:
		results[0] /= service_arg0 ? service_arg0 : 1;
		results[1] /= service_arg1 ? service_arg1 : 1;
		break;
	default:
		break;
	}

	return set_smc_args(func, 0,
			    results[0],
			    results[1],
			    0, 0, 0, 0);
}

/*******************************************************************************
 * TSP smc abort handler. This function is called when aborting a preempted
 * yielding SMC request. It should cleanup all resources owned by the SMC
 * handler such as locks or dynamically allocated memory so following SMC
 * request are executed in a clean environment.
 ******************************************************************************/
tsp_args_t *tsp_abort_smc_handler(uint64_t func,
				  uint64_t arg1,
				  uint64_t arg2,
				  uint64_t arg3,
				  uint64_t arg4,
				  uint64_t arg5,
				  uint64_t arg6,
				  uint64_t arg7)
{
	return set_smc_args(TSP_ABORT_DONE, 0, 0, 0, 0, 0, 0, 0);
}

#if SPMC_AT_EL3

/*******************************************************************************
 * This enum is used to handle test cases driven from the FFA Test Driver
 ******************************************************************************/
/* Keep in Sync with FF-A Test Driver */
enum message_t
{
	/* Partition Only Messages. */
	FF_A_RELAY_MESSAGE = 0,

	/* Basic Functionality. */
	FF_A_ECHO_MESSAGE,
	FF_A_RELAY_MESSAGE_EL3,

	/* Memory Sharing. */
	FF_A_MEMORY_SHARE,
	FF_A_MEMORY_SHARE_FRAGMENTED,
	FF_A_MEMORY_LEND,
	FF_A_MEMORY_LEND_FRAGMENTED,

	LAST,
	FF_A_RUN_ALL = 255,
	FF_A_OP_MAX = 256
};


/*******************************************************************************
 * This function handles framework messages. Currently only PM.
 ******************************************************************************/
tsp_args_t *handle_framework_message(uint64_t arg0,
				     uint64_t arg1,
				     uint64_t arg2,
				     uint64_t arg3,
				     uint64_t arg4,
				     uint64_t arg5,
				     uint64_t arg6,
				     uint64_t arg7)
{

	/*
	* Check if it is a power management message from the SPMC to
	* turn off this cpu else barf for now.
	*/
	if (FFA_SENDER(arg1) != spmc_id)
		goto err;

	/* Check it is a PM request message */
	if ((arg2 & FFA_PM_MSG_MASK) != FFA_PM_MSG_PSCI_REQ)
		goto err;

	/* Check it is a PSCI CPU_OFF request */
	if (arg3 != PSCI_CPU_OFF)
		goto err;

	/* Everything checks out. Do the needful */
	return tsp_cpu_off_main(arg0, arg1, arg2, arg3,
				arg4, arg5, arg6, arg7);
err:
	/* TODO Add support in SPMC for FFA_ERROR. */
	return set_smc_args(FFA_ERROR, 0, 0, 0, 0, 0, 0, 0);
}

/*******************************************************************************
 * Helper function tow swap source and destination partition IDs
 ******************************************************************************/
void swap_src_dst(uint16_t *src, uint16_t *dst)
{
	uint32_t tmp;
	tmp = *src;
	*src = *dst;
	*dst = tmp;
}

/*******************************************************************************
 * Wrapper function to send a direct response
 ******************************************************************************/
tsp_args_t *ffa_msg_send_direct_resp(uint16_t sender,
			      uint16_t receiver,
			      uint32_t arg3,
			      uint32_t arg4,
			      uint32_t arg5,
			      uint32_t arg6,
			      uint32_t arg7)
{
	uint32_t flags = 0;
	uint32_t src_dst_ids = (sender << FFA_DIRECT_MSG_SOURCE_SHIFT) |
			       (receiver << FFA_DIRECT_MSG_DESTINATION_SHIFT);

	return set_smc_args(FFA_MSG_SEND_DIRECT_RESP_SMC64, src_dst_ids,
			    flags, arg3, arg4, arg5, arg6, arg7);
}

/*******************************************************************************
* Wrapper function to send a direct request
 ******************************************************************************/
tsp_args_t ffa_msg_send_direct_req(uint16_t sender,
			      uint16_t receiver,
			      uint32_t arg3,
			      uint32_t arg4,
			      uint32_t arg5,
			      uint32_t arg6,
			      uint32_t arg7)
{
	uint32_t flags = 0;
	uint32_t src_dst_ids = (sender << FFA_DIRECT_MSG_SOURCE_SHIFT) |
			       (receiver << FFA_DIRECT_MSG_DESTINATION_SHIFT);


	/* Send Direct Request. */
	return tsp_smc(FFA_MSG_SEND_DIRECT_REQ_SMC64, src_dst_ids,
			flags, arg3, arg4, arg5, arg6, arg7);
}

/*******************************************************************************
* Wrapper function to call FFA_RUN
 ******************************************************************************/
tsp_args_t ffa_run(uint16_t target, uint16_t vcpu)
{
	uint32_t target_info = FFA_RUN_TARGET(target) | FFA_RUN_VCPU(vcpu);

	/* Send Direct Request. */
	return tsp_smc(FFA_MSG_RUN, target_info,
			FFA_PARAM_MBZ, FFA_PARAM_MBZ, FFA_PARAM_MBZ,
			FFA_PARAM_MBZ, FFA_PARAM_MBZ, FFA_PARAM_MBZ);
}

/*******************************************************************************
 *  Wrapper to handle BUSY and INTERRUPT error codes when sending a direct request.
 ******************************************************************************/
tsp_args_t ffa_direct_req_wrapper(
	uint16_t sender, uint16_t receiver, uint32_t arg3,
	uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7)
{

	tsp_args_t ret;

	/* Handle initial busy Error Code */
	ret = ffa_msg_send_direct_req(sender, receiver, arg3, arg4, arg5, arg6, arg7);
	while (ret._regs[0] == FFA_ERROR && ret._regs[2] == FFA_ERROR_BUSY) {
		ret = ffa_msg_send_direct_req(sender, receiver, arg3, arg4, arg5, arg6, arg7);
	}

	/* We've sent the direct request and been interrupted, keep running until completes. */
	while (ret._regs[0] == FFA_INTERRUPT) {
		ret = ffa_run((ret._regs[1] >> FFA_DIRECT_MSG_SOURCE_SHIFT) & FFA_DIRECT_MSG_ENDPOINT_ID_MASK,
			       ret._regs[1] & FFA_DIRECT_MSG_ENDPOINT_ID_MASK);
	}

	return ret;
}

/*******************************************************************************
 * Test Functions
 ******************************************************************************/
int ffa_test_relay(uint64_t arg0,
		   uint64_t arg1,
		   uint64_t arg2,
		   uint64_t arg3,
		   uint64_t arg4,
		   uint64_t arg5,
		   uint64_t arg6,
		   uint64_t arg7)
{
	tsp_args_t ffa_forward_result;
	uint32_t receiver = arg5;
	ffa_forward_result = ffa_direct_req_wrapper(FFA_SENDER(arg1), receiver, FF_A_ECHO_MESSAGE, arg4, 0, 0, 0);
	return ffa_forward_result._regs[3];
}


/*******************************************************************************
 * Memory Management Helpers
 ******************************************************************************/
static char mem_region_buffer[4096 * 2]  __aligned(PAGE_SIZE);
#define REGION_BUF_SIZE sizeof(mem_region_buffer)

bool memory_retrieve(struct mailbox *mb,
		     struct ffa_memory_region **retrieved, uint64_t handle,
		     ffa_id_t sender, ffa_id_t receiver,
		     uint32_t flags, uint32_t *frag_length, uint32_t *total_length )
{
	tsp_args_t ret;
	uint32_t descriptor_size;

	if (retrieved == NULL || mb == NULL) {
		ERROR("Invalid parameters!\n");
		return false;
	}

	/* Clear TX buffer. */
	memset(mb->send, 0, PAGE_SIZE);

	/* Clear local buffer. */
	memset(mem_region_buffer, 0, REGION_BUF_SIZE);

	/*
	 * TODO: Revise shareability attribute in function call
	 * below.
	 * https://lists.trustedfirmware.org/pipermail/hafnium/2020-June/000023.html
	 */
	descriptor_size = ffa_memory_retrieve_request_init(
	    mb->send, handle, sender, receiver, 0, flags,
	    FFA_DATA_ACCESS_RW,
	    FFA_INSTRUCTION_ACCESS_NX,
	    FFA_MEMORY_NORMAL_MEM,
	    FFA_MEMORY_CACHE_WRITE_BACK,
	    FFA_MEMORY_OUTER_SHAREABLE);

	ret = ffa_mem_retrieve_req(descriptor_size, descriptor_size);

	if (ffa_func_id(ret) == FFA_ERROR) {
		ERROR("Couldn't retrieve the memory page. Error: %x\n",
		      ffa_error_code(ret));
		return false;
	}

	/*
	 * Following total_size and fragment_size are useful to keep track
	 * of the state of transaction. When the sum of all fragment_size of all
	 * fragments is equal to total_size, the memory transaction has been
	 * completed.
	 */
	*total_length = ret._regs[1];
	*frag_length = ret._regs[2];

	/* Copy reponse to local buffer. */
	memcpy(mem_region_buffer, mb->recv, *frag_length);

        if (ffa_rx_release()) {
                ERROR("Failed to release buffer!\n");
                return false;
       }

	*retrieved = (struct ffa_memory_region *) mem_region_buffer;

	if ((*retrieved)->receiver_count > MAX_MEM_SHARE_RECIPIENTS) {
		VERBOSE("SPMC memory sharing operations support max of %u "
			"receivers!\n", MAX_MEM_SHARE_RECIPIENTS);
		return false;
	}

	VERBOSE("Memory Descriptor Retrieved!\n");

	return true;
}

/*******************************************************************************
 * This function handles memory management tests, currently share and lend.
 ******************************************************************************/
int test_memory_send(uint16_t sender, uint64_t handle, bool share)
{
        struct ffa_memory_region *m;
        struct ffa_composite_memory_region *composite;
        int ret, status = 0;
        unsigned int mem_attrs;
        char *ptr;
        ffa_id_t source = sender;
	uint32_t flags = share ? FFA_FLAG_SHARE_MEMORY : FFA_FLAG_LEND_MEMORY;
	uint32_t total_length, recv_length= 0;

        memory_retrieve(&mailbox, &m, handle, source, partition_id, flags, &recv_length, &total_length);

	while (total_length != recv_length) {
		tsp_args_t ffa_return;
		uint32_t frag_length;
		ffa_return = ffa_mem_frag_rx((uint32_t) handle, recv_length);

		if (ffa_return._regs[0] == FFA_ERROR)
		{
			WARN("TSP: failed to resume mem with handle %llx\n", handle);
			return -4;
		}
		frag_length = ffa_return._regs[3];

		memcpy(&mem_region_buffer[recv_length], mailbox.recv, frag_length);

		if (ffa_rx_release()) {
                	ERROR("Failed to release buffer!\n");
                	return false;
       		}

		recv_length += frag_length;

		assert(recv_length <= total_length);
	}

        composite = ffa_memory_region_get_composite(m, 0);
	if (composite == NULL){
		WARN("Failed to get composite descriptor!\n");
	}

        VERBOSE("Address: %p; page_count: %x %lx\n",
                composite->constituents[0].address,
                composite->constituents[0].page_count, PAGE_SIZE);

        /* This test is only concerned with RW permissions. */
        if (ffa_get_data_access_attr(
                        m->receivers[0].receiver_permissions.permissions) !=
                FFA_DATA_ACCESS_RW) {
                ERROR(" %x != %x!\n", ffa_get_data_access_attr(
                        m->receivers[0].receiver_permissions.permissions),
			FFA_DATA_ACCESS_RW);
                return -1;
        }

        mem_attrs = MT_RW_DATA | MT_EXECUTE_NEVER;

	/* Only expecting to be sent memory from Nwld so map accordinly. */
	mem_attrs |= MT_NS;

	for (int i = 0; i < composite->constituent_count; i++) {
		ret = mmap_add_dynamic_region(
				(uint64_t)composite->constituents[i].address,
				(uint64_t)composite->constituents[i].address,
				composite->constituents[i].page_count * PAGE_SIZE,
				mem_attrs);

		if (ret != 0) {
			ERROR("Failed [%d] mmap_add_dynamic_region %d (%llx) (%lx) (%x)!\n", i, ret,
				(uint64_t)composite->constituents[i].address,
				composite->constituents[i].page_count * PAGE_SIZE,
				mem_attrs);
			return -2;
		}

	        ptr = (char *) composite->constituents[i].address;

       		/* Read initial magic number from memory region for validation purposes. */
		if (!i) {
			status = *ptr + 1;
		}
       		/* Increment memory region for validation purposes. */
		++(*ptr);
	}

	for (int i = 0; i < composite->constituent_count; i++) {
		ret = mmap_remove_dynamic_region(
			(uint64_t)composite->constituents[i].address,
			composite->constituents[i].page_count * PAGE_SIZE);

		if (ret != 0) {
			ERROR("Failed [%d] mmap_add_dynamic_region!\n", i);
			return -3;
		}
	}
	if (!memory_relinquish((struct ffa_mem_relinquish *)mailbox.send,
				m->handle, partition_id)) {
		ERROR("Failed to relinquish memory region!\n");
		return -4;
	}
       return status;
}

/*******************************************************************************
 * This function handles partition messages. Exercised from the FFA Test Driver
 ******************************************************************************/
tsp_args_t *handle_partition_message(uint64_t arg0,
				     uint64_t arg1,
				     uint64_t arg2,
				     uint64_t arg3,
				     uint64_t arg4,
				     uint64_t arg5,
				     uint64_t arg6,
				     uint64_t arg7)
{
	uint16_t sender = FFA_SENDER(arg1);
	uint16_t receiver = FFA_RECEIVER(arg1);
	uint32_t status = -1;

	switch (arg3) {
		case FF_A_MEMORY_SHARE:
			INFO("TSP Tests: Memory Share Request--\n");
			status = test_memory_send(sender, arg4, true);
			break;
		case FF_A_MEMORY_LEND:
			INFO("TSP Tests: Memory Lend Request--\n");
			status = test_memory_send(sender, arg4, false);
			break;

		case FF_A_RELAY_MESSAGE:
			INFO("TSP Tests: Relaying message--\n");
			status = ffa_test_relay(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
			break;
		case FF_A_ECHO_MESSAGE:
			INFO("TSP Tests: echo message--\n");
			status = arg4;
			break;
		default:
			INFO("TSP Tests: Unknown request ID %d--\n", (int) arg3);
	}

	swap_src_dst(&sender, &receiver);
	return ffa_msg_send_direct_resp(sender, receiver, status, 0, 0, 0, 0);
}


/*******************************************************************************
 * This function implements the event loop for handling FF-A ABI invocations.
 ******************************************************************************/
tsp_args_t *tsp_event_loop(uint64_t arg0,
			   uint64_t arg1,
			   uint64_t arg2,
			   uint64_t arg3,
			   uint64_t arg4,
			   uint64_t arg5,
			   uint64_t arg6,
			   uint64_t arg7)
{
	uint64_t smc_fid = arg0;

	/* Panic if the SPMC did not forward an FF-A call */
	if(!is_ffa_fid(smc_fid))
		panic();

	switch (smc_fid) {
	case FFA_INTERRUPT:
		/*
		 * IRQs were enabled upon re-entry into the TSP. The interrupt
		 * must have been handled by now. Return to the SPMC indicating
		 * the same.
		 */
		return set_smc_args(FFA_MSG_WAIT, 0, 0, 0, 0, 0, 0, 0);

	case FFA_MSG_SEND_DIRECT_REQ_SMC64:
	case FFA_MSG_SEND_DIRECT_REQ_SMC32:

		/* Check if a framework message, handle accordingly */
		if ((arg2 & FFA_DIRECT_FRAMEWORK_MSG_MASK)) {
			return handle_framework_message(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
		} else {
			return handle_partition_message(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
		}
	default:
		return set_smc_args(FFA_MSG_SEND_DIRECT_RESP_SMC32, 1, 2, 3, 4, 0, 0, 0);
	}

	INFO("%s: Unsupported FF-A FID (0x%llu)\n", __func__, smc_fid);
	panic();
}
#endif /* SPMC_AT_EL3*/
