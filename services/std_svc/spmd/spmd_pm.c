/*
 * Copyright (c) 2020, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch_helpers.h>
#include <assert.h>
#include <common/bl_common.h>
#include <common/debug.h>
#include <lib/el3_runtime/context_mgmt.h>
#include <plat/common/platform.h>
#include <smccc_helpers.h>
#include "spmd_private.h"

/*******************************************************************************
 * This cpu has been turned on. Enter SPMC to initialise S-EL1 or S-EL2 before
 * initialising any SPs that they manage. Entry into SPMC is done after
 * initialising minimal architectural state that guarantees safe execution.
 ******************************************************************************/
static void spmd_cpu_on_finish_handler(u_register_t unused)
{
	int32_t rc = 0;
	uint32_t linear_id = plat_my_core_pos();
	spmd_spm_core_context_t *ctx = &spm_core_context[linear_id];

	assert(ctx->state != AFF_STATE_ON);

	/* Enter SPMC on this cpu if an entry point has been setup */
	if (ctx->state == AFF_STATE_OFF)
		return;

	rc = spmd_spm_core_sync_entry(ctx);
	if (rc) {
		ERROR("SPMC initialisation failed (%d) on cpu%d\n", rc,
		      linear_id);
		panic();
	}

	ctx->state = AFF_STATE_ON;
}

/*******************************************************************************
 * This function handles all PSCI SMCs that originate from the Secure world.
 ******************************************************************************/
uint64_t spmd_psci_smc_handler(uint32_t smc_fid, uint64_t x1, uint64_t x2,
			       uint64_t x3, uint64_t x4, void *cookie,
			       void *handle, uint64_t flags)
{
	int32_t ret;
	entry_point_info_t ep_info;
	int32_t target_ctx;
	spmd_spm_core_context_t *ctx;

	/* Assert that the call originated from the Secure world */
	assert(!is_caller_non_secure(flags));

	switch (smc_fid) {
	case PSCI_VERSION:
		return (u_register_t) psci_version();

	case PSCI_CPU_ON_AARCH32:
	case PSCI_CPU_ON_AARCH64:
		/* Determine if the target SPMC context exists of not */
		ret = psci_validate_mpidr(x1);
		if (ret != PSCI_E_SUCCESS) {
			WARN("%s: %d\n", __func__, ret);
			return PSCI_E_INVALID_PARAMS;
		}

		/* Obtain linear index of target ctx */
		target_ctx = plat_core_pos_by_mpidr(x1);

		/* Obtain reference to SPMC context information on target ctx */
		ctx = &spm_core_context[target_ctx];

		/* Check if target context is already on */
		if (ctx->state == AFF_STATE_ON) {
			WARN("%s: %d\n", __func__, PSCI_E_ALREADY_ON);
			return PSCI_E_ALREADY_ON;
		}

		/* Check if target context is being turned on */
		if (ctx->state == AFF_STATE_ON_PENDING) {
			WARN("%s: %d\n", __func__, ret);
			return PSCI_E_ON_PENDING;
		}

		/*
		 * Initialise an entry_point_info structure for target
		 * context.
		 */
		entry_point_info_t *spmc_ep_info = spmd_spmc_ep_info_get();
		ep_info = *spmc_ep_info;
		ep_info.pc = x2; /* TODO: check PSCI ABI */
		zeromem(&ep_info.args, sizeof(ep_info.args));
		ep_info.args.arg0 = x3;

		spin_lock(&ctx->lock);

		/* Check if a parallel call got ahead of us */
		ret = PSCI_E_ON_PENDING;
		if (ctx->state == AFF_STATE_OFF) {
			/*
			 * Setup a CPU context for entry into SPMC on the target
			 * ctx
			 */
			cm_setup_context(&ctx->cpu_ctx, &ep_info);
			ctx->state = AFF_STATE_ON_PENDING;
			ret = PSCI_E_SUCCESS;
		}

		spin_unlock(&ctx->lock);
		return ret;

	default:
		WARN("SPMD: Unsupported PSCI call: 0x%08x\n", smc_fid);
		return SMC_UNK;
	}
}

/* TODO: off case */

/*******************************************************************************
 * Structure populated by the SPM Dispatcher to perform any bookkeeping before
 * PSCI executes a power mgmt. operation.
 ******************************************************************************/
const spd_pm_ops_t spmd_pm = {
	.psci_sec_smc_handler = spmd_psci_smc_handler,
	.svc_on_finish = spmd_cpu_on_finish_handler,
};
