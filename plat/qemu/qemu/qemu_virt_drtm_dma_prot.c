/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


bool plat_has_non_host_platforms(void)
{
	/* Arm QEMU virt may have a GPU via cmd-line -virtio-gpu-device. */
	return true;
}

/* Note-LPT:
 * From looking at the docs and source code, I have not found clues whether this
 * platform has DMA-capable devices, and if it does, whether their memory
 * accesses necessarily go through IOMMU translation (SMMUv3).  Therefore,
 * for now assume there are no DMA-capable devices and therefore no (useful)
 * SMMUs.  This is a supported DRTM case whereby full DMA protection is still
 * advertised.
 */
bool plat_has_unmanaged_dma_peripherals(void)
{
	return false;
}
unsigned int plat_get_total_num_smmus(void)
{
	return 0;
}

void plat_enumerate_smmus(const uintptr_t (*smmus_out)[],
                          size_t *smmu_count_out)
{
	*(const uintptr_t **)smmus_out = NULL;
	*smmu_count_out = 0;
}
