/*
 * Copyright (c) 2017-2021, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch_helpers.h>
#include <assert.h>
#include <errno.h>

#include <bl31/bl31.h>
#include <bl31/ehf.h>
#include <common/debug.h>
#include <common/runtime_svc.h>
#include <lib/el3_runtime/context_mgmt.h>
#include <lib/smccc.h>
#include <lib/spinlock.h>
#include <lib/utils.h>
#include <lib/xlat_tables/xlat_tables_v2.h>
#include <plat/common/platform.h>
#include <services/spm_mm_svc.h>
#include <smccc_helpers.h>

#include "spm_common.h"
#include "spm_partition.h"
#include "spm_shim_private.h"

/*******************************************************************************
 * Secure Partition context information.
 ******************************************************************************/
static sp_context_t sp_ctx;

/*******************************************************************************
 * Jump to each Secure Partition for the first time.
 ******************************************************************************/
static int32_t spm_init(void)
{
	uint64_t rc;
	sp_context_t *ctx;

	INFO("Secure Partition init...\n");

	ctx = &sp_ctx;

	ctx->state = SP_STATE_RESET;

	rc = spm_sp_synchronous_entry(ctx);
	assert(rc == 0);

	ctx->state = SP_STATE_IDLE;

	INFO("Secure Partition initialized.\n");

	return !rc;
}

/*******************************************************************************
 * Initialize contexts of all Secure Partitions.
 ******************************************************************************/
int32_t spm_mm_setup(void)
{
	sp_context_t *ctx;
	cpu_context_t *cpu_ctx;

	/* Disable MMU at EL1 (initialized by BL2) */
	disable_mmu_icache_el1();

	/* Initialize context of the SP */
	INFO("Secure Partition context setup start...\n");

	ctx = &sp_ctx;
	cpu_ctx = &(ctx->cpu_ctx);

	/* Assign translation tables context. */
	ctx->xlat_ctx_handle = spm_get_sp_xlat_context();

	/* Pointer to the MP information from the platform port. */
	const spm_boot_info_t *sp_boot_info =
			plat_get_secure_partition_boot_info(NULL);

	/*
	 * Initialize CPU context
	 * ----------------------
	 */

	entry_point_info_t ep_info = {0};

	SET_PARAM_HEAD(&ep_info, PARAM_EP, VERSION_1, SECURE | EP_ST_ENABLE);

	/* Setup entrypoint and SPSR */
	ep_info.pc = sp_boot_info->sp_image_base;
	ep_info.spsr = SPSR_64(MODE_EL0, MODE_SP_EL0, DISABLE_ALL_EXCEPTIONS);

	/*
	 * X0: Virtual address of a buffer shared between EL3 and Secure EL0.
	 *     The buffer will be mapped in the Secure EL1 translation regime
	 *     with Normal IS WBWA attributes and RO data and Execute Never
	 *     instruction access permissions.
	 *
	 * X1: Size of the buffer in bytes
	 *
	 * X2: cookie value (Implementation Defined)
	 *
	 * X3: cookie value (Implementation Defined)
	 *
	 * X4 to X7 = 0
	 */
	ep_info.args.arg0 = sp_boot_info->sp_shared_buf_base;
	ep_info.args.arg1 = sp_boot_info->sp_shared_buf_size;
	ep_info.args.arg2 = PLAT_SPM_COOKIE_0;
	ep_info.args.arg3 = PLAT_SPM_COOKIE_1;

	cm_setup_context(cpu_ctx, &ep_info);

	write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_SP_EL0,
			sp_boot_info->sp_stack_base + sp_boot_info->sp_pcpu_stack_size);

#if ENABLE_ASSERTIONS

	/* Get max granularity supported by the platform. */
	unsigned int max_granule = xlat_arch_get_max_supported_granule_size();

	VERBOSE("Max translation granule size supported: %u KiB\n",
		max_granule / 1024U);

	unsigned int max_granule_mask = max_granule - 1U;

	/* Base must be aligned to the max granularity */
	assert((sp_boot_info->sp_ns_comm_buf_base & max_granule_mask) == 0);

	/* Size must be a multiple of the max granularity */
	assert((sp_boot_info->sp_ns_comm_buf_size & max_granule_mask) == 0);

