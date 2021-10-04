/*
 * Copyright (c) 2017-2021, ARM Limited and Contributors. All rights reserved.
 * Copyright (c) 2021, NVIDIA Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <string.h>

#include <arch.h>
#include <arch_helpers.h>
#include <context.h>
#include <common/debug.h>
#include <lib/el3_runtime/context_mgmt.h>
#include <lib/xlat_tables/xlat_tables_v2.h>
#include <lib/utils.h>
#include <platform_def.h>
#include <plat/common/common_def.h>
#include <plat/common/platform.h>
#include <services/ffa_svc.h>

#include "spm_common.h"
#include "spm_shim_private.h"
#include "spmc.h"

/*
 * We need to choose one execution context from all those available for a S-EL0
 * SP. This execution context will be used subsequently irrespective of which
 * physical CPU the SP runs on. The index of this context is chosen during setup
 * on the primary cpu and stored in this variable for subsequent usage.
 */
static unsigned int sel0_sp_ec_index = 0;

unsigned int get_ec_index(sp_desc_t *sp)
{
	return (sp->runtime_el == EL0) ? sel0_sp_ec_index : plat_my_core_pos();
}

/* Setup context of a EL0 MM Secure Partition */
void spmc_el0_sp_setup(sp_desc_t *sp, entry_point_info_t *ep_info)
{
	cpu_context_t *ctx;

	/*
	 * Choose the linear of the primary core as the index of the S-EL0 SP
	 * execution context.
	 */
	sel0_sp_ec_index = plat_my_core_pos();
	ctx = &sp->ec[sel0_sp_ec_index].cpu_ctx;

	init_xlat_tables_ctx(sp->xlat_ctx_handle);

	/*
	 * MMU-related registers
	 * ---------------------
	 */
	xlat_ctx_t *xlat_ctx = sp->xlat_ctx_handle;

	uint64_t mmu_cfg_params[MMU_CFG_PARAM_MAX];

	setup_mmu_cfg((uint64_t *)&mmu_cfg_params, 0, xlat_ctx->base_table,
		      xlat_ctx->pa_max_address, xlat_ctx->va_max_address,
		      EL1_EL0_REGIME);

	write_ctx_reg(get_el1_sysregs_ctx(ctx), CTX_MAIR_EL1,
		      mmu_cfg_params[MMU_CFG_MAIR]);

	write_ctx_reg(get_el1_sysregs_ctx(ctx), CTX_TCR_EL1,
		      mmu_cfg_params[MMU_CFG_TCR]);

	write_ctx_reg(get_el1_sysregs_ctx(ctx), CTX_TTBR0_EL1,
		      mmu_cfg_params[MMU_CFG_TTBR0]);

	/* Setup SCTLR_EL1 */
	u_register_t sctlr_el1 = read_ctx_reg(get_el1_sysregs_ctx(ctx), CTX_SCTLR_EL1);

	sctlr_el1 |=
		/*SCTLR_EL1_RES1 |*/
		/* Don't trap DC CVAU, DC CIVAC, DC CVAC, DC CVAP, or IC IVAU */
		SCTLR_UCI_BIT							|
		/* RW regions at xlat regime EL1&0 are forced to be XN. */
		SCTLR_WXN_BIT							|
		/* Don't trap to EL1 execution of WFI or WFE at EL0. */
		SCTLR_NTWI_BIT | SCTLR_NTWE_BIT					|
		/* Don't trap to EL1 accesses to CTR_EL0 from EL0. */
		SCTLR_UCT_BIT							|
		/* Don't trap to EL1 execution of DZ ZVA at EL0. */
		SCTLR_DZE_BIT							|
		/* Enable SP Alignment check for EL0 */
		SCTLR_SA0_BIT							|
		/* Don't change PSTATE.PAN on taking an exception to EL1 */
		SCTLR_SPAN_BIT							|
		/* Allow cacheable data and instr. accesses to normal memory. */
		SCTLR_C_BIT | SCTLR_I_BIT					|
		/* Enable MMU. */
		SCTLR_M_BIT
	;

	sctlr_el1 &= ~(
		/* Explicit data accesses at EL0 are little-endian. */
		SCTLR_E0E_BIT							|
		/*
		 * Alignment fault checking disabled when at EL1 and EL0 as
		 * the UEFI spec permits unaligned accesses.
		 */
		SCTLR_A_BIT							|
		/* Accesses to DAIF from EL0 are trapped to EL1. */
		SCTLR_UMA_BIT
	);

	write_ctx_reg(get_el1_sysregs_ctx(ctx), CTX_SCTLR_EL1, sctlr_el1);

	/*
	 * Setup other system registers
	 * ----------------------------
	 */

	/* Shim Exception Vector Base Address */
	write_ctx_reg(get_el1_sysregs_ctx(ctx), CTX_VBAR_EL1,
			SPM_SHIM_EXCEPTIONS_PTR);

	write_ctx_reg(get_el1_sysregs_ctx(ctx), CTX_CNTKCTL_EL1,
		      EL0PTEN_BIT | EL0VTEN_BIT | EL0PCTEN_BIT | EL0VCTEN_BIT);

	/*
	 * FPEN: Allow the Secure Partition to access FP/SIMD registers.
	 * Note that SPM will not do any saving/restoring of these registers on
	 * behalf of the SP. This falls under the SP's responsibility.
	 * TTA: Enable access to trace registers.
	 * ZEN (v8.2): Trap SVE instructions and access to SVE registers.
	 */
	write_ctx_reg(get_el1_sysregs_ctx(ctx), CTX_CPACR_EL1,
			CPACR_EL1_FPEN(CPACR_EL1_FP_TRAP_NONE));

	sp->xlat_ctx_handle->xlat_regime =
		EL1_EL0_REGIME;

	/* This region contains the exception vectors used at S-EL1. */
	mmap_region_t sel1_exception_vectors =
		MAP_REGION_FLAT(SPM_SHIM_EXCEPTIONS_START,
				SPM_SHIM_EXCEPTIONS_SIZE,
				MT_CODE | MT_SECURE | MT_PRIVILEGED);
	mmap_add_region_ctx(sp->xlat_ctx_handle,
			    &sel1_exception_vectors);

	/*
	 * Save the stack base in SP_EL0 so that there is a C runtime upon the
	 * first ERET into the StMM SP.
	 */
	write_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_SP_EL0,
		      sp->sp_stack_base + sp->sp_stack_size);

}

