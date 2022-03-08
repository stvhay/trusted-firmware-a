/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * DRTM DMA protection.
 *
 * Authors:
 *	Lucian Paul-Trifu <lucian.paultrifu@gmail.com>
 *
 */
#include <stdint.h>
#include <string.h>

#include <common/debug.h>
#include <drivers/arm/smmu_v3.h>
#include <services/drtm_svc_plat.h>
#include <smccc_helpers.h>

#include "drtm_dma_prot.h"
#include "drtm_remediation.h"
#include "drtm_main.h"


/* Values for DRTM_PROTECT_MEMORY */
enum dma_prot_type {
	PROTECT_NONE            = -1,
	PROTECT_MEM_ALL         = 0,
	PROTECT_MEM_REGION      = 1,
};

struct dma_prot {
	enum dma_prot_type type;
};

/*
 *  ________________________  LAUNCH success        ________________________
 * |        Initial         | -------------------> |      Prot engaged      |
 * |````````````````````````|                      |````````````````````````|
 * |  request.type == NONE  |                      |  request.type != NONE  |
 * |                        | <------------------- |                        |
 * `________________________'        UNPROTECT_MEM `________________________'
 *
 * Transitions that are not shown correspond to ABI calls that do not change
 * state and result in an error being returned to the caller.
 */
static struct dma_prot active_prot = {
	.type = PROTECT_NONE,
};

/* Version-independent type. */
typedef struct drtm_dl_dma_prot_args_v1 struct_drtm_dl_dma_prot_args;


int drtm_dma_prot_init(void)
{
	bool must_init_fail = false;
	const uintptr_t *smmus;
	size_t num_smmus = 0;
	unsigned int num_smmus_total;

	/* Report presence of non-host platforms, for info only. */
	if (plat_has_non_host_platforms()) {
		WARN("DRTM: the platform includes trusted DMA-capable devices"
		     " (non-host platforms)\n");
	}

	/*
	 * DLME protection is uncertain on platforms with peripherals whose
	 * DMA is not managed by an SMMU.  DRTM doesn't work on such platforms.
	 */
	if (plat_has_unmanaged_dma_peripherals()) {
		ERROR("DRTM: this platform does not provide DMA protection\n");
		must_init_fail = true;
	}

	/*
	 * Check that the platform reported all SMMUs.
	 * It is acceptable if the platform doesn't have any SMMUs when it
	 * doesn't have any DMA-capable devices.
	 */
	num_smmus_total = plat_get_total_num_smmus();
	plat_enumerate_smmus((const uintptr_t (*)[])&smmus, &num_smmus);
	if (num_smmus != num_smmus_total) {
		ERROR("DRTM: could not discover all SMMUs\n");
		must_init_fail = true;
	}

	/* Check any SMMUs enumerated. */
	for (const uintptr_t *smmu = smmus; smmu < smmus + num_smmus; smmu++) {
		if (*smmu == 0) {
			WARN("DRTM: SMMU reported at unusual PA 0x0\n");
		}
	}

	return (int)must_init_fail;
}

uint64_t drtm_features_dma_prot(void *ctx)
{
	SMC_RET2(ctx, 1ULL,  /* DMA protection feature is supported */
		1u  /* DMA protection support:  Complete DMA protection. */
	);
}

/*
 * Checks that the DMA protection arguments are valid and that the given
 * protected regions would be covered by DMA protection.
 */
enum drtm_retc drtm_dma_prot_check_args(const struct_drtm_dl_dma_prot_args *a,
                                        int a_dma_prot_type,
                                        struct __protected_regions p)
{
	switch ((enum dma_prot_type)a_dma_prot_type) {
	case PROTECT_MEM_ALL:
		if (a->dma_prot_table_paddr || a->dma_prot_table_size) {
			ERROR("DRTM: invalid launch due to inconsistent"
			      " DMA protection arguments\n");
			return MEM_PROTECT_INVALID;
		}
		/*
		 * Full DMA protection ought to ensure that the DLME and NWd
		 * DCE regions are protected, no further checks required.
		 */
		return SUCCESS;

	default:
		ERROR("DRTM: invalid launch due to unsupported DMA protection type\n");
		return MEM_PROTECT_INVALID;
	}
}

