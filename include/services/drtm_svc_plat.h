/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 * DRTM service's dependencies on the platform.
 *
 */
#ifndef ARM_DRTM_SVC_PLAT_H
#define ARM_DRTM_SVC_PLAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(DRTM_SHA_ALG)
#error "The DRTM service requires definition of the DRTM_SHA_ALG macro"
#else

#if DRTM_SHA_ALG == 256
#define DRTM_SHA_ALG_DSIZE  32
#elif DRTM_SHA_ALG == 384
#define DRTM_SHA_ALG_DSIZE  48
#elif DRTM_SHA_ALG == 512
#define DRTM_SHA_ALG_DSIZE  64
#else
#warning "Unrecognised DRTM_SHA_ALG"
#define DRTM_SHA_ALG_DSIZE  64
#endif

#endif


/***
 * DRTM's dependency on platform DMA protection.
 */

/* Sanity checks. */
bool plat_has_non_host_platforms(void);
bool plat_has_unmanaged_dma_peripherals(void);
unsigned int plat_get_total_num_smmus(void);

/* Dependency on Arm-compliant SMMUs. */
void plat_enumerate_smmus(const uintptr_t (*smmus_out)[],
                          size_t *smmu_count_out);

struct drtm_mem_region_descr_table_v1;
typedef struct drtm_mem_region_descr_table_v1 struct_drtm_mem_region_descr_table;

/* Dependencies on platform-specific region-based DMA protection. */
struct drtm_dma_protector_ops {
	int (*protect_regions)(void *data,
	                       const struct_drtm_mem_region_descr_table *regions);
};
struct drtm_dma_protector {
	void *data;
	struct drtm_dma_protector_ops *ops;
};
struct drtm_dma_protector plat_get_dma_protector(void);


/***
 * DRTM's platform-specific DRTM TCB hashes.
 */

struct plat_drtm_tcb_hash {
	union {
#define _HASH_ID_TYPE uint32_t
		_HASH_ID_TYPE uint32;
		unsigned char uchars[sizeof(_HASH_ID_TYPE)];
#undef  _HASH_ID_TYPE
	} hash_id;
        size_t hash_bytes;
        unsigned char hash_val[DRTM_SHA_ALG_DSIZE];
};
#define PLAT_DRTM_TCB_HASH_VAL_AND_SIZE(...) \
	.hash_bytes = sizeof((unsigned char[]){ __VA_ARGS__ }), .hash_val = { __VA_ARGS__ }

void plat_enumerate_drtm_tcb_hashes(const struct plat_drtm_tcb_hash **hashes_out,
                                    size_t *hashes_count_out);

#endif /* ARM_DRTM_SVC_PLAT_H */