#endif /* ENABLE_ASSERTIONS */

	/* This region contains the exception vectors used at S-EL1. */
	const mmap_region_t sel1_exception_vectors =
		MAP_REGION_FLAT(SPM_SHIM_EXCEPTIONS_START,
				SPM_SHIM_EXCEPTIONS_SIZE,
				MT_CODE | MT_SECURE | MT_PRIVILEGED);
	mmap_add_region_ctx(ctx->xlat_ctx_handle,
			    &sel1_exception_vectors);

	mmap_add_ctx(ctx->xlat_ctx_handle,
		     plat_get_secure_partition_mmap(NULL));

	spm_sp_setup(ctx);

	/*
	 * Prepare information in buffer shared between EL3 and S-EL0
	 * ----------------------------------------------------------
	 */

	void *shared_buf_ptr = (void *) sp_boot_info->sp_shared_buf_base;

	/* Copy the boot information into the shared buffer with the SP. */
	assert((uintptr_t)shared_buf_ptr + sizeof(spm_boot_info_t)
	       <= (sp_boot_info->sp_shared_buf_base + sp_boot_info->sp_shared_buf_size));

	assert(sp_boot_info->sp_shared_buf_base <=
				(UINTPTR_MAX - sp_boot_info->sp_shared_buf_size + 1));

	assert(sp_boot_info != NULL);

	memcpy((void *) shared_buf_ptr, (const void *) sp_boot_info,
	       sizeof(spm_boot_info_t));

	/* Pointer to the MP information from the platform port. */
	spm_mp_info_t *sp_mp_info =
		((spm_boot_info_t *) shared_buf_ptr)->mp_info;

	assert(sp_mp_info != NULL);

	/*
	 * Point the shared buffer MP information pointer to where the info will
	 * be populated, just after the boot info.
	 */
	((spm_boot_info_t *) shared_buf_ptr)->mp_info =
		(spm_mp_info_t *) ((uintptr_t)shared_buf_ptr
				+ sizeof(spm_boot_info_t));

	/*
	 * Update the shared buffer pointer to where the MP information for the
	 * payload will be populated
	 */
	shared_buf_ptr = ((spm_boot_info_t *) shared_buf_ptr)->mp_info;

	/*
	 * Copy the cpu information into the shared buffer area after the boot
	 * information.
	 */
	assert(sp_boot_info->num_cpus <= PLATFORM_CORE_COUNT);

	assert((uintptr_t)shared_buf_ptr
	       <= (sp_boot_info->sp_shared_buf_base + sp_boot_info->sp_shared_buf_size -
		       (sp_boot_info->num_cpus * sizeof(*sp_mp_info))));

	memcpy(shared_buf_ptr, (const void *) sp_mp_info,
		sp_boot_info->num_cpus * sizeof(*sp_mp_info));

	/*
	 * Calculate the linear indices of cores in boot information for the
	 * secure partition and flag the primary CPU
	 */
	sp_mp_info = (spm_mp_info_t *) shared_buf_ptr;

	for (unsigned int index = 0; index < sp_boot_info->num_cpus; index++) {
		u_register_t mpidr = sp_mp_info[index].mpidr;

		sp_mp_info[index].linear_id = plat_core_pos_by_mpidr(mpidr);
		if (plat_my_core_pos() == sp_mp_info[index].linear_id)
			sp_mp_info[index].flags |= MP_INFO_FLAG_PRIMARY_CPU;
	}

	/* Register init function for deferred init.  */
	bl31_register_bl32_init(&spm_init);

	INFO("Secure Partition setup done.\n");

	return 0;
}

/*******************************************************************************
 * Function to perform a call to a Secure Partition.
 ******************************************************************************/
uint64_t spm_mm_sp_call(uint32_t smc_fid, uint64_t x1, uint64_t x2, uint64_t x3)
{
	uint64_t rc;
	sp_context_t *sp_ptr = &sp_ctx;

	/* Wait until the Secure Partition is idle and set it to busy. */
	sp_state_wait_switch(sp_ptr, SP_STATE_IDLE, SP_STATE_BUSY);

	/* Set values for registers on SP entry */
	cpu_context_t *cpu_ctx = &(sp_ptr->cpu_ctx);

	write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_X0, smc_fid);
	write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_X1, x1);
	write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_X2, x2);
	write_ctx_reg(get_gpregs_ctx(cpu_ctx), CTX_GPREG_X3, x3);

	/* Jump to the Secure Partition. */
	rc = spm_sp_synchronous_entry(sp_ptr);

	/* Flag Secure Partition as idle. */
	assert(sp_ptr->state == SP_STATE_BUSY);
	sp_state_set(sp_ptr, SP_STATE_IDLE);

	return rc;
}

/*******************************************************************************
 * MM_COMMUNICATE handler
 ******************************************************************************/
