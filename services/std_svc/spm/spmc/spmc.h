/*
 * Copyright (c) 2021, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SPMC_H
#define SPMC_H

#include <stdint.h>

#include "spm_common.h"

/*
 * 0x1 is used for StandAloneMM Secure Parition ID.
 * Same has been followed in Optee.
 * https://github.com/OP-TEE/optee_os/blob/49dbb9ef65643c4322cf3f848910fa880d1c02f6/core/arch/arm/kernel/stmm_sp.c#L65-L67
 */
#define  STMM_SP_ID	(0x1)

typedef struct spmc_sp_context {
	/*
	 * Secure parition context
	 */
	sp_context_t sp_ctx;

	/*
	 * ID of the Secure Partition
	 */
	uint16_t sp_id;

} spmc_sp_context_t;

#endif /* SPMC_H */
