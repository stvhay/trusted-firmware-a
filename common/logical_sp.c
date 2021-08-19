/*
 * Copyright (c) 2013-2019, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <common/uuid.h>
#include <common/debug.h>
#include <common/runtime_svc.h>

#include <services/logical_sp.h>

/*******************************************************************************
 * The 'logical_partition' array holds the logical partition descriptors exported by
 * SPs by placing them in the 'logical_partition' linker section.
 ******************************************************************************/



/*******************************************************************************
 * Simple routine to sanity check a logical partition descriptor before using it
 ******************************************************************************/
static int32_t validate_logical_partition_struct(const el3_lp_desc_t *desc)
{
	if (desc == NULL)
		return -EINVAL;

	/* Ensue we have setup and direct messaging callback registered */
	if ((desc->init == NULL) || (desc->direct_req == NULL))
		return -EINVAL;

	return 0;
}

/*******************************************************************************
 * This function validates any logical partition descriptors.
 * Initalistaion of said partitions will be taken care of during SPMC boot.
 ******************************************************************************/
void __init el3_sp_desc_init(void)
{
	int rc = 0;
	uint8_t index, inner_idx;
	el3_lp_desc_t *logical_partition;

	/* Assert the number of descriptors detected are less than maximum indices */
	assert(EL3_LP_DESCS_END >= EL3_LP_DESCS_START);

	assert(EL3_LP_DESCS_NUM <= MAX_EL3_LP_DESCS_COUNT);

	/* If no logical partitions are implemented then simply bail out */
	if (EL3_LP_DESCS_NUM == 0U)
		return;

	/* Initialise internal variables to invalid state */
	/* TODO: Do we want to reused indexing mechanism here or just loop as arrary is small? */
	// (void)memset(logical_partition_indices, -1, sizeof(logical_partition_indices));

	logical_partition = (el3_lp_desc_t *) EL3_LP_DESCS_START;
	for (index = 0U; index < EL3_LP_DESCS_NUM; index++) {
		el3_lp_desc_t *lp_descriptor = &logical_partition[index];

		/*
		 * Validate our logical partition descriptors.
		 */
		rc = validate_logical_partition_struct(lp_descriptor);
		if (rc != 0) {
			ERROR("Invalid logical partition descriptor %p\n",
				(void *) lp_descriptor);
			panic(); // TODO: Should we just continue to load without that partition?
		}

		/* Check we have a UUID Specified. */
		if (lp_descriptor->uuid == NULL) {
			ERROR("Invalid UUID Specified\n");
		}

		/* Ensure that all partition IDs are unique. */
		for (inner_idx = index  + 1; inner_idx < EL3_LP_DESCS_NUM; inner_idx++) {
			el3_lp_desc_t *lp_descriptor_other = &logical_partition[index];
			if (lp_descriptor->sp_id == lp_descriptor_other->sp_id) {
				ERROR("Duplicate Partition ID Detected 0x%x\n", lp_descriptor->sp_id);
				panic();
			}
		}
	}
}
