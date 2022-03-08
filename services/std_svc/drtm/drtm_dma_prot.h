/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier:    BSD-3-Clause
 *
 */
#ifndef DRTM_DMA_PROT_H
#define DRTM_DMA_PROT_H

#include <stdint.h>

#include <lib/utils.h>


struct __packed drtm_dl_dma_prot_args_v1 {
	uint64_t dma_prot_table_paddr;
	uint64_t dma_prot_table_size;
};
/* Opaque / encapsulated type. */
typedef struct drtm_dl_dma_prot_args_v1 drtm_dl_dma_prot_args_v1_t;

struct __protected_regions {
	struct p_mem_region dlme_region;
	struct p_mem_region dce_nwd_region;
};


struct __packed drtm_mem_region_descr_v1 {
	uint64_t paddr;
	uint64_t pages_and_type;
};
#define DRTM_MEM_REGION_PAGES_AND_TYPE(pages, type) \
	(((uint64_t)(pages) & (((uint64_t)1 << 52) - 1)) \
	 | (((uint64_t)(type) & 0x7) << 52))
#define DRTM_MEM_REGION_PAGES(pages_and_type) \
	((uint64_t)(pages_and_type) & (((uint64_t)1 << 52) - 1))
#define DRTM_MEM_REGION_TYPE(pages_and_type) \
	((uint8_t)((pages_and_type) >> 52 & 0x7))
enum drtm_mem_region_type {
	DRTM_MEM_REGION_TYPE_NORMAL                           = 0,
	DRTM_MEM_REGION_TYPE_NORMAL_WITH_CACHEABILITY_ATTRS   = 1,
	DRTM_MEM_REGION_TYPE_DEVICE                           = 2,
	DRTM_MEM_REGION_TYPE_NON_VOLATILE                     = 3,
	DRTM_MEM_REGION_TYPE_RESERVED                         = 4,
};


struct __packed drtm_mem_region_descr_table_v1 {
	uint16_t version;        /* Must be 1. */
	uint8_t __res[2];
	uint32_t num_regions;
	struct drtm_mem_region_descr_v1 regions[];
};


typedef struct drtm_mem_region_descr_v1 struct_drtm_mem_region_descr;
typedef struct drtm_mem_region_descr_table_v1 struct_drtm_mem_region_descr_table;


int drtm_dma_prot_init(void);
uint64_t drtm_features_dma_prot(void *ctx);
enum drtm_retc drtm_dma_prot_check_args(const drtm_dl_dma_prot_args_v1_t *a,
                                        int a_dma_prot_type,
                                        struct __protected_regions p);
enum drtm_retc drtm_dma_prot_engage(const drtm_dl_dma_prot_args_v1_t *a,
                                    int a_dma_prot_type);
enum drtm_retc drtm_dma_prot_disengage(void);
uint64_t drtm_unprotect_mem(void *ctx);
void drtm_dma_prot_serialise_table(char *dst, size_t *prot_table_size_out);

#endif /* DRTM_DMA_PROT_H */
