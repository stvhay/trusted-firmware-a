/*
 * Copyright (c) 2021, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SPMC_SVC_H
#define SPMC_SVC_H

#ifndef __ASSEMBLER__
#include <services/ffa_svc.h>
#include <stdint.h>

#include <lib/utils_def.h>

/*
 * IDs defined for accessing services implemented by the Secure Partition
 * Manager from the Secure Partition(s). These services enable a partition to
 * handle delegated events and request privileged operations from the manager.
 * They occupy the range 0x60-0x7f.
 */
#define SP_EVENT_COMPLETE_AARCH64		U(0xC4000061)
#define SP_MEMORY_ATTRIBUTES_GET_AARCH64	U(0xC4000064)
#define SP_MEMORY_ATTRIBUTES_SET_AARCH64	U(0xC4000065)

int spmc_setup(void);
void *spmc_get_config_addr(void);

void spmc_set_config_addr(uintptr_t soc_fw_config);

uint64_t spmc_smc_handler(uint32_t smc_fid,
			  uint64_t x1,
			  uint64_t x2,
			  uint64_t x3,
			  uint64_t x4,
			  void *cookie,
			  void *handle,
			  uint64_t flags);

#if SPMC_AT_EL3
inline bool is_spmc_at_el3(void)
{
	return true;
}
#else
inline bool is_spmc_at_el3(void)
{
	return false;
}
#endif /* SPMC_AT_EL3 */

#endif /* __ASSEMBLER__ */

#endif /* SPMC_SVC_H */
