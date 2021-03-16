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
 * Set state of a Secure Partition context.
 ******************************************************************************/
void sp_state_set(sp_context_t *sp_ptr, sp_state_t state)
{
	spin_lock(&(sp_ptr->state_lock));
	sp_ptr->state = state;
	spin_unlock(&(sp_ptr->state_lock));
}

/*******************************************************************************
 * Wait until the state of a Secure Partition is the specified one and change it
 * to the desired state.
 ******************************************************************************/
void sp_state_wait_switch(sp_context_t *sp_ptr, sp_state_t from, sp_state_t to)
{
	int success = 0;

	while (success == 0) {
		spin_lock(&(sp_ptr->state_lock));

		if (sp_ptr->state == from) {
			sp_ptr->state = to;

			success = 1;
		}

		spin_unlock(&(sp_ptr->state_lock));
	}
}

/*******************************************************************************
 * Check if the state of a Secure Partition is the specified one and, if so,
 * change it to the desired state. Returns 0 on success, -1 on error.
 ******************************************************************************/
int sp_state_try_switch(sp_context_t *sp_ptr, sp_state_t from, sp_state_t to)
{
	int ret = -1;

	spin_lock(&(sp_ptr->state_lock));

	if (sp_ptr->state == from) {
		sp_ptr->state = to;

		ret = 0;
	}

	spin_unlock(&(sp_ptr->state_lock));

	return ret;
}

/*******************************************************************************
 * This function takes an SP context pointer and performs a synchronous entry
 * into it.
 ******************************************************************************/
uint64_t spm_sp_synchronous_entry(sp_context_t *ctx)
{
	uint64_t rc;

	assert(ctx != NULL);

	/* Assign the context of the SP to this CPU */
	cm_set_context(&(ctx->cpu_ctx), SECURE);

	/* Restore the context assigned above */
	cm_el1_sysregs_context_restore(SECURE);
	cm_set_next_eret_context(SECURE);

	/* Invalidate TLBs at EL1. */
	tlbivmalle1();
	dsbish();

	/* Enter Secure Partition */
	rc = spm_secure_partition_enter(&ctx->c_rt_ctx);

	/* Save secure state */
	cm_el1_sysregs_context_save(SECURE);

	return rc;
}

/*******************************************************************************
 * This function returns to the place where spm_sp_synchronous_entry() was
 * called originally.
 ******************************************************************************/
__dead2 void spm_sp_synchronous_exit(sp_context_t *ctx, uint64_t rc)
{
	/*
	 * The SPM must have initiated the original request through a
	 * synchronous entry into the secure partition. Jump back to the
	 * original C runtime context with the value of rc in x0;
	 */
	spm_secure_partition_exit(ctx->c_rt_ctx, rc);

	panic();
}
