/*
 * Copyright (c) 2021, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdbool.h>
#include <stddef.h>

#include <drivers/arm/smmu_v3.h>
#include <plat/arm/common/arm_config.h>
#include <platform_def.h>
#include <services/drtm_svc_plat.h>


bool plat_has_non_host_platforms(void)
{
	/* Note: FVP base platforms typically have GPU, as per --list-instances. */
	return true;
}

bool plat_has_unmanaged_dma_peripherals(void)
{
	/*
	 * Note-LPT: As far as I can tell, RevC's --list-instances does not show
	 * devices that are described as DMA-capable but not managed by an SMMU
	 * in the FVP documentation.
	 * However, the SMMU seems to have only been introduced in the RevC
	 * revision.
	 */
	return !(arm_config.flags & ARM_CONFIG_FVP_HAS_SMMUV3);
}

unsigned int plat_get_total_num_smmus(void)
{
	if ((arm_config.flags & ARM_CONFIG_FVP_HAS_SMMUV3)) {
		return 1;
	} else {
		return 0;
	}
}

static const uintptr_t smmus[] = {
	PLAT_FVP_SMMUV3_BASE,
};

void plat_enumerate_smmus(const uintptr_t (*smmus_out)[],
                          size_t *smmu_count_out)
{
	if ((arm_config.flags & ARM_CONFIG_FVP_HAS_SMMUV3)) {
		*(const uintptr_t **)smmus_out = smmus;
		*smmu_count_out = sizeof(smmus) / sizeof(uintptr_t);
	} else {
		*(const uintptr_t **)smmus_out = NULL;
		*smmu_count_out = 0;
	}
}