enum drtm_retc drtm_dma_prot_engage(const struct_drtm_dl_dma_prot_args *a,
                                    int a_dma_prot_type)
{
	const uintptr_t *smmus;
	size_t num_smmus = 0;

	if (active_prot.type != PROTECT_NONE) {
		ERROR("DRTM: launch denied as previous DMA protection"
		      " is still engaged\n");
		return DENIED;
	}

	if (a_dma_prot_type == PROTECT_NONE) {
		return SUCCESS;
	/* Only PROTECT_MEM_ALL is supported currently. */
	} else if (a_dma_prot_type != PROTECT_MEM_ALL) {
		ERROR("%s(): unimplemented DMA protection type\n", __func__);
		panic();
	}

	/*
	 * Engage SMMUs in accordance with the request we have previously received.
	 * Only PROTECT_MEM_ALL is implemented currently.
	 */
	plat_enumerate_smmus((const uintptr_t (*)[])&smmus, &num_smmus);
	for (const uintptr_t *smmu = smmus; smmu < smmus+num_smmus; smmu++) {
		int rc;

		/*
		 * TODO: Invalidate SMMU's Stage-1 and Stage-2 TLB entries.  This ensures
		 * that any outstanding device transactions are completed, see Section
		 * 3.21.1, specification IHI_0070_C_a for an approximate reference.
		 */

		if ((rc = smmuv3_ns_set_abort_all(*smmu))) {
			ERROR("DRTM: SMMU at PA 0x%lx failed to engage DMA protection"
			      " rc=%d\n", *smmu, rc);
			return INTERNAL_ERROR;
		}
	}

	/*
	 * TODO: Restrict DMA from the GIC.
	 *
	 * Full DMA protection may be achieved as follows:
	 *
	 * With a GICv3:
	 * - Set GICR_CTLR.EnableLPIs to 0, for each GICR;
	 *   GICR_CTLR.RWP == 0 must be the case before finishing, for each GICR.
	 * - Set GITS_CTLR.Enabled to 0;
	 *   GITS_CTLR.Quiescent == 1 must be the case before finishing.
	 *
	 * In addition, with a GICv4:
	 * - Set GICR_VPENDBASER.Valid to 0, for each GICR;
	 *   GICR_CTLR.RWP == 0 must be the case before finishing, for each GICR.
	 *
	 * Alternatively, e.g. if some bit values cannot be changed at runtime,
	 * this procedure should return an error if the LPI Pending and
	 * Configuration tables overlap the regions being protected.
	 */

	active_prot.type = a_dma_prot_type;

	return SUCCESS;
}

/*
 * Undo what has previously been done in drtm_dma_prot_engage(), or enter
 * remediation if it is not possible.
 */
enum drtm_retc drtm_dma_prot_disengage(void)
{
	const uintptr_t *smmus;
	size_t num_smmus = 0;

	if (active_prot.type == PROTECT_NONE) {
		return SUCCESS;
	/* Only PROTECT_MEM_ALL is supported currently. */
	} else if (active_prot.type != PROTECT_MEM_ALL) {
		ERROR("%s(): unimplemented DMA protection type\n", __func__);
		panic();
	}

	/*
	 * For PROTECT_MEM_ALL, undo the SMMU configuration for "abort all" mode
	 * done during engage().
	 */
	/* Simply enter remediation for now. */
	(void)smmus;
	(void)num_smmus;
	drtm_enter_remediation(1, "cannot undo PROTECT_MEM_ALL SMMU configuration");

	/* TODO: Undo GIC DMA restrictions. */

	active_prot.type = PROTECT_NONE;

	return SUCCESS;
}

uint64_t drtm_unprotect_mem(void *ctx)
{
	enum drtm_retc ret;

	switch (active_prot.type) {
	case PROTECT_NONE:
		ERROR("DRTM: invalid UNPROTECT_MEM, no DMA protection has"
		      " previously been engaged\n");
		ret = DENIED;
		break;

	case PROTECT_MEM_ALL:
		/*
		 * UNPROTECT_MEM is a no-op for PROTECT_MEM_ALL:  DRTM must not touch
		 * the NS SMMU as it is expected that the DLME has configured it.
		 */
		active_prot.type = PROTECT_NONE;

		ret = SUCCESS;
		break;

	default:
		ret = drtm_dma_prot_disengage();
		break;
	}

	SMC_RET1(ctx, ret);
}

void drtm_dma_prot_serialise_table(char *dst, size_t *size_out)
{
	if (active_prot.type == PROTECT_NONE) {
		if (size_out) {
			*size_out = 0;
		}
		return;
	} else if (active_prot.type != PROTECT_MEM_ALL) {
		ERROR("%s(): unimplemented DMA protection type\n", __func__);
		panic();
	}

	struct __packed descr_table_1 {
		struct_drtm_mem_region_descr_table header;
		struct_drtm_mem_region_descr regions[1];
	} prot_table = {
		.header = {
			.version = 1,
			.num_regions = sizeof(((struct descr_table_1 *)NULL)->regions) /
						   sizeof(((struct descr_table_1 *)NULL)->regions[0])
		},
			#define PAGES_AND_TYPE(pages, type) \
			.pages_and_type = DRTM_MEM_REGION_PAGES_AND_TYPE(pages, type)
		.regions = {
			{.paddr = 0, PAGES_AND_TYPE(UINT64_MAX, 0x3)},
		}
	};

	if (dst) {
		(void)memcpy(dst, &prot_table, sizeof(prot_table));
	}
	if (size_out) {
		*size_out = sizeof(prot_table);
	}
}
