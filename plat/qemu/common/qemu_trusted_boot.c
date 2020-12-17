/*
 * Copyright (c) 2017-2019, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <plat/common/platform.h>
#include "assert.h"

uint32_t *qemu_trial_rot_nv = (uint32_t *)QEMU_ROT_NV_CTR_ADDR;
uint32_t *qemu_trial_flag = (uint32_t *)QEMU_TRIAL_FLAG_ADDR;

extern char qemu_rotpk_hash[], qemu_rotpk_hash_end[];

int plat_get_rotpk_info(void *cookie, void **key_ptr, unsigned int *key_len,
			unsigned int *flags)
{
	*key_ptr = qemu_rotpk_hash;
	*key_len = qemu_rotpk_hash_end - qemu_rotpk_hash;
	*flags = ROTPK_IS_HASH;

	return 0;
}

int plat_get_nv_ctr(void *cookie, unsigned int *nv_ctr)
{
	if (*qemu_trial_flag)
		*nv_ctr = *qemu_trial_rot_nv;
	else
		*nv_ctr = *(uint32_t *)SWD_NV_COUNTER;

	return 0;
}

int plat_set_nv_ctr(void *cookie, unsigned int nv_ctr)
{
	/*
	 *  In this prototype the NV rollback counter is set in the FWU Implementation (edk2-platforms: FWUpdate.c).
	 *  The real NV counter resides in flash at offset SWD_NV_COUNTER.
	 *  This call sets the temporary rollback counter used during a trial run.
	 *  The value set in QEMU_ROT_NV_CTR_ADDR is visible in the edk2-platform: FWUpdate.c.
	 *  During the complete_trial_run handler, the value in QEMU_ROT_NV_CTR_ADDR is written to flash at offset SWD_NV_COUNTER.
	 */
	if(*qemu_trial_flag) {
		NOTICE("qemu tbbr set trial nv_ctr %d\n", nv_ctr);
		*qemu_trial_rot_nv = nv_ctr;
	}

	return 0;
}

int plat_get_mbedtls_heap(void **heap_addr, size_t *heap_size)
{
	return get_mbedtls_heap_helper(heap_addr, heap_size);
}

void plat_decrement_trial(void)
{
	(*qemu_trial_flag)--;
}

uint32_t plat_get_trial(void)
{
	/*
	 * XXX: Qemu seems to initalize the memory as 0 out of a cold boot.
	 * We rely on this fact for correct prototype operation.
	 * This is a temporary assumption, the platform port must ensure that
	 * the trial_run is set to zero when coming out of a cold reset.
	 */

	return *qemu_trial_flag;
}