static uint64_t mm_communicate(uint32_t smc_fid, uint64_t mm_cookie,
			       uint64_t comm_buffer_address,
			       uint64_t comm_size_address, void *handle)
{
	uint64_t rc;

	/* Cookie. Reserved for future use. It must be zero. */
	if (mm_cookie != 0U) {
		ERROR("MM_COMMUNICATE: cookie is not zero\n");
		SMC_RET1(handle, SPM_INVALID_PARAMETER);
	}

	if (comm_buffer_address == 0U) {
		ERROR("MM_COMMUNICATE: comm_buffer_address is zero\n");
		SMC_RET1(handle, SPM_INVALID_PARAMETER);
	}

	if (comm_size_address != 0U) {
		VERBOSE("MM_COMMUNICATE: comm_size_address is not 0 as recommended.\n");
	}

	/*
	 * The current secure partition design mandates
	 * - at any point, only a single core can be
	 *   executing in the secure partiton.
	 * - a core cannot be preempted by an interrupt
	 *   while executing in secure partition.
	 * Raise the running priority of the core to the
	 * interrupt level configured for secure partition
	 * so as to block any interrupt from preempting this
	 * core.
	 */
	ehf_activate_priority(PLAT_SP_PRI);

	/* Save the Normal world context */
	cm_el1_sysregs_context_save(NON_SECURE);

	rc = spm_mm_sp_call(smc_fid, comm_buffer_address, comm_size_address,
			    plat_my_core_pos());

	/* Restore non-secure state */
	cm_el1_sysregs_context_restore(NON_SECURE);
	cm_set_next_eret_context(NON_SECURE);

	/*
	 * Exited from secure partition. This core can take
	 * interrupts now.
	 */
	ehf_deactivate_priority(PLAT_SP_PRI);

	SMC_RET1(handle, rc);
}

/*******************************************************************************
 * Secure Partition Manager SMC handler.
 ******************************************************************************/
uint64_t spm_mm_smc_handler(uint32_t smc_fid,
			 uint64_t x1,
			 uint64_t x2,
			 uint64_t x3,
			 uint64_t x4,
			 void *cookie,
			 void *handle,
			 uint64_t flags)
{
	unsigned int ns;

	/* Determine which security state this SMC originated from */
	ns = is_caller_non_secure(flags);

	if (ns == SMC_FROM_SECURE) {

		/* Handle SMCs from Secure world. */

		assert(handle == cm_get_context(SECURE));

		/* Make next ERET jump to S-EL0 instead of S-EL1. */
		cm_set_elr_spsr_el3(SECURE, read_elr_el1(), read_spsr_el1());

		switch (smc_fid) {

		case SPM_MM_VERSION_AARCH32:
			SMC_RET1(handle, SPM_MM_VERSION_COMPILED);

		case MM_SP_EVENT_COMPLETE_AARCH64:
			spm_sp_synchronous_exit(&sp_ctx, x1);

		case MM_SP_MEMORY_ATTRIBUTES_GET_AARCH64:
			INFO("Received MM_SP_MEMORY_ATTRIBUTES_GET_AARCH64 SMC\n");

			if (sp_ctx.state != SP_STATE_RESET) {
				WARN("MM_SP_MEMORY_ATTRIBUTES_GET_AARCH64 is available at boot time only\n");
				SMC_RET1(handle, SPM_NOT_SUPPORTED);
			}
			SMC_RET1(handle,
				 spm_memory_attributes_get_smc_handler(
					 &sp_ctx, x1));

		case MM_SP_MEMORY_ATTRIBUTES_SET_AARCH64:
			INFO("Received MM_SP_MEMORY_ATTRIBUTES_SET_AARCH64 SMC\n");

			if (sp_ctx.state != SP_STATE_RESET) {
				WARN("MM_SP_MEMORY_ATTRIBUTES_SET_AARCH64 is available at boot time only\n");
				SMC_RET1(handle, SPM_NOT_SUPPORTED);
			}
			SMC_RET1(handle,
				 spm_memory_attributes_set_smc_handler(
					&sp_ctx, x1, x2, x3));
		default:
			break;
		}
	} else {

		/* Handle SMCs from Non-secure world. */

		assert(handle == cm_get_context(NON_SECURE));

		switch (smc_fid) {

		case MM_VERSION_AARCH32:
			SMC_RET1(handle, MM_VERSION_COMPILED);

		case MM_COMMUNICATE_AARCH32:
		case MM_COMMUNICATE_AARCH64:
			return mm_communicate(smc_fid, x1, x2, x3, handle);

		case MM_SP_MEMORY_ATTRIBUTES_GET_AARCH64:
		case MM_SP_MEMORY_ATTRIBUTES_SET_AARCH64:
			/* SMC interfaces reserved for secure callers. */
			SMC_RET1(handle, SPM_NOT_SUPPORTED);

		default:
			break;
		}
	}

	SMC_RET1(handle, SMC_UNK);
}
