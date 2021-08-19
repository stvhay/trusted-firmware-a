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
#include <platform_def.h>
#include <plat/common/common_def.h>
#include <plat/common/platform.h>

#include "spm_common.h"
#include "spm_partition.h"
#include "spm_shim_private.h"

/* TODO: What is/is not common to any EL0 partition? */
/* Setup context of a EL0 MM Secure Partition */
void spm_el0_sp_setup(sp_context_t *sp_ctx)
{
	cpu_context_t *ctx = &(sp_ctx->cpu_ctx);

	init_xlat_tables_ctx(sp_ctx->xlat_ctx_handle);

	/*
	 * MMU-related registers
	 * ---------------------
	 */
	xlat_ctx_t *xlat_ctx = sp_ctx->xlat_ctx_handle;

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

}


/* TODO: Split out common functionality to boot an SP. */
void spm_sp_common_setup(sp_context_t *sp_ctx) {

}

/* TODO: SEL1 partition specific initialisation. */
void spm_el1_sp_setup(sp_context_t *sp_ctx)
{
	/* TODO: Populate with EL1 specific initialisation. */

}