/* SEL1 partition specific initialisation. */
void spmc_el1_sp_setup(sp_desc_t *sp, entry_point_info_t *ep_info)
{
	/* Sanity check input arguments */
	assert(NULL != sp);
	assert(NULL != ep_info);

	/*
	 * Lets just zero the general purpose registers for now. This would be a
	 * good time to let the platform enforce its own boot protocol.
	 */
	zeromem(&ep_info->args, sizeof(ep_info->args));

	/*
	 * Check whether setup is being performed for the primary or a secondary
	 * execution context. In the latter case, indicate to the SP that this
	 * is a warm boot.
	 * TODO: This check would need to be reworked if the same entry point is
	 * used for both primary and secondary initialisation.
	 */
	if (sp->secondary_ep) {
		/*
		 * Sanity check that the secondary entry point is still what was
		 * originally set.
		 */
		assert (sp->secondary_ep == ep_info->pc);

		write_ctx_reg(get_gpregs_ctx(&sp->ec[get_ec_index(sp)].cpu_ctx),
			      CTX_GPREG_X0,
			      FFA_WB_TYPE_S2RAM);
	}
}

/* Common initialisation for all SPs. */
void spmc_sp_common_setup(sp_desc_t *sp, entry_point_info_t *ep_info)
{
	cpu_context_t *cpu_ctx;

	/* Assign FFA Partition ID if not already assigned */
	if (sp->sp_id == INV_SP_ID)
		sp->sp_id = FFA_SP_ID_BASE + ACTIVE_SP_DESC_INDEX;

	/*
	 * The initialisation of the SPSR in the ep_info should ideally be done
	 * in the EL specific initialisation routines above. However,
	 * cm_context_setup() needs this information to initialise system
	 * registers correctly. So, lets do this here.
	 */
	if (sp->runtime_el == EL0)
		/* Setup Secure Partition SPSR for S-EL0 SP*/
		ep_info->spsr = SPSR_64(MODE_EL0, MODE_SP_EL0,
					DISABLE_ALL_EXCEPTIONS);
	else
		/* Setup Secure Partition SPSR for S-EL1 SP */
		ep_info->spsr =	SPSR_64(MODE_EL1, MODE_SP_ELX,
					DISABLE_ALL_EXCEPTIONS);

	/*
	 * Initialise the SP context based upon the entrypoint information
	 * collected so far. We are assuming that the index of the execution
	 * context used for both S-EL0 and S-EL1 SPs is the linear index of the
	 * primary cpu. This index was saved in spmc_el0_sp_setup() as well.
	 */
	cpu_ctx = &sp->ec[plat_my_core_pos()].cpu_ctx;
	cm_setup_context(cpu_ctx, ep_info);
}
