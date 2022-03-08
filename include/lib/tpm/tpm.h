/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 */
#ifndef TPM_H
#define TPM_H

#include <lib/utils_def.h>

/*
 * TPM_ALG_ID constants.
 * Ref. Table 9 - Definition of (UINT16) TPM_ALG_ID Constants
 * Trusted Platform Module Library. Part 2: Structures,
 * Family "2.0", Level 00 Revision 01.38, September 29 2016.
 */
enum tpm_hash_alg {
	TPM_ALG_NONE   = 0x0,
	TPM_ALG_SHA256 = 0x000B,
	TPM_ALG_SHA384 = 0x000C,
	TPM_ALG_SHA512 = 0x000D,
};
static inline bool tpm_alg_is_valid(enum tpm_hash_alg alg)
{
	switch (alg) {
	case TPM_ALG_SHA256:
	case TPM_ALG_SHA384:
	case TPM_ALG_SHA512:
		return true;

	default:
		return false;
	}
}

enum tpm_hash_alg_dsize {
	TPM_ALG_SHA256_DSIZE = 32,
	TPM_ALG_SHA384_DSIZE = 48,
	TPM_ALG_SHA512_DSIZE = 64,

	TPM_ALG_MAX_DSIZE = TPM_ALG_SHA512_DSIZE
};
static inline size_t tpm_alg_dsize(enum tpm_hash_alg alg)
{
	switch (alg) {
	case TPM_ALG_SHA256:
		return TPM_ALG_SHA256_DSIZE;

	case TPM_ALG_SHA384:
		return TPM_ALG_SHA384_DSIZE;

	case TPM_ALG_SHA512:
		return TPM_ALG_SHA512_DSIZE;

	default:
		return 0;
	}
}

enum tpm_pcr_idx {
	/*
	 * SRTM, BIOS, Host Platform Extensions, Embedded
	 * Option ROMs and PI Drivers
	 */
	TPM_PCR_0 = 0,
	/* Host Platform Configuration */
	TPM_PCR_1,
	/* UEFI driver and application Code */
	TPM_PCR_2,
	/* UEFI driver and application Configuration and Data */
	TPM_PCR_3,
	/* UEFI Boot Manager Code (usually the MBR) and Boot Attempts */
	TPM_PCR_4,
	/*
	 * Boot Manager Code Configuration and Data (for use
	 * by the Boot Manager Code) and GPT/Partition Table
	 */
	TPM_PCR_5,
	/* Host Platform Manufacturer Specific */
	TPM_PCR_6,
	/* Secure Boot Policy */
	TPM_PCR_7,
	/* 8-15: Defined for use by the Static OS */
	TPM_PCR_8,
	/* Debug */
	TPM_PCR_16 = 16,

	/* DRTM (1) */
	TPM_PCR_17 = 17,
	/* DRTM (2) */
	TPM_PCR_18 = 18,
};
static bool inline tpm_pcr_idx_is_valid(enum tpm_pcr_idx pcr_idx)
{
	switch (pcr_idx) {
	case TPM_PCR_0:
	case TPM_PCR_1:
	case TPM_PCR_2:
	case TPM_PCR_3:
	case TPM_PCR_4:
	case TPM_PCR_5:
	case TPM_PCR_6:
	case TPM_PCR_7:
	case TPM_PCR_8:
	case TPM_PCR_16:
	case TPM_PCR_17:
	case TPM_PCR_18:
		return true;

	default:
		return false;
	}
}

#endif /* TPM_H */